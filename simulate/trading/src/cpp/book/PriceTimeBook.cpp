/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "PriceTimeBook.hpp"

#include "Simulation.hpp"


//-------------------------------------------------------------------------

PriceTimeBook::PriceTimeBook(
    Simulation* simulation,
    BookId id,
    size_t maxDepth,
    size_t detailedDepth)
    : Book{simulation, id, maxDepth, detailedDepth}
{}

//-------------------------------------------------------------------------

taosim::decimal_t PriceTimeBook::processAgainstTheBuyQueue(Order::Ptr order, taosim::decimal_t minPrice)
{
    taosim::decimal_t processedQuote = {};
    const auto volumeDecimals = m_simulation->exchange()->config().parameters().volumeIncrementDecimals;
    const auto priceDecimals = m_simulation->exchange()->config().parameters().priceIncrementDecimals;
    const auto agentId = m_order2clientCtx[order->id()].agentId;

    auto bestBuyDeque = &m_buyQueue.back();

    order->setVolume(taosim::util::round(order->volume(), volumeDecimals));
    order->setLeverage(taosim::util::round(order->leverage(), volumeDecimals));

    while (order->volume() > 0_dec && bestBuyDeque->price() >= minPrice) {
        LimitOrder::Ptr iop = bestBuyDeque->front();
        const auto iopAgentId = m_order2clientCtx[iop->id()].agentId;
        if (agentId == iopAgentId && order->stpFlag() != STPFlag::NONE){
            bestBuyDeque = preventSelfTrade(bestBuyDeque, iop, order, agentId);
            if (bestBuyDeque == nullptr)
                break;
            continue;
        }

        const taosim::decimal_t usedVolume = std::min(iop->totalVolume(), order->totalVolume());
        
        OrderClientContext aggCtx, restCtx;
        if (m_simulation->debug()) {
            aggCtx = m_order2clientCtx[order->id()];
            const auto& aggBalances = m_simulation->exchange()->accounts()[aggCtx.agentId][m_id];        
            restCtx = m_order2clientCtx[iop->id()];
            const auto& restingBalances = m_simulation->exchange()->accounts()[restCtx.agentId][m_id];    
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), restCtx.agentId, m_id, restingBalances.quote, restingBalances.base);
        }

        if (usedVolume > 0_dec) {
            processedQuote += usedVolume * bestBuyDeque->price();
            logTrade(OrderDirection::SELL, order->id(), iop->id(), usedVolume, bestBuyDeque->price());
        }

        order->removeLeveragedVolume(usedVolume);
        iop->removeLeveragedVolume(usedVolume);

        order->setVolume(taosim::util::round(order->volume(), volumeDecimals));
        iop->setVolume(taosim::util::round(iop->volume(), volumeDecimals));

        bestBuyDeque->updateVolume(-taosim::util::round(usedVolume, volumeDecimals));

        if (taosim::util::round(iop->totalVolume(), volumeDecimals) == 0_dec) {
            bestBuyDeque->pop_front();
            unregisterLimitOrder(iop);
            m_simulation->logDebug("BOOK {} : UNREGISTERING ORDER #{}", m_id, iop->id());
        }

        if (m_simulation->debug()) {
            const auto& aggBalances = m_simulation->exchange()->accounts()[aggCtx.agentId][m_id];
            const auto& restingBalances = m_simulation->exchange()->accounts()[restCtx.agentId][m_id];    
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), aggCtx.agentId, m_id, aggBalances.quote, aggBalances.base);
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), restCtx.agentId, m_id, restingBalances.quote, restingBalances.base);
        }

        if (bestBuyDeque->empty()) {
            m_buyQueue.pop_back();
            if (m_buyQueue.empty()) {
                break;
            }
            bestBuyDeque = &m_buyQueue.back();
        }
    }
    return processedQuote;
}

//-------------------------------------------------------------------------

taosim::decimal_t PriceTimeBook::processAgainstTheSellQueue(Order::Ptr order, taosim::decimal_t maxPrice)
{
    taosim::decimal_t processedQuote = {};
    const auto volumeDecimals = m_simulation->exchange()->config().parameters().volumeIncrementDecimals;
    const auto priceDecimals = m_simulation->exchange()->config().parameters().priceIncrementDecimals;
    const auto agentId = m_order2clientCtx[order->id()].agentId;

    auto bestSellDeque = &m_sellQueue.front();

    order->setVolume(taosim::util::round(order->volume(), volumeDecimals));
    order->setLeverage(taosim::util::round(order->leverage(), volumeDecimals));
    
    while ( order->volume() > 0_dec && bestSellDeque->price() <= maxPrice) {
        LimitOrder::Ptr iop = bestSellDeque->front();
        const auto iopAgentId = m_order2clientCtx[iop->id()].agentId;
        if (agentId == iopAgentId && order->stpFlag() != STPFlag::NONE){
            bestSellDeque = preventSelfTrade(bestSellDeque, iop, order, agentId);
            if (bestSellDeque == nullptr)
                break;
            continue;
        }

        const taosim::decimal_t usedVolume = std::min(iop->totalVolume(), order->totalVolume());

        OrderClientContext aggCtx, restCtx;
        if (m_simulation->debug()) {
            aggCtx = m_order2clientCtx[order->id()];
            const auto& aggBalances = m_simulation->exchange()->accounts()[aggCtx.agentId][m_id];        
            restCtx = m_order2clientCtx[iop->id()];
            const auto& restingBalances = m_simulation->exchange()->accounts()[restCtx.agentId][m_id];    
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), restCtx.agentId, m_id, restingBalances.quote, restingBalances.base);
        }

        if (usedVolume > 0_dec) {
            processedQuote += usedVolume * bestSellDeque->price();
            logTrade(OrderDirection::BUY, order->id(), iop->id(), usedVolume, bestSellDeque->price());
        }

        order->removeLeveragedVolume(usedVolume);
        iop->removeLeveragedVolume(usedVolume);

        order->setVolume(taosim::util::round(order->volume(), volumeDecimals));
        iop->setVolume(taosim::util::round(iop->volume(), volumeDecimals));

        bestSellDeque->updateVolume(-taosim::util::round(usedVolume, volumeDecimals));

        if (taosim::util::round(iop->totalVolume(), volumeDecimals) == 0_dec) {
            bestSellDeque->pop_front();
            unregisterLimitOrder(iop);
            m_simulation->logDebug("BOOK {} : UNREGISTERING ORDER #{}", m_id, iop->id());
        }

        if (m_simulation->debug()) {
            const auto& aggBalances = m_simulation->exchange()->accounts()[aggCtx.agentId][m_id];
            const auto& restingBalances = m_simulation->exchange()->accounts()[restCtx.agentId][m_id];    
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), aggCtx.agentId, m_id, aggBalances.quote, aggBalances.base);
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), restCtx.agentId, m_id, restingBalances.quote, restingBalances.base);
        }

        if (bestSellDeque->empty()) {
            m_sellQueue.pop_front();
            if (m_sellQueue.empty()) {
                break;
            }
            bestSellDeque = &m_sellQueue.front();
        }
    }
    return processedQuote;
}


//-------------------------------------------------------------------------

taosim::book::TickContainer* PriceTimeBook::preventSelfTrade(
    taosim::book::TickContainer* queue, LimitOrder::Ptr iop, Order::Ptr order, AgentId agentId)
{
    auto stpFlag = order->stpFlag();
    auto now = m_simulation->currentTimestamp();

    auto cancelAndLog = [&](OrderID orderId, std::optional<taosim::decimal_t> volume = {}) {
        if (cancelOrderOpt(orderId, volume)) {
            taosim::event::Cancellation cancellation{orderId, volume};
            m_simulation->exchange()->signals(m_id)->cancelLog(CancellationWithLogContext(
                cancellation,
                std::make_shared<CancellationLogContext>(
                    agentId,
                    m_id,
                    now)));
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : SELF TRADE PREVENTION CANCELED {}ORDER {}", 
                now, agentId, m_id, 
                volume.has_value() ? fmt::format("{} volume of ", volume.value()) : "",
                orderId);
            return true;
        } else {
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : SELF TRADE PREVENTION OF ORDER {} FAILED", now, agentId, m_id, orderId);
            return false;
        }
    };

    if (stpFlag == STPFlag::CN || stpFlag == STPFlag::CB) {
        order->removeVolume(order->volume());
        m_simulation->logDebug("{} | AGENT #{} BOOK {} : SELF TRADE PREVENTION CANCELED ORDER {}", now, agentId, m_id, order->id());
        if (stpFlag == STPFlag::CN)
            return nullptr;
    }

    if (stpFlag == STPFlag::CO || stpFlag == STPFlag::CB) {
        auto volumeToCancel = iop->totalVolume();
        const auto ticksOnLevelBeforeCancel = queue->size();
        if (cancelAndLog(iop->id())) {
            if (ticksOnLevelBeforeCancel == 1) {
                bool isBuy = iop->direction() == OrderDirection::BUY;
                if ((isBuy && m_buyQueue.empty()) || (!isBuy && m_sellQueue.empty()))
                    return nullptr;
                queue = isBuy ? &m_buyQueue.back() : &m_sellQueue.front();
            }
        }
        if (stpFlag == STPFlag::CB)
            return nullptr;
        return queue;
    }

    if(stpFlag == STPFlag::DC){
        if (iop->totalVolume() == order->totalVolume()){
            order->removeVolume(order->volume());
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : SELF TRADE PREVENTION CANCELED ORDER {}", now, agentId, m_id, order->id());
            cancelAndLog(iop->id());
            return nullptr;
        } else if (iop->totalVolume() < order->totalVolume()){
            auto volumeToCancel = taosim::util::round(iop->totalVolume() / taosim::util::dec1p(order->leverage()),
                m_simulation->exchange()->config().parameters().volumeIncrementDecimals);
            const auto ticksOnLevelBeforeCancel = queue->size();
            if (cancelAndLog(iop->id())){
                if (ticksOnLevelBeforeCancel == 1) {
                    bool isBuy = iop->direction() == OrderDirection::BUY;
                    if ((isBuy && m_buyQueue.empty()) || (!isBuy && m_sellQueue.empty()))
                        return nullptr;
                    queue = isBuy ? &m_buyQueue.back() : &m_sellQueue.front();
                }
                order->removeVolume(volumeToCancel);
                return queue;
            }
        } else {
            auto volumeToCancel = taosim::util::round(order->totalVolume() / taosim::util::dec1p(iop->leverage()),
                m_simulation->exchange()->config().parameters().volumeIncrementDecimals);
            order->removeVolume(order->volume());
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : SELF TRADE PREVENTION CANCELED ORDER {}", now, agentId, m_id, order->id());
            cancelAndLog(iop->id(), volumeToCancel);
            return nullptr;
        }
    }

    return queue;

}

//-------------------------------------------------------------------------
