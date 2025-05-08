/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "MultiBookExchangeAgent.hpp"

#include "BookFactory.hpp"
#include "FeePolicyFactory.hpp"
#include "Simulation.hpp"
#include "util.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <date/date.h>
#include <fmt/core.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <source_location>
#include <sstream>
#include <string_view>
#include <utility>

//-------------------------------------------------------------------------

MultiBookExchangeAgent::MultiBookExchangeAgent(Simulation* simulation) noexcept
    : Agent{simulation, "EXCHANGE"}
{}

//-------------------------------------------------------------------------

std::span<Book::Ptr> MultiBookExchangeAgent::books() noexcept
{
    return m_books;
}

//-------------------------------------------------------------------------

taosim::accounting::Account& MultiBookExchangeAgent::account(const LocalAgentId& agentId)
{
    return m_clearingManager->accounts()[agentId];
}

//-------------------------------------------------------------------------

taosim::accounting::AccountRegistry& MultiBookExchangeAgent::accounts() noexcept
{
    return m_clearingManager->accounts();
}

//-------------------------------------------------------------------------

ExchangeSignals* MultiBookExchangeAgent::signals(BookId bookId)
{
    return m_signals.at(bookId).get();
}

//-------------------------------------------------------------------------

Process* MultiBookExchangeAgent::process(const std::string& name, BookId bookId)
{
    return m_bookProcessManager->at(name).at(bookId).get();
}

//-------------------------------------------------------------------------

taosim::exchange::ClearingManager& MultiBookExchangeAgent::clearingManager() noexcept
{
    return *m_clearingManager;
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::publishState()
{
    const Timestamp now = simulation()->currentTimestamp();

    if (now < m_gracePeriod) return;

    simulation()->dispatchMessage(
        now,
        0,
        name(),
        "DISTRIBUTED_PROXY_AGENT",
        "MULTIBOOK_STATE_PUBLISH",
        MessagePayload::create<BookStateMessagePayload>([this] -> rapidjson::Document {
            rapidjson::Document json;
            jsonSerialize(json);
            return json;
        }()));
    
    if (!m_retainRecord) {
        m_L3Record.clear();
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::retainRecord(bool flag) noexcept
{
    m_retainRecord = flag;
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::setParallel(bool flag) noexcept
{
    m_parallel = flag;
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::checkMarginCall() noexcept
{

    taosim::decimal_t bestAsk{}, bestBid{};
    for (const auto book : m_books) {
        const auto bookId = book->id();
        
        //-------------------------- Margin Buying orders ------------------------
        if (!book->buyQueue().empty()){
            
            auto bestBuyDeque = &book->buyQueue().back();
            bestBid = bestBuyDeque->price();
            auto& m_marginBuy = m_clearingManager->getMarginBuys();
            
            if (!m_marginBuy.empty()){
                auto marginIt = m_marginBuy.find(bookId);
                
                if (marginIt != m_marginBuy.end()) {
                    auto& marginBuyOrders = marginIt->second;

                    auto it = marginBuyOrders.upper_bound(bestBid);

                    for (auto tempIt = it; tempIt != marginBuyOrders.end(); ++tempIt) {
                        const auto& ids = tempIt->second;
                        for (auto idIt = ids.begin(); idIt != ids.end(); ++idIt) {

                            const auto& loan = accounts()[idIt->agentId][bookId].getLoan(idIt->orderId);
                            if (loan.has_value()){
                                taosim::decimal_t remainingVolume = book->calculatCorrespondingVolume(loan->get().amount());

                                simulation()->logDebug("Margin Call for BUY order #{} of agent {} at price {} (marginCall:{}) in Book {} for volume {}x{}",
                                    idIt->orderId,
                                    idIt->agentId,
                                    bestBid,
                                    loan->get().marginCallPrice(),
                                    bookId,
                                    taosim::util::dec1p(loan->get().leverage()),
                                    remainingVolume
                                );

                                simulation()->dispatchMessageWithPriority(
                                    simulation()->currentTimestamp(),
                                    0,
                                    accounts().idBimap().right.at(idIt->agentId),
                                    name(),
                                    "PLACE_ORDER_MARKET",
                                    MessagePayload::create<PlaceOrderMarketPayload>(
                                        OrderDirection::SELL,
                                        remainingVolume,
                                        bookId),
                                    m_marginCallCounter++
                                );

                                // throw std::runtime_error("\n#################    CHECK THE RESULT     ##################\n");
                            }

                        }
                    }
                    ///##/ This is important to check whether the order successfully happens
                    // if it is not then it should be resend
                    // if it succeeds it should remove the id from m_marginBuy
                    // but if we don't remove it here it might happen several times?
                    // this should be checked and the same for m_marginSell ///##/
                    marginBuyOrders.erase(it, marginBuyOrders.end());

                    if (marginBuyOrders.empty()) {
                        m_marginBuy.erase(marginIt);
                    }
                }       
            }
        }

        //-------------------------- Short Selling orders ------------------------
        if (!book->sellQueue().empty()){
            auto bestSellDeque = &book->sellQueue().front();
            bestAsk = bestSellDeque->price();
            auto& m_marginSell = m_clearingManager->getMarginSells();
            
            if (!m_marginSell.empty()){
                auto marginIt = m_marginSell.find(bookId);
                if (marginIt != m_marginSell.end()) {
                    auto& marginSellOrders = marginIt->second;

                    auto it = marginSellOrders.lower_bound(bestAsk);

                    for (auto tempIt = marginSellOrders.begin(); tempIt != it; ++tempIt) {

                        const auto& ids = tempIt->second;
                        
                        for (auto idIt = ids.begin(); idIt != ids.end(); ++idIt) {

                            const auto& loan = accounts()[idIt->agentId][bookId].getLoan(idIt->orderId);
                            if (loan.has_value()){
                                taosim::decimal_t remainingVolume = loan->get().amount();
                                

                                simulation()->logDebug("Margin Call for SELL order #{} of agent {} at price {} (marginCall:{}) in Book {} for volume {}x{}",
                                    idIt->orderId,
                                    idIt->agentId,
                                    bestAsk,
                                    loan->get().marginCallPrice(),
                                    bookId,
                                    taosim::util::dec1p(loan->get().leverage()),
                                    remainingVolume
                                );
                                
                                simulation()->dispatchMessageWithPriority(
                                    simulation()->currentTimestamp(),
                                    0,
                                    accounts().idBimap().right.at(idIt->agentId),
                                    name(),
                                    "PLACE_ORDER_MARKET",
                                    MessagePayload::create<PlaceOrderMarketPayload>(
                                        OrderDirection::BUY,
                                        remainingVolume,
                                        bookId),
                                    m_marginCallCounter++
                                );

                            }
                            
                        }
                        
                    }
                    
                    
                    marginSellOrders.erase(marginSellOrders.begin(), it);

                    if (marginSellOrders.empty()) {
                        m_marginSell.erase(marginIt);
                    }
                }
            }
            
        }
    }

}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::configure(const pugi::xml_node& node)
{
    Agent::configure(node);

    m_config.configure(node);

    // TODO: This monstrosity should be split up somehow.
    try {
        m_gracePeriod = node.attribute("gracePeriod").as_ullong();
        m_maintenanceMargin = taosim::util::double2decimal(node.attribute("maintenanceMargin").as_double());
        m_maxLeverage = taosim::util::double2decimal(node.attribute("maxLeverage").as_double());
        m_maxLoan = taosim::util::double2decimal(node.attribute("maxLoan").as_double());
        const taosim::decimal_t maxAllowedMaintenance = 1_dec / (2_dec * taosim::util::dec1p(m_maxLeverage));
        if (m_maintenanceMargin > maxAllowedMaintenance){
            throw std::invalid_argument(fmt::format(
                "maintanceMargin {} cannot be less than {} with maxLeverage set to {}",
                m_maintenanceMargin,
                maxAllowedMaintenance,
                m_maxLeverage
            ));
        }

        m_eps = taosim::util::double2decimal(node.attribute("eps").as_double());

        const auto booksNode = node.child("Books");
        const uint16_t bookCount = booksNode.attribute("instanceCount").as_uint();
        const std::string bookAlgorithm = booksNode.attribute("algorithm").as_string();
        const size_t maxDepth = booksNode.attribute("maxDepth").as_ullong(1024);
        const size_t detailedDepth = booksNode.attribute("detailedDepth").as_ullong(maxDepth);

        m_bookProcessManager = BookProcessManager::fromXML(
            booksNode, const_cast<Simulation*>(simulation()));
        m_clearingManager = std::make_unique<taosim::exchange::ClearingManager>(
            this,
            taosim::exchange::FeePolicyFactory::createFromXML(node.child("FeePolicy")),
            taosim::exchange::OrderPlacementValidator::Parameters{
                .volumeIncrementDecimals = m_config.parameters().volumeIncrementDecimals,
                .priceIncrementDecimals = m_config.parameters().priceIncrementDecimals,
                .baseIncrementDecimals = m_config.parameters().baseIncrementDecimals,
                .quoteIncrementDecimals = m_config.parameters().quoteIncrementDecimals
            });


        const auto balancesNode = node.child("Balances");
        const auto baseNode = balancesNode.child("Base");
        const auto quoteNode = balancesNode.child("Quote");

        std::chrono::system_clock::time_point startTimePoint;
        std::string L2LogTag;
        int L2Depth;
        std::string L3LogTag;
        pugi::xml_node loggingNode;
        pugi::xml_node L2Node;
        pugi::xml_node L3Node;
        if (loggingNode = node.child("Logging")) {
            std::istringstream in{loggingNode.attribute("startDate").as_string()};
            // TODO: Handle the timezone
            date::from_stream(in, "%Y-%m-%d %H:%M:%S", startTimePoint);
            if (L2Node = loggingNode.child("L2")) {
                L2LogTag = L2Node.attribute("tag").as_string();
                L2Depth = L2Node.attribute("depth").as_int(21);
            }
            if (L3Node = loggingNode.child("L3")) {
                L3LogTag = L3Node.attribute("tag").as_string();
            }
        }

        taosim::accounting::Account accountTemplate;        
        const fs::path logDir = simulation()->logDir();
        for (uint16_t bookId{}; bookId < bookCount; ++bookId) {
            auto book = BookFactory::createBook(
                bookAlgorithm,
                simulation(),
                bookId,
                maxDepth,
                detailedDepth);
            book->signals().orderCreated.connect(
                [this](Order::Ptr order, OrderContext ctx) { orderCallback(order, ctx); });
            book->signals().trade.connect(
                [this](Trade::Ptr trade, BookId bookId) { tradeCallback(trade, bookId); });
            book->signals().unregister.connect(
                [this](LimitOrder::Ptr order, BookId bookId) {
                    unregisterLimitOrderCallback(order, bookId);
                });
            book->signals().cancelOrderDetails.connect(
                [this](LimitOrder::Ptr order, taosim::decimal_t volumeToCancel, BookId bookId) {
                    m_clearingManager->handleCancelOrder({
                        .bookId = bookId,
                        .order = order,
                        .volumeToCancel = volumeToCancel
                    });
                    m_L3Record[bookId].push(std::make_shared<Cancellation>(order->id(), volumeToCancel));
                });
            book->signals().marketOrderProcessed.connect(
                [this](MarketOrder::Ptr marketOrder, OrderContext ctx) {
                    marketOrderProcessedCallback(marketOrder, ctx);
                });
            m_books.push_back(book);
            m_signals[bookId] = std::make_unique<ExchangeSignals>();
            m_L3Record[bookId] = {};
            accountTemplate.holdings().emplace_back(
                taosim::accounting::Balance::fromXML(baseNode,
                    m_config.parameters().baseIncrementDecimals),
                taosim::accounting::Balance::fromXML(quoteNode,
                    m_config.parameters().quoteIncrementDecimals),
                m_config.parameters().baseIncrementDecimals,
                m_config.parameters().quoteIncrementDecimals);
            accountTemplate.activeOrders().emplace_back();
            if (loggingNode) {
                if (L2Node) {
                    const fs::path logPath =
                    logDir / fmt::format(
                        "{}L2-{}.log",
                        !L2LogTag.empty() ? L2LogTag + "-" : "",
                        bookId);
                    m_L2Loggers[bookId] = std::make_unique<L2Logger>(
                        logPath,
                        L2Depth,
                        startTimePoint,
                        book->signals(),
                        simulation());
                }
                if (L3Node) {
                    const fs::path logPath =
                    logDir / fmt::format(
                        "{}L3-{}.log",
                        !L3LogTag.empty() ? L3LogTag + "-" : "",
                        bookId);
                    m_L3EventLoggers[bookId] = std::make_unique<L3EventLogger>(
                        logPath,
                        startTimePoint,
                        m_signals.at(bookId)->L3,
                        simulation());
                }
            }
        }
        m_clearingManager->accounts().setAccountTemplate(std::move(accountTemplate));

        if (const uint32_t remoteAgentCount = node.attribute("remoteAgentCount").as_uint()) {
            for (AgentId agentId{}; agentId < remoteAgentCount; ++agentId) {
                m_clearingManager->accounts().registerRemote();
            }
        }

        for (BookId bookId = 0; bookId < bookCount; ++bookId) {
            m_parallelQueues.push_back(MessageQueue{});
        }

        simulation()->signals().step.connect([this] { publishState(); });
        simulation()->signals().stop.connect([this] {
            Simulation* simulation = const_cast<Simulation*>(this->simulation());
            simulation->m_messageQueue.clear();
            publishState();
            Message::Ptr publishMsg = simulation->m_messageQueue.top();
            simulation->m_messageQueue.pop();
            simulation->deliverMessage(publishMsg);
        });
        simulation()->signals().timeAboutToProgress.connect([this](Timespan timespan) {
            timeProgressCallback(timespan);
        });
        simulation()->signals().agentsCreated.connect([=, this] {
            if (!balancesNode.attribute("log").as_bool()) return;
            for (BookId bookId = 0; bookId < bookCount; ++bookId) {
                auto balanceLogger = std::make_unique<taosim::accounting::BalanceLogger>(
                    logDir / fmt::format("bals-{}.log", bookId),
                    m_signals.at(bookId)->L3,
                    &accounts());
                m_balanceLoggers.push_back(std::move(balanceLogger));
            }
        });
    }
    catch (...) {
        handleException();
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::receiveMessage(Message::Ptr msg)
{
    try {
        if (msg->type == "DISTRIBUTED_AGENT_RESET") {
            return handleDistributedMessage(msg);
        }
    }
    catch (...) {
        handleException();
    }

    if (m_parallel) {
        try {
            if (msg->type == "PLACE_ORDER_MARKET") {
                auto payload = std::dynamic_pointer_cast<PlaceOrderMarketPayload>(msg->payload);
                m_parallelQueues[payload->bookId].push(msg);
            }
            else if (msg->type == "DISTRIBUTED_PLACE_ORDER_MARKET") {
                auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
                auto subPayload = std::dynamic_pointer_cast<PlaceOrderMarketPayload>(payload->payload);
                m_parallelQueues[subPayload->bookId].push(msg);
            }
            else if (msg->type == "PLACE_ORDER_LIMIT") {
                auto payload = std::dynamic_pointer_cast<PlaceOrderLimitPayload>(msg->payload);
                m_parallelQueues[payload->bookId].push(msg);
            }
            else if (msg->type == "DISTRIBUTED_PLACE_ORDER_LIMIT") {
                auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
                auto subPayload = std::dynamic_pointer_cast<PlaceOrderLimitPayload>(payload->payload);
                m_parallelQueues[subPayload->bookId].push(msg);
            }
            else if (msg->type == "RETRIEVE_ORDERS") {
                auto payload = std::dynamic_pointer_cast<RetrieveOrdersPayload>(msg->payload);
                m_parallelQueues[payload->bookId].push(msg);
            }
            else if (msg->type == "DISTRIBUTED_RETRIEVE_ORDERS") {
                auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
                auto subPayload = std::dynamic_pointer_cast<RetrieveOrdersPayload>(payload->payload);
                m_parallelQueues[subPayload->bookId].push(msg);
            }
            else if (msg->type == "CANCEL_ORDERS") {
                auto payload = std::dynamic_pointer_cast<CancelOrdersPayload>(msg->payload);
                m_parallelQueues[payload->bookId].push(msg);
            }
            else if (msg->type == "DISTRIBUTED_CANCEL_ORDERS") {
                auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
                auto subPayload = std::dynamic_pointer_cast<CancelOrdersPayload>(payload->payload);
                m_parallelQueues[subPayload->bookId].push(msg);
            }
            else if (msg->type == "RETRIEVE_L1") {
                auto payload = std::dynamic_pointer_cast<RetrieveL1Payload>(msg->payload);
                m_parallelQueues[payload->bookId].push(msg);
            }
            else if (msg->type == "RETRIEVE_BOOK_ASK") {
                auto payload = std::dynamic_pointer_cast<RetrieveBookPayload>(msg->payload);
                m_parallelQueues[payload->bookId].push(msg);
            }
            else if (msg->type == "RETRIEVE_BOOK_BID") {
                auto payload = std::dynamic_pointer_cast<RetrieveBookPayload>(msg->payload);
                m_parallelQueues[payload->bookId].push(msg);
            }
            else {
                auto it = std::min_element(
                    m_parallelQueues.begin(),
                    m_parallelQueues.end(),
                    [](const auto& a, const auto& b) { return a.size() < b.size(); });
                it->push(msg);
            }
        } catch (const std::exception &exc) {
            fmt::println(
                "receiveMessage: Error processing {} Message From {} at {} to {} at {} : {}\n{}",
                msg->type,
                msg->source,
                msg->occurrence,
                fmt::join(msg->targets, ","),
                msg->arrival,
                exc.what(),
                taosim::json::jsonSerializable2str(msg));
        }
        // simulation()->logDebug("{}: QUEUE {} {}", simulation()->currentTimestamp(), msg->arrival, taosim::json::jsonSerializable2str(msg));
        return;
    }
    try {
        if (msg->type.starts_with("DISTRIBUTED")) {
            return handleDistributedMessage(msg);
        }
        handleLocalMessage(msg);
    }
    catch (...) {
        handleException();
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        auto& allocator = json.GetAllocator();
        m_clearingManager->accounts().checkpointSerialize(json);

        for (AgentId agentId : views::keys(m_clearingManager->accounts())) {
            const auto agentIdStr = std::to_string(agentId);
            const char* agentIdCStr = agentIdStr.c_str();
            rapidjson::Document ordersJson{rapidjson::kArrayType, &allocator};
            json[agentIdCStr].AddMember("orders", ordersJson, allocator);
        }

        for (const auto book : m_books) {
            const BookId bookId = book->id();
            for (const auto& [agentId, holdings] : m_clearingManager->accounts()) {
                const auto agentIdStr = std::to_string(agentId);
                const char* agentIdCStr = agentIdStr.c_str();
                json[agentIdCStr]["orders"].PushBack(
                    rapidjson::Document{rapidjson::kArrayType, &allocator},
                    allocator);
            }
            for (const TickContainer& bidLevel : book->buyQueue()) {
                for (const auto& bid : bidLevel) {
                    const auto [agentId, clientOrderId] = m_books[bookId]->orderClientContext(bid->id());
                    const auto agentIdStr = std::to_string(agentId);
                    const char* agentIdCStr = agentIdStr.c_str();
                    rapidjson::Document orderJson{&allocator};
                    bid->checkpointSerialize(orderJson);
                    taosim::json::setOptionalMember(orderJson, "clientOrderId", clientOrderId);
                    json[agentIdCStr]["orders"][bookId].PushBack(orderJson, allocator);
                }
            }
            for (const TickContainer& askLevel : book->sellQueue()) {
                for (const auto& ask : askLevel) {
                    const auto [agentId, clientOrderId] = m_books[bookId]->orderClientContext(ask->id());
                    const auto agentIdStr = std::to_string(agentId);
                    const char* agentIdCStr = agentIdStr.c_str();
                    rapidjson::Document orderJson{&allocator};
                    ask->checkpointSerialize(orderJson);
                    taosim::json::setOptionalMember(orderJson, "clientOrderId", clientOrderId);
                    json[agentIdCStr]["orders"][bookId].PushBack(orderJson, allocator);
                }
            }
        }
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::jsonSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("logDir", rapidjson::Value{simulation()->logDir().c_str(), allocator}, allocator);
        auto serializeBooks = [this](rapidjson::Document& json) {
            json.SetArray();
            auto& allocator = json.GetAllocator();
            for (const auto book : m_books) {
                const BookId bookId = book->id();
                rapidjson::Document bookJson{rapidjson::kObjectType, &allocator};
                bookJson.AddMember("bookId", rapidjson::Value{bookId}, allocator);
                m_L3Record.at(bookId).jsonSerialize(bookJson, "record");
                rapidjson::Document bidAskJson{&allocator};
                book->jsonSerialize(bidAskJson);
                bookJson.AddMember("bid", bidAskJson["bid"], allocator);
                bookJson.AddMember("ask", bidAskJson["ask"], allocator);
                json.PushBack(bookJson, allocator);
            }
        };
        taosim::json::serializeHelper(json, "books", serializeBooks);
        auto serializeAccounts = [this](rapidjson::Document& json) {
            auto& allocator = json.GetAllocator();
            m_clearingManager->accounts().jsonSerialize(json);
            for (AgentId agentId : views::keys(m_clearingManager->accounts())) {
                const auto agentIdStr = std::to_string(agentId);
                const char* agentIdCStr = agentIdStr.c_str();
                rapidjson::Document ordersJson{rapidjson::kArrayType, &allocator};
                json[agentIdCStr].AddMember("orders", ordersJson, allocator);
            }
            for (const auto book : m_books) {
                const BookId bookId = book->id();
                for (const auto& [agentId, holdings] : m_clearingManager->accounts()) {
                    const auto agentIdStr = std::to_string(agentId);
                    const char* agentIdCStr = agentIdStr.c_str();
                    json[agentIdCStr]["orders"].PushBack(
                        rapidjson::Document{rapidjson::kArrayType, &allocator},
                        allocator);
                }
                for (const TickContainer& bidLevel : book->buyQueue()) {
                    for (const auto& bid : bidLevel) {
                        const auto [agentId, clientOrderId] = m_books[bookId]->orderClientContext(bid->id());
                        const auto agentIdStr = std::to_string(agentId);
                        const char* agentIdCStr = agentIdStr.c_str();
                        rapidjson::Document orderJson{&allocator};
                        bid->jsonSerialize(orderJson);
                        taosim::json::setOptionalMember(orderJson, "clientOrderId", clientOrderId);
                        json[agentIdCStr]["orders"][bookId].PushBack(orderJson, allocator);
                    }
                }
                for (const TickContainer& askLevel : book->sellQueue()) {
                    for (const auto& ask : askLevel) {
                        const auto [agentId, clientOrderId] = m_books[bookId]->orderClientContext(ask->id());
                        const auto agentIdStr = std::to_string(agentId);
                        const char* agentIdCStr = agentIdStr.c_str();
                        rapidjson::Document orderJson{&allocator};
                        ask->jsonSerialize(orderJson);
                        taosim::json::setOptionalMember(orderJson, "clientOrderId", clientOrderId);
                        json[agentIdCStr]["orders"][bookId].PushBack(orderJson, allocator);
                    }
                }
            }
        };
        taosim::json::serializeHelper(json, "accounts", serializeAccounts);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleException()
{
    try {
        throw;
    }
    catch (const std::exception& e) {
        fmt::println("{}", e.what());
        throw;
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleDistributedMessage(Message::Ptr msg)
{
    if (msg->type.ends_with("PLACE_ORDER_MARKET")) {
        handleDistributedPlaceMarketOrder(msg);
    }
    else if (msg->type.ends_with("PLACE_ORDER_LIMIT")) {
        handleDistributedPlaceLimitOrder(msg);
    }
    else if (msg->type.ends_with("RETRIEVE_ORDERS")) {
        handleDistributedRetrieveOrders(msg);
    }
    else if (msg->type.ends_with("CANCEL_ORDERS")) {
        handleDistributedCancelOrders(msg);
    }
    else if (msg->type.ends_with("RESET_AGENT")) {
        handleDistributedAgentReset(msg);
    }
    else {
        handleDistributedUnknownMessage(msg);
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleDistributedAgentReset(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
    const auto subPayload = std::dynamic_pointer_cast<ResetAgentsPayload>(payload->payload);

    std::vector<AgentId> valid = subPayload->agentIds
        | views::filter([&](AgentId agentId) { return accounts().contains(agentId); })
        | ranges::to<std::vector>;

    if (valid.empty()) {
        return;
    }

    std::vector<std::vector<Cancellation>> cancellations;
    for (AgentId agentId : valid) {
        simulation()->logDebug("{} | AGENT #{} : RESET-CANCELS", simulation()->currentTimestamp(), agentId);
        for (BookId bookId = 0; bookId < m_books.size(); ++bookId) {
            simulation()->logDebug("{} | AGENT #{} BOOK {} : RESET-CANCELS", simulation()->currentTimestamp(), agentId, bookId);
            std::vector<Cancellation> bookCancellations;
            const auto orders = accounts()[agentId].activeOrders()[bookId];
            const auto book = m_books.at(bookId);
            for (Order::Ptr order : orders) {
                if (auto limitOrder = std::dynamic_pointer_cast<LimitOrder>(order)) {
                    simulation()->logDebug("{} | AGENT #{} BOOK {} : START RESET-CANCEL OF ORDER {}", simulation()->currentTimestamp(), agentId, bookId, limitOrder->id());
                    if (book->cancelOrderOpt(limitOrder->id())) {
                        const Cancellation cancellation{limitOrder->id()};
                        bookCancellations.push_back(cancellation);
                        m_signals.at(bookId)->cancelLog(CancellationWithLogContext(
                            cancellation,
                            std::make_shared<CancellationLogContext>(
                                agentId,
                                bookId,
                                simulation()->currentTimestamp())));
                        simulation()->logDebug("{} | AGENT #{} BOOK {} : END RESET-CANCEL OF ORDER {}", simulation()->currentTimestamp(), agentId, bookId, limitOrder->id());
                    } else {
                        simulation()->logDebug("{} | AGENT #{} BOOK {} : RESET-CANCEL OF ORDER {} FAILED", simulation()->currentTimestamp(), agentId, bookId, limitOrder->id());
                    }
                }
            }
            cancellations.push_back(std::move(bookCancellations));
        }
        accounts().reset(agentId);
        simulation()->logDebug("{} | AGENT #{} : RESET-CANCELS DONE", simulation()->currentTimestamp(), agentId);
    }
    simulation()->logDebug("{} | ALL RESET-CANCELS DONE", simulation()->currentTimestamp());

    const auto allQueuesEmpty =
        ranges::all_of(m_parallelQueues, [](const auto& queue) { return queue.empty(); });
    if (m_parallel && !allQueuesEmpty) {
        simulation()->logDebug("{} | PARALLEL QUEUES NON-EMPTY UPON AGENT RESET", simulation()->currentTimestamp());
    }	
	
    const std::unordered_set<AgentId> resetAgentIds{valid.begin(), valid.end()};
    std::vector<MessageQueue::PrioritizedMessageWithId> messagesToKeep;
    auto& mainMsgQueue = simulation()->m_messageQueue;
    while (!mainMsgQueue.empty()) {
        auto prioMsgWithId = mainMsgQueue.prioTop();
        mainMsgQueue.pop();
        const auto distributedPayload =
            std::dynamic_pointer_cast<DistributedAgentResponsePayload>(prioMsgWithId.pmsg.msg->payload);
        if (distributedPayload && resetAgentIds.contains(distributedPayload->agentId)) {
            continue;
        }
        messagesToKeep.push_back(prioMsgWithId);
    }
    ThreadSafeMessageQueue newQueue;
    for (const auto& message : messagesToKeep) {
        newQueue.push(message);
    }
    simulation()->m_messageQueue = std::move(newQueue);
    simulation()->logDebug("{} | MESSAGE QUEUE CLEARED", simulation()->currentTimestamp());

    // for (const auto [bookId, bookCancellations] : views::enumerate(cancellations)) {
    //     if (bookCancellations.empty()) continue;
    //     respondToMessage(
    //         msg,
    //         MessagePayload::create<DistributedAgentResponsePayload>(
    //             payload->agentId,
    //             MessagePayload::create<CancelOrdersResponsePayload>(
    //                 bookCancellations
    //                     | views::transform([](const Cancellation& c) { return c.id; })
    //                     | ranges::to<std::vector>(),
    //                 MessagePayload::create<CancelOrdersPayload>(bookCancellations, bookId))),
    //         0);
    // }
    // simulation()->logDebug("{} | bookCancellations NOTIFIED", simulation()->currentTimestamp());

    simulation()->fastRespondToMessage(
        msg,
        MessagePayload::create<DistributedAgentResponsePayload>(
            payload->agentId,
            MessagePayload::create<ResetAgentsResponsePayload>(valid, subPayload)));
    simulation()->logDebug("{} | RESET COMPLETE", simulation()->currentTimestamp());
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleDistributedPlaceMarketOrder(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
    const auto subPayload = std::dynamic_pointer_cast<PlaceOrderMarketPayload>(payload->payload);

    if (simulation()->debug()) {
        const auto& balances = simulation()->exchange()->accounts()[payload->agentId][subPayload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), payload->agentId, subPayload->bookId, balances.quote, balances.base);
    }
    const OrderErrorCode ec = m_clearingManager->handleOrder(
        taosim::exchange::MarketOrderDesc{
            .agentId = payload->agentId,
            .payload = subPayload
        });
    if (simulation()->debug()) {
        const auto& balances = simulation()->exchange()->accounts()[payload->agentId][subPayload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), payload->agentId, subPayload->bookId, balances.quote, balances.base);
    }

    if (ec != OrderErrorCode::VALID) {
        simulation()->logDebug(
            "Invalid Market Order Placement by Distributed Agent - {} : {}",
            ec,
            taosim::json::jsonSerializable2str(payload));
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<DistributedAgentResponsePayload>(
                payload->agentId,
                MessagePayload::create<PlaceOrderMarketErrorResponsePayload>(
                    subPayload,
                    MessagePayload::create<ErrorResponsePayload>(
                        OrderErrorCode2StrView(ec).data()))));
    }

    const auto order = m_books[subPayload->bookId]->placeMarketOrder(
        subPayload->direction,
        msg->arrival,
        subPayload->volume,
        subPayload->leverage,
        OrderClientContext{payload->agentId, subPayload->clientOrderId});

    const auto retSubPayload =
        MessagePayload::create<PlaceOrderMarketResponsePayload>(order->id(), subPayload);

    respondToMessage(
        msg,
        MessagePayload::create<DistributedAgentResponsePayload>(
            payload->agentId,
            MessagePayload::create<PlaceOrderMarketResponsePayload>(order->id(), subPayload)),
        0);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleDistributedPlaceLimitOrder(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
    const auto subPayload = std::dynamic_pointer_cast<PlaceOrderLimitPayload>(payload->payload);

    if (simulation()->debug()) {
        const auto& balances = simulation()->exchange()->accounts()[payload->agentId][subPayload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), payload->agentId, subPayload->bookId, balances.quote, balances.base);
    }
    const auto ec = m_clearingManager->handleOrder(
        taosim::exchange::LimitOrderDesc{
            .agentId = payload->agentId,
            .payload = subPayload
        });
    if (simulation()->debug()) {
        const auto& balances = simulation()->exchange()->accounts()[payload->agentId][subPayload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), payload->agentId, subPayload->bookId, balances.quote, balances.base);
    }

    if (ec != OrderErrorCode::VALID) {
        simulation()->logDebug(
            "Invalid Limit Order Placement by Distributed Agent - {} : {}",
            ec,
            taosim::json::jsonSerializable2str(payload));
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<DistributedAgentResponsePayload>(
                payload->agentId,
                MessagePayload::create<PlaceOrderLimitErrorResponsePayload>(
                    subPayload,
                    MessagePayload::create<ErrorResponsePayload>(
                        OrderErrorCode2StrView(ec).data()))));
    }

    const auto order = m_books[subPayload->bookId]->placeLimitOrder(
        subPayload->direction,
        msg->arrival,
        subPayload->volume,
        subPayload->price,
        subPayload->leverage,
        OrderClientContext{payload->agentId, subPayload->clientOrderId});

    const auto retSubPayload =
        MessagePayload::create<PlaceOrderLimitResponsePayload>(order->id(), subPayload);

    respondToMessage(
        msg,
        MessagePayload::create<DistributedAgentResponsePayload>(
            payload->agentId,
            MessagePayload::create<PlaceOrderLimitResponsePayload>(order->id(), subPayload)),
        0);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleDistributedRetrieveOrders(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
    const auto subPayload = std::dynamic_pointer_cast<RetrieveOrdersPayload>(payload->payload);

    const auto book = m_books[subPayload->bookId];

    auto retSubPayload = MessagePayload::create<RetrieveOrdersResponsePayload>();
    for (OrderID id : subPayload->ids) {
        if (LimitOrder::Ptr order; book->tryGetOrder(id, order)) {
            retSubPayload->orders.push_back(*order);
        }
    }

    respondToMessage(
        msg,
        MessagePayload::create<DistributedAgentResponsePayload>(payload->agentId, retSubPayload),
        0);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleDistributedCancelOrders(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
    const auto subPayload = std::dynamic_pointer_cast<CancelOrdersPayload>(payload->payload);

    const auto bookId = subPayload->bookId;
    const auto book = m_books[bookId];

    std::vector<Cancellation> cancellations;
    std::vector<Cancellation> failures;
    for (const auto& cancellation : subPayload->cancellations) {        
        if (book->cancelOrderOpt(cancellation.id, cancellation.volume)) {
            cancellations.push_back(cancellation);
            m_signals[bookId]->cancelLog(CancellationWithLogContext(
                cancellation,
                std::make_shared<CancellationLogContext>(
                    payload->agentId, bookId, simulation()->currentTimestamp())));
        }
        else {
            failures.push_back(cancellation);
        }
    }

    if (!cancellations.empty()) {        
        std::vector<OrderID> orderIds;
        for (const Cancellation& canc : cancellations) {
            orderIds.push_back(canc.id);
        }
        respondToMessage(
            msg,
            MessagePayload::create<DistributedAgentResponsePayload>(
                payload->agentId,
                MessagePayload::create<CancelOrdersResponsePayload>(
                    std::move(orderIds),
                    MessagePayload::create<CancelOrdersPayload>(
                        std::move(cancellations), bookId)))
            );
    }

    if (!failures.empty()) {
        std::vector<OrderID> orderIds = failures
            | views::transform([](const Cancellation& c) { return c.id; })
            | ranges::to<std::vector>();
        auto errorMsg = fmt::format("Order IDs {} do not exist.", fmt::join(orderIds, ", "));
        auto retSubPayload = MessagePayload::create<CancelOrdersErrorResponsePayload>(
            std::move(orderIds),
            MessagePayload::create<CancelOrdersPayload>(std::move(failures), bookId), 
            MessagePayload::create<ErrorResponsePayload>(std::move(errorMsg))
        );
        respondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<DistributedAgentResponsePayload>(
                payload->agentId,
                retSubPayload),
            0);
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleDistributedUnknownMessage(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);

    auto retSubPayload =
        MessagePayload::create<ErrorResponsePayload>(
            fmt::format("Unknown message type: {}", msg->type));
    fastRespondToMessage(
        msg,
        "ERROR",
        MessagePayload::create<DistributedAgentResponsePayload>(
            payload->agentId,
            retSubPayload));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalMessage(Message::Ptr msg)
{
    if (msg->type == "PLACE_ORDER_MARKET") {
        handleLocalPlaceMarketOrder(msg);
    }
    else if (msg->type == "PLACE_ORDER_LIMIT") {
        handleLocalPlaceLimitOrder(msg);
    }
    else if (msg->type == "RETRIEVE_ORDERS") {
        handleLocalRetrieveOrders(msg);
    }
    else if (msg->type == "CANCEL_ORDERS") {
        handleLocalCancelOrders(msg);
    }
    else if (msg->type == "RETRIEVE_L1") {
        handleLocalRetrieveL1(msg);
    }
    else if (msg->type == "RETRIEVE_BOOK_ASK") {
        handleLocalRetrieveBookAsk(msg);
    }
    else if (msg->type == "RETRIEVE_BOOK_BID") {
        handleLocalRetrieveBookBid(msg);
    }
    else if (msg->type == "SUBSCRIBE_EVENT_ORDER_MARKET") {
        handleLocalMarketOrderSubscription(msg);
    }
    else if (msg->type == "SUBSCRIBE_EVENT_ORDER_LIMIT") {
        handleLocalLimitOrderSubscription(msg);
    }
    else if (msg->type == "SUBSCRIBE_EVENT_TRADE") {
        handleLocalTradeSubscription(msg);
    }
    else if (msg->type == "SUBSCRIBE_EVENT_ORDER_TRADE") {
        handleLocalTradeByOrderSubscription(msg);
    }
    else {
        handleLocalUnknownMessage(msg);
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalPlaceMarketOrder(Message::Ptr msg)
{
    const auto& payload = std::dynamic_pointer_cast<PlaceOrderMarketPayload>(msg->payload);

    if (simulation()->debug()) {
        auto agentId = accounts().idBimap().left.at(msg->source);
        const auto& balances = simulation()->exchange()->accounts()[agentId][payload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), agentId, payload->bookId, balances.quote, balances.base);
    }
    const OrderErrorCode ec = m_clearingManager->handleOrder(
        taosim::exchange::MarketOrderDesc{
            .agentId = msg->source,
            .payload = payload
        });
    if (simulation()->debug()) {
        auto agentId = accounts().idBimap().left.at(msg->source);
        const auto& balances = simulation()->exchange()->accounts()[agentId][payload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), agentId, payload->bookId, balances.quote, balances.base);
    }
    
    if (ec != OrderErrorCode::VALID) {
        simulation()->logDebug(
            "Invalid Market Order Placement by Local Agent - {} : {}",
            ec,
            taosim::json::jsonSerializable2str(payload));
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<PlaceOrderMarketErrorResponsePayload>(
                payload,
                MessagePayload::create<ErrorResponsePayload>(
                    OrderErrorCode2StrView(ec).data())));
    }

    const auto order = m_books[payload->bookId]->placeMarketOrder(
        payload->direction,
        msg->arrival,
        payload->volume,
        payload->leverage,
        OrderClientContext{accounts().idBimap().left.at(msg->source), payload->clientOrderId});

    respondToMessage(
        msg,
        MessagePayload::create<PlaceOrderMarketResponsePayload>(order->id(), payload),
        1);

    notifyMarketOrderSubscribers(order);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalPlaceLimitOrder(Message::Ptr msg)
{
    const auto& payload = std::dynamic_pointer_cast<PlaceOrderLimitPayload>(msg->payload);

    if (simulation()->debug()) {
        auto agentId = accounts().idBimap().left.at(msg->source);
        const auto& balances = simulation()->exchange()->accounts()[agentId][payload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), agentId, payload->bookId, balances.quote, balances.base);
    }
    const OrderErrorCode ec = m_clearingManager->handleOrder(
        taosim::exchange::LimitOrderDesc{
            .agentId = msg->source,
            .payload = payload
        });
    if (simulation()->debug()) {
        auto agentId = accounts().idBimap().left.at(msg->source);
        const auto& balances = simulation()->exchange()->accounts()[agentId][payload->bookId];
        simulation()->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", simulation()->currentTimestamp(), agentId, payload->bookId, balances.quote, balances.base);
    }

    if (ec != OrderErrorCode::VALID) {
        simulation()->logDebug(
            "Invalid Limit Order Placement by Local Agent - {} : {}",
            ec,
            taosim::json::jsonSerializable2str(payload));
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<PlaceOrderLimitErrorResponsePayload>(
                payload,
                MessagePayload::create<ErrorResponsePayload>(
                    OrderErrorCode2StrView(ec).data())));
    }

    const auto order = m_books[payload->bookId]->placeLimitOrder(
        payload->direction,
        msg->arrival,
        payload->volume,
        payload->price,
        payload->leverage,
        OrderClientContext{accounts().idBimap().left.at(msg->source), payload->clientOrderId});

    respondToMessage(
        msg,
        MessagePayload::create<PlaceOrderLimitResponsePayload>(order->id(), payload),
        1);

    notifyLimitOrderSubscribers(order);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalRetrieveOrders(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<RetrieveOrdersPayload>(msg->payload);

    const auto book = m_books[payload->bookId];

    respondToMessage(
        msg,
        MessagePayload::create<RetrieveOrdersResponsePayload>(
            payload->ids
                | views::transform([&](OrderID id) { return book->getOrder(id); })
                | views::filter([](std::optional<LimitOrder::Ptr> order) { return order.has_value(); })
                | views::transform([](std::optional<LimitOrder::Ptr> order) { return **order; })
                | ranges::to<std::vector>(),
            payload->bookId));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalCancelOrders(Message::Ptr msg)
{
    const auto& payload = std::dynamic_pointer_cast<CancelOrdersPayload>(msg->payload);

    const auto bookId = payload->bookId;
    const auto book = m_books[bookId];

    std::vector<Cancellation> cancellations;
    std::vector<Cancellation> failures;
    for (auto& cancellation : payload->cancellations) {
        ////###
        if (cancellation.volume.has_value()){
            cancellation.volume = taosim::util::round(cancellation.volume.value(), m_config.parameters().volumeIncrementDecimals);
        }
        
        if (book->cancelOrderOpt(cancellation.id, cancellation.volume)) {
            cancellations.push_back(cancellation);
            m_signals.at(bookId)->cancelLog(CancellationWithLogContext(
                cancellation,
                std::make_shared<CancellationLogContext>(
                    accounts().idBimap().left.at(msg->source),
                    bookId,
                    simulation()->currentTimestamp())));
        }
        else {
            failures.push_back(cancellation);
        }
    }

    if (!cancellations.empty()) {
        respondToMessage(
            msg,
            MessagePayload::create<CancelOrdersResponsePayload>(
                cancellations
                    | views::transform([](const Cancellation& c) { return c.id; })
                    | ranges::to<std::vector>(),
                MessagePayload::create<CancelOrdersPayload>(
                    std::move(cancellations), payload->bookId)),
            0);
    }

    if (!failures.empty()) {
        std::vector<OrderID> orderIds = failures
            | views::transform([](const Cancellation& c) { return c.id; })
            | ranges::to<std::vector>();
        auto errorMsg = fmt::format("Order IDs {} do not exist.", fmt::join(orderIds, ", "));
        auto retSubPayload = MessagePayload::create<CancelOrdersErrorResponsePayload>(
            std::move(orderIds),
            payload,
            MessagePayload::create<ErrorResponsePayload>(std::move(errorMsg))
        );
        respondToMessage(msg, "ERROR", retSubPayload);
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalRetrieveL1(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<RetrieveL1Payload>(msg->payload);

    const auto book = m_books[payload->bookId];

    taosim::decimal_t bestAskPrice{};
    taosim::decimal_t bestAskVolume{}, askTotalVolume{};
    taosim::decimal_t bestBidPrice{};
    taosim::decimal_t bestBidVolume{}, bidTotalVolume{};

    auto totalVolumeOnSide = [](const OrderContainer<TickContainer>& side) {
        taosim::decimal_t totalVolume{};
        for (const auto& level : side) {
            totalVolume += level.volume();
        }
        return totalVolume;
    };

    if (!book->sellQueue().empty()) {
        const auto& bestSellLevel = book->sellQueue().front();
        bestAskPrice = bestSellLevel.price();
        bestAskVolume = bestSellLevel.volume();
        askTotalVolume = totalVolumeOnSide(book->sellQueue());
    }

    if (!book->buyQueue().empty()) {
        const auto& bestBuyLevel = book->buyQueue().back();
        bestBidPrice = bestBuyLevel.price();
        bestBidVolume = bestBuyLevel.volume();
        bidTotalVolume = totalVolumeOnSide(book->buyQueue());
    }

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        1,
        name(),
        msg->source,
        "RESPONSE_RETRIEVE_L1",
        MessagePayload::create<RetrieveL1ResponsePayload>(
            simulation()->currentTimestamp(),
            bestAskPrice,
            bestAskVolume,
            askTotalVolume,
            bestBidPrice,
            bestBidVolume,
            bidTotalVolume,
            payload->bookId));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalRetrieveBookAsk(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<RetrieveBookPayload>(msg->payload);

    const auto book = m_books[payload->bookId];

    auto levels = [=] -> std::vector<TickContainer> {
        std::vector<TickContainer> levels;
        const auto actualDepth = std::min(payload->depth, book->sellQueue().size());
        std::copy(
            book->sellQueue().cbegin(),
            book->sellQueue().cbegin() + actualDepth,
            std::back_inserter(levels));
        return levels;
    }();

    respondToMessage(
        msg,
        MessagePayload::create<RetrieveBookResponsePayload>(
            simulation()->currentTimestamp(), std::move(levels)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalRetrieveBookBid(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<RetrieveBookPayload>(msg->payload);

    const auto book = m_books[payload->bookId];

    auto levels = [=] -> std::vector<TickContainer> {
        std::vector<TickContainer> levels;
        const auto actualDepth = std::min(payload->depth, book->buyQueue().size());
        std::copy(
            book->buyQueue().crbegin(),
            book->buyQueue().crbegin() + actualDepth,
            std::back_inserter(levels));
        return levels;
    }();

    respondToMessage(
        msg,
        MessagePayload::create<RetrieveBookResponsePayload>(
            simulation()->currentTimestamp(), std::move(levels)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalMarketOrderSubscription(Message::Ptr msg)
{
    const auto& sub = msg->source;

    if (!m_localMarketOrderSubscribers.add(sub)) {
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<ErrorResponsePayload>(
                fmt::format("Agent {} is already subscribed to market order events", sub)));
    }

    fastRespondToMessage(
        msg,
        MessagePayload::create<SuccessResponsePayload>(
            fmt::format("Agent {} subscribed successfully to market order events", sub)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalLimitOrderSubscription(Message::Ptr msg)
{
    const auto& sub = msg->source;

    if (!m_localLimitOrderSubscribers.add(sub)) {
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<ErrorResponsePayload>(
                fmt::format("Agent {} is already subscribed to limit order events", sub)));
    }

    fastRespondToMessage(
        msg,
        MessagePayload::create<SuccessResponsePayload>(
            fmt::format("Agent {} subscribed successfully to limit order events", sub)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalTradeSubscription(Message::Ptr msg)
{
    const auto& sub = msg->source;

    if (!m_localTradeSubscribers.add(sub)) {
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<ErrorResponsePayload>(
                fmt::format("Agent {} is already subscribed to trade events", sub)));
    }

    fastRespondToMessage(
        msg,
        MessagePayload::create<SuccessResponsePayload>(
            fmt::format("Agent {} subscribed successfully to trade events", sub)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalTradeByOrderSubscription(Message::Ptr msg)
{
    const auto& sub = msg->source;
    auto pptr = std::dynamic_pointer_cast<SubscribeEventTradeByOrderPayload>(msg->payload);
    const auto orderId = pptr->id;

    if (!m_localTradeByOrderSubscribers[orderId].add(sub)) {
        return fastRespondToMessage(
            msg,
            "ERROR",
            MessagePayload::create<ErrorResponsePayload>(fmt::format(
                "Agent {} is already subscribed to trade events for order {}", sub, orderId)));
    }

    fastRespondToMessage(
        msg,
        MessagePayload::create<SuccessResponsePayload>(fmt::format(
            "Agent {} subscribed successfully to trade events for order {}", sub, orderId)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::handleLocalUnknownMessage(Message::Ptr msg)
{
    fastRespondToMessage(
        msg,
        "ERROR",
        MessagePayload::create<ErrorResponsePayload>(fmt::format(
            "Unknown message type: {}",
            msg->type)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::notifyMarketOrderSubscribers(MarketOrder::Ptr marketOrder)
{
    const Timestamp now = simulation()->currentTimestamp();

    for (const auto& sub : m_localMarketOrderSubscribers) {
        simulation()->dispatchMessage(
            now,
            1,
            name(),
            sub,
            "EVENT_ORDER_MARKET",
            MessagePayload::create<EventOrderMarketPayload>(*marketOrder));
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::notifyLimitOrderSubscribers(LimitOrder::Ptr limitOrder)
{
    const Timestamp now = simulation()->currentTimestamp();

    for (const auto& sub : m_localLimitOrderSubscribers) {
        simulation()->dispatchMessage(
            now,
            1,
            name(),
            sub,
            "EVENT_ORDER_LIMIT",
            MessagePayload::create<EventOrderLimitPayload>(*limitOrder));
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::notifyTradeSubscribers(TradeWithLogContext::Ptr tradeWithCtx)
{
    const Timestamp now = simulation()->currentTimestamp();
    // The trade happens exactly on the receipt of the aggressing order, no processing
    // delay there; the processing delay only kicks in sending out a response and events
    // related to the matching.
    tradeWithCtx->trade->setTimestamp(now);

    for (const auto& sub : m_localTradeSubscribers) {
        simulation()->dispatchMessage(
            now,
            Timestamp{},
            name(),
            sub,
            "EVENT_TRADE",
            MessagePayload::create<EventTradePayload>(
                *(tradeWithCtx->trade),
                *(tradeWithCtx->logContext),
                tradeWithCtx->logContext->bookId));
    }

    notifyTradeSubscribersByOrderID(tradeWithCtx, tradeWithCtx->trade->aggressingOrderID());
    notifyTradeSubscribersByOrderID(tradeWithCtx, tradeWithCtx->trade->restingOrderID());
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::notifyTradeSubscribersByOrderID(
    TradeWithLogContext::Ptr tradeWithCtx, OrderID orderId)
{
    auto it = m_localTradeByOrderSubscribers.find(orderId);
    if (it == m_localTradeByOrderSubscribers.end()) {
        return;
    }
    const auto& subs = it->second;

    const Timestamp now = simulation()->currentTimestamp();

    for (const auto& sub : subs) {
        simulation()->dispatchMessage(
            now,
            1,
            name(),
            sub,
            "EVENT_TRADE",
            MessagePayload::create<EventTradePayload>(
                *(tradeWithCtx->trade),
                *(tradeWithCtx->logContext),
                tradeWithCtx->logContext->bookId));
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::orderCallback(Order::Ptr order, OrderContext ctx)
{
    accounts()[ctx.agentId].activeOrders()[ctx.bookId].insert(order);
    m_L3Record[ctx.bookId].push(std::make_shared<OrderEvent>(order, ctx));
    m_signals[ctx.bookId]->orderLog(OrderWithLogContext(
        order, std::make_shared<OrderLogContext>(ctx.agentId, ctx.bookId)));
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::tradeCallback(Trade::Ptr trade, BookId bookId)
{
    const auto restingOrderId = trade->restingOrderID();
    const auto aggressingOrderId = trade->aggressingOrderID();

    const auto [restingAgentId, restingClientOrderId] =
        m_books[bookId]->orderClientContext(restingOrderId);
    const auto [aggressingAgentId, aggressingClientOrderId] =
        m_books[bookId]->orderClientContext(aggressingOrderId);

    const auto fees = m_clearingManager->handleTrade(taosim::exchange::TradeDesc{
        .bookId = bookId,
        .restingAgentId = restingAgentId,
        .aggressingAgentId = aggressingAgentId,
        .trade = trade
    });

    m_L3Record[bookId].push(
        std::make_shared<TradeEvent>(
            trade, TradeContext(bookId, aggressingAgentId, restingAgentId, fees)));

    auto tradeWithCtx = std::make_shared<TradeWithLogContext>(
        trade,
        std::make_shared<TradeLogContext>(aggressingAgentId, restingAgentId, bookId, fees));

    const Timestamp now = simulation()->currentTimestamp();
    const std::array<std::pair<AgentId, std::optional<ClientOrderID>>, 2> idPairs{
        std::pair{restingAgentId, restingClientOrderId},
        std::pair{aggressingAgentId, aggressingClientOrderId}};
    for (const auto [agentId, clientOrderId] : idPairs) {
        const bool isLocalAgent = agentId < AgentId{};
        if (isLocalAgent)
            continue;
        simulation()->dispatchMessage(
            now,
            Timestamp{},
            name(),
            "DISTRIBUTED_PROXY_AGENT",
            "EVENT_TRADE",
            MessagePayload::create<DistributedAgentResponsePayload>(
                agentId,
                MessagePayload::create<EventTradePayload>(
                    *trade, *tradeWithCtx->logContext, bookId, clientOrderId)));
    }

    m_signals[bookId]->tradeLog(*tradeWithCtx);

    notifyTradeSubscribers(tradeWithCtx);
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::unregisterLimitOrderCallback(LimitOrder::Ptr limitOrder, BookId bookId)
{
    const OrderID orderId = limitOrder->id();
    const AgentId agentId = m_books[bookId]->orderClientContext(orderId).agentId;

    taosim::accounting::Balances& balances = accounts().at(agentId).at(bookId);
    const auto freed = [&] -> taosim::accounting::ReservationAmounts {
        if (balances.canFree(orderId, limitOrder->direction())){
            if (limitOrder->direction() == OrderDirection::BUY) {
                simulation()->logDebug("FREEING RESERVATION OF {} BASE + {} QUOTE for BUY order #{}", 
                    balances.base.getReservation(orderId).value_or(0_dec), balances.quote.getReservation(orderId).value_or(0_dec), orderId);
                return balances.freeReservation(orderId, limitOrder->price(),
                    m_books[bookId]->bestBid(), m_books[bookId]->bestAsk(), limitOrder->direction());
            } else {
                simulation()->logDebug("FREEING RESERVATION OF {} BASE + {} QUOTE for SELL order #{}", 
                    balances.base.getReservation(orderId).value_or(0_dec), balances.quote.getReservation(orderId).value_or(0_dec), orderId);
                return balances.freeReservation(orderId, limitOrder->price(),
                    m_books[bookId]->bestBid(), m_books[bookId]->bestAsk(), limitOrder->direction());
            }
        }
        return {};
    }();

    accounts()[agentId].activeOrders()[bookId].erase(limitOrder);

    if (limitOrder->volume() > 0_dec) {
        simulation()->logDebug(
            "{} | AGENT #{} BOOK {} : UNREGISTERED {} ORDER #{} ({}@{}) (FREED {} BASE + {} QUOTE) | RESERVED_QUOTE={} | RESERVED_BASE={}",
            simulation()->currentTimestamp(),
            agentId,
            bookId,
            limitOrder->direction() == OrderDirection::BUY ? "BUY" : "SELL",
            orderId,
            limitOrder->leverage() > 0_dec ? fmt::format("{}x{}",1_dec + limitOrder->leverage(),limitOrder->volume()) : fmt::format("{}",limitOrder->volume()),
            limitOrder->price(),
            freed.base, freed.quote,
            balances.quote.getReserved(),
            balances.base.getReserved());
    }

    if (balances.quote.getReserved() < 0_dec) {
        throw std::runtime_error(fmt::format(
            "{} | AGENT #{} BOOK {} | {}: Reserved quote balance {} < 0 after unregistering order #{}", 
            simulation()->currentTimestamp(),
            agentId,
            bookId, std::source_location::current().function_name(),
            balances.quote.getReserved(), agentId, orderId));
    }
    if (accounts()[agentId].activeOrders()[bookId].empty()) {
        if (balances.quote.getReserved() > 0_dec) {
            
            throw std::runtime_error(fmt::format(
                "{} | AGENT #{} BOOK {} | {}: Reserved quote balance {} > 0 with no active orders after unregistering order #{}", 
                simulation()->currentTimestamp(),
                agentId,
                bookId, std::source_location::current().function_name(),
                balances.quote.getReserved(), orderId));
        }
        if (balances.base.getReserved() > 0_dec) {

            throw std::runtime_error(fmt::format(
                "{} | AGENT #{} BOOK {} | {}: Reserved base balance {} > 0 with no active orders after unregistering order #{}", 
                simulation()->currentTimestamp(),
                agentId,
                bookId, std::source_location::current().function_name(),
                balances.base.getReserved(), orderId));
        }
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::marketOrderProcessedCallback(
    MarketOrder::Ptr marketOrder, OrderContext ctx)
{
    accounts()[ctx.agentId].activeOrders()[ctx.bookId].erase(marketOrder);

    taosim::accounting::Balances& balances = accounts()[ctx.agentId][ctx.bookId];
    if (marketOrder->direction() == OrderDirection::BUY){
        std::optional<taosim::decimal_t> res = balances.quote.getReservation(marketOrder->id());
        if (res.has_value()){
            balances.freeReservation(marketOrder->id(), m_books[ctx.bookId]->bestAsk(),
                m_books[ctx.bookId]->bestBid(), m_books[ctx.bookId]->bestAsk(), marketOrder->direction());
        }
    }else{
        std::optional<taosim::decimal_t> res = balances.base.getReservation(marketOrder->id());
        if (res.has_value()){
            balances.freeReservation(marketOrder->id(), m_books[ctx.bookId]->bestBid(),
                m_books[ctx.bookId]->bestBid(), m_books[ctx.bookId]->bestAsk(), marketOrder->direction());
        }
    }
}

//-------------------------------------------------------------------------

void MultiBookExchangeAgent::timeProgressCallback(Timespan timespan)
{
    if (m_parallel) {
        // simulation()->logDebug(
        //     "{}: TIME {} {}", simulation()->currentTimestamp(), timespan.begin, timespan.end);
        std::vector<std::future<void>> futures;
        for (MessageQueue& queue : m_parallelQueues) {
            futures.push_back(std::async(
                std::launch::async,
                [this](MessageQueue* queue) {
                    while (!queue->empty()) {
                        Message::Ptr msg = queue->top();
                        queue->pop();
                        // simulation()->logDebug(
                        //     "{}: PROCS {} {}",
                        //     simulation()->currentTimestamp(),
                        //     msg->arrival,
                        //     taosim::json::jsonSerializable2str(msg));
                        if (msg->type.starts_with("DISTRIBUTED")) {
                            handleDistributedMessage(msg);
                        } else {
                            handleLocalMessage(msg);
                        }
                    }
                },
                &queue));
        }
        for (auto& future : futures) {
            future.get();
        }
    }
}

//-------------------------------------------------------------------------
