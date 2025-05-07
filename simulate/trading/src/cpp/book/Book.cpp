/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "Book.hpp"

#include "Simulation.hpp"
#include "SimulationException.hpp"


//-------------------------------------------------------------------------

Book::Book(
    Simulation* simulation,
    BookId id,
    size_t maxDepth,
    size_t detailedDepth)
    : m_simulation{simulation}, m_id{id}
{
    if (maxDepth == 0) {
        throw std::invalid_argument("Book maximum depth must be non-zero");
    }
    m_maxDepth = maxDepth;

    if (detailedDepth == 0) {
        throw std::invalid_argument("Book detailed depth must be non-zero");
    }
    m_detailedDepth = std::min(detailedDepth, maxDepth);

    setupL2Signal();
}

//-------------------------------------------------------------------------

taosim::decimal_t Book::midPrice() const noexcept
{
    if (m_buyQueue.empty() || m_sellQueue.empty()) [[unlikely]] {
        return {};
    }
    return DEC(0.5) * (m_buyQueue.back().price() + m_sellQueue.front().price());
}

//-------------------------------------------------------------------------

taosim::decimal_t Book::bestBid() const noexcept
{
    if (m_buyQueue.empty()) [[unlikely]] {
        return {};
    }
    return m_buyQueue.back().price();
}

//-------------------------------------------------------------------------

taosim::decimal_t Book::bestAsk() const noexcept
{
    if (m_sellQueue.empty()) [[unlikely]] {
        return {};
    }
    return m_sellQueue.front().price();
}

//-------------------------------------------------------------------------

MarketOrder::Ptr Book::placeMarketOrder(
    OrderDirection direction,
    Timestamp timestamp,
    taosim::decimal_t volume,
    taosim::decimal_t leverage,
    OrderClientContext clientCtx)
{
    const auto marketOrder = m_orderFactory.makeMarketOrder(
        direction, timestamp, volume, leverage);
    m_order2clientCtx[marketOrder->id()] = clientCtx;
    m_signals.orderCreated(
        marketOrder, OrderContext{clientCtx.agentId, m_id, clientCtx.clientOrderId});
    placeOrder(marketOrder);
    return marketOrder;
}

//-------------------------------------------------------------------------

LimitOrder::Ptr Book::placeLimitOrder(
    OrderDirection direction,
    Timestamp timestamp,
    taosim::decimal_t volume,
    taosim::decimal_t price,
    taosim::decimal_t leverage,
    OrderClientContext clientCtx)
{
    const auto limitOrder = m_orderFactory.makeLimitOrder(
        direction, timestamp, volume, price, leverage);
    m_order2clientCtx[limitOrder->id()] = clientCtx;
    m_signals.orderCreated(
        limitOrder, OrderContext{clientCtx.agentId, m_id, clientCtx.clientOrderId});
    placeOrder(limitOrder);
    return limitOrder;
}

//-------------------------------------------------------------------------

void Book::placeOrder(MarketOrder::Ptr order)
{
    switch (order->direction()) {
        case OrderDirection::BUY:
            if (m_sellQueue.empty()) return;
            processAgainstTheSellQueue(order, std::numeric_limits<taosim::decimal_t>::max());
            m_signals.marketOrderProcessed(
                order,
                [&] -> OrderContext {
                    const auto& clientCtx = m_order2clientCtx[order->id()];
                    return OrderContext{clientCtx.agentId, m_id, clientCtx.clientOrderId};
                }());
            return;
        case OrderDirection::SELL:
            if (m_buyQueue.empty()) return;
            processAgainstTheBuyQueue(order, std::numeric_limits<taosim::decimal_t>::min());
            m_signals.marketOrderProcessed(
                order,
                [&] -> OrderContext {
                    const auto& clientCtx = m_order2clientCtx[order->id()];
                    return OrderContext{clientCtx.agentId, m_id, clientCtx.clientOrderId};
                }());
            return;
        default:
            std::unreachable();
    }
}

//-------------------------------------------------------------------------

void Book::placeOrder(LimitOrder::Ptr order)
{
    switch (order->direction()) {
        case OrderDirection::BUY:
            placeLimitBuy(order);
            m_signals.limitOrderProcessed(
                order,
                [&] -> OrderContext {
                    const auto& clientCtx = m_order2clientCtx[order->id()];
                    return OrderContext{clientCtx.agentId, m_id, clientCtx.clientOrderId};
                }());
            return;
        case OrderDirection::SELL:
            placeLimitSell(order);
            m_signals.limitOrderProcessed(
                order,
                [&] -> OrderContext {
                    const auto& clientCtx = m_order2clientCtx[order->id()];
                    return OrderContext{clientCtx.agentId, m_id, clientCtx.clientOrderId};
                }());
            return;
        default:
            std::unreachable();
    }
}

//-------------------------------------------------------------------------

bool Book::cancelOrderOpt(OrderID orderId, std::optional<taosim::decimal_t> volumeToCancel)
{
    auto it = m_orderIdMap.find(orderId);
    if (it == m_orderIdMap.end()) return false;

    auto order = it->second;

    const taosim::decimal_t orderVolume = order->volume();
    taosim::decimal_t volumeToCancelActual =
        std::min(volumeToCancel.value_or(orderVolume), orderVolume);

    volumeToCancelActual = taosim::util::round(volumeToCancelActual, 
        m_simulation->exchange()->config().parameters().volumeIncrementDecimals);

    if (m_simulation->debug()) {
        const auto ctx = m_order2clientCtx[order->id()];
        const auto& balances = m_simulation->exchange()->accounts()[ctx.agentId][m_id];
        m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", 
            m_simulation->currentTimestamp(), ctx.agentId, m_id, balances.quote, balances.base);
    }
    m_signals.cancelOrderDetails(order, volumeToCancelActual, m_id);

    auto& orderSideLevels = order->direction() == OrderDirection::BUY ? m_buyQueue : m_sellQueue;
    auto level = std::lower_bound(orderSideLevels.begin(), orderSideLevels.end(), order->price());

    if (volumeToCancelActual == orderVolume) {
        std::erase_if(
            *level, [orderId](const auto orderOnLevel) { return orderOnLevel->id() == orderId; });
        if (level->empty()) {
            std::erase_if(
                orderSideLevels,
                [&](const TickContainer& levelInBook) {
                    return level->price() == levelInBook.price();
                });
        }
        m_orderIdMap.erase(orderId);
    }
    else {
        order->removeVolume(volumeToCancelActual);
    }
    level->updateVolume(
        -taosim::util::round(
            volumeToCancelActual * taosim::util::dec1p(order->leverage()),
            m_simulation->exchange()->config().parameters().volumeIncrementDecimals));

    m_signals.cancel(order->id(), volumeToCancelActual);
    
    if (m_simulation->debug()) {
        const auto ctx = m_order2clientCtx[order->id()];
        const auto& balances = m_simulation->exchange()->accounts()[ctx.agentId][m_id];
        m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), ctx.agentId, m_id, balances.quote, balances.base);
    }

    return true;
}

//-------------------------------------------------------------------------

bool Book::tryGetOrder(OrderID id, LimitOrder::Ptr& orderPtr) const
{
    if (auto it = m_orderIdMap.find(id); it != m_orderIdMap.end()) {
        orderPtr = it->second;
        return true;
    }
    return false;
}

//-------------------------------------------------------------------------

std::optional<LimitOrder::Ptr> Book::getOrder(OrderID orderId) const
{
    auto it = m_orderIdMap.find(orderId);
    return it != m_orderIdMap.end() ? std::make_optional(it->second) : std::nullopt;
}

//-------------------------------------------------------------------------

void Book::placeLimitBuy(LimitOrder::Ptr order)
{
    if (m_sellQueue.empty() || order->price() < m_sellQueue.front().price()) {
        auto firstLessThan = std::find_if(
            m_buyQueue.rbegin(),
            m_buyQueue.rend(),
            [order](const auto& level) { return level.price() <= order->price(); });

        if (firstLessThan != m_buyQueue.rend() && firstLessThan->price() == order->price()) {
            registerLimitOrder(order);
            firstLessThan->push_back(order);
        }
        else {
            TickContainer tov{order->price()};
            registerLimitOrder(order);
            tov.push_back(order);
            m_buyQueue.insert(firstLessThan.base(), std::move(tov));
            m_lastBetteringBuyOrder = order;
        }
    }
    else {
        processAgainstTheSellQueue(order, order->price());

        if (order->volume() > 0_dec) {
            placeOrder(order);
        } else {
            unregisterLimitOrder(order);
        }
    }
}

//-------------------------------------------------------------------------

void Book::placeLimitSell(LimitOrder::Ptr order)
{
    if (m_buyQueue.empty() || order->price() > m_buyQueue.back().price()) {
        auto firstGreaterThan = std::find_if(
            m_sellQueue.begin(),
            m_sellQueue.end(),
            [order](const auto& level) { return level.price() >= order->price(); });

        if (firstGreaterThan != m_sellQueue.end() && firstGreaterThan->price() == order->price()) {
            registerLimitOrder(order);
            firstGreaterThan->push_back(order);
        }
        else {
            TickContainer tov{order->price()};
            registerLimitOrder(order);
            tov.push_back(order);
            m_sellQueue.insert(firstGreaterThan, std::move(tov));
            m_lastBetteringSellOrder = order;
        }
    }
    else {
        processAgainstTheBuyQueue(order, order->price());

        if (order->volume() > 0_dec) {
            placeOrder(order);
        } else {
            unregisterLimitOrder(order);
        }
    }
}

//-------------------------------------------------------------------------

void Book::registerLimitOrder(LimitOrder::Ptr order)
{
    m_orderIdMap[order->id()] = order;
    if (m_simulation->debug()) {
        const auto ctx = m_order2clientCtx[order->id()];
        auto& balances = m_simulation->exchange()->accounts().at(ctx.agentId).at(m_id);
        fmt::println("{} | AGENT #{} BOOK {} : REGISTERED {} ORDER #{} FOR {}@{}| RESERVED {} QUOTE + {} BASE | BALANCES : QUOTE {}  BASE {}", m_simulation->currentTimestamp(), 
            ctx.agentId, m_id, order->direction() == OrderDirection::BUY ? "BUY" : "SELL", 
            order->id(), order->leverage() > 0_dec ? fmt::format("{}x{}",1_dec + order->leverage(),order->volume()) : fmt::format("{}",order->volume()), order->price(),
            balances.quote.getReservation(order->id()).value_or(0_dec), balances.base.getReservation(order->id()).value_or(0_dec),
            balances.quote, balances.base);
    } 
}

//-------------------------------------------------------------------------

void Book::unregisterLimitOrder(LimitOrder::Ptr order)
{
    m_signals.unregister(order, m_id);
    m_orderIdMap.erase(order->id());
    m_order2clientCtx.erase(order->id());
}

//-------------------------------------------------------------------------

void Book::logTrade(
    OrderDirection direction,
    OrderID aggressorId,
    OrderID restingId,
    taosim::decimal_t volume,
    taosim::decimal_t execPrice)
{
    Trade::Ptr trade = m_tradeFactory.makeRecord(
        m_simulation->currentTimestamp(), direction, aggressorId, restingId, volume, execPrice);
    m_signals.trade(trade, m_id);
}

//-------------------------------------------------------------------------

void Book::printCSV() const
{
    this->printCSV(5);
}

//-------------------------------------------------------------------------

void Book::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();

        auto serializeLevelBroad = [](rapidjson::Document& json, const TickContainer& level) {
            json.SetObject();
            auto& allocator = json.GetAllocator();
            json.AddMember(
                "price", rapidjson::Value{taosim::util::decimal2double(level.price())}, allocator);
            json.AddMember(
                "volume",
                rapidjson::Value{
                    taosim::util::decimal2double([&level] {
                        taosim::decimal_t volumeOnLevel{};
                        for (const auto& order : level) {
                            volumeOnLevel += order->volume();
                        }
                        return volumeOnLevel;
                    }())},
                allocator);
        };

        rapidjson::Value bidsJson{rapidjson::kArrayType};
        for (const auto& level : m_buyQueue | views::reverse | views::take(m_detailedDepth)) {
            rapidjson::Document levelJson{&allocator};
            level.jsonSerialize(levelJson);
            bidsJson.PushBack(levelJson, allocator);
        }
        for (const auto& level : m_buyQueue | views::reverse | views::drop(m_detailedDepth)) {
            rapidjson::Document levelJson{&allocator};
            serializeLevelBroad(levelJson, level);
            bidsJson.PushBack(levelJson, allocator);
        }
        json.AddMember(
            "bid", bidsJson.Size() > 0 ? bidsJson : rapidjson::Value{}.SetNull(), allocator);

        rapidjson::Value asksJson{rapidjson::kArrayType};
        for (const auto& level : m_sellQueue | views::take(m_detailedDepth)) {
            rapidjson::Document levelJson{&allocator};
            level.jsonSerialize(levelJson);
            asksJson.PushBack(levelJson, allocator);
        }
        for (const auto& level : m_sellQueue | views::drop(m_detailedDepth)) {
            rapidjson::Document levelJson{&allocator};
            serializeLevelBroad(levelJson, level);
            asksJson.PushBack(levelJson, allocator);
        }
        json.AddMember(
            "ask", asksJson.Size() > 0 ? asksJson : rapidjson::Value{}.SetNull(), allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void Book::printCSV(uint32_t depth) const
{
    std::cout << "ask";
    dumpCSVLOB(m_sellQueue.cbegin(), m_sellQueue.cend(), depth);
    std::cout << "\n";

    std::cout << "bid";
    dumpCSVLOB(m_buyQueue.crbegin(), m_buyQueue.crend(), depth);
    std::cout << "\n";
}

//-------------------------------------------------------------------------

void Book::setupL2Signal()
{
    m_signals.limitOrderProcessed.connect([this](auto&&... args) {
        if (!m_initMode) {
            emitL2Signal(std::forward<decltype(args)>(args)...);
        }
    });
    m_signals.marketOrderProcessed.connect([this](auto&&... args) {
        emitL2Signal(std::forward<decltype(args)>(args)...);
    });
    m_signals.trade.connect([this](auto&&... args) {
        emitL2Signal(std::forward<decltype(args)>(args)...);
    });
    m_signals.cancel.connect([this](auto&&... args) {
        emitL2Signal(std::forward<decltype(args)>(args)...);
    });
}

//-------------------------------------------------------------------------
