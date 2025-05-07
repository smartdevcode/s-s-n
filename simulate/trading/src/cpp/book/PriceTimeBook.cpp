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

void PriceTimeBook::processAgainstTheBuyQueue(Order::Ptr order, taosim::decimal_t minPrice)
{
    auto bestBuyDeque = &m_buyQueue.back();
    order->setVolume(taosim::util::round(order->volume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));
    order->setLeverage(taosim::util::round(order->leverage(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));

    while (order->volume() > 0_dec && bestBuyDeque->price() >= minPrice) {
        LimitOrder::Ptr iop = bestBuyDeque->front();
        iop->setPrice(taosim::util::round(iop->price(), m_simulation->exchange()->config().parameters().priceIncrementDecimals));

        iop->setLeverage(taosim::util::round(iop->leverage(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));
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
            logTrade(OrderDirection::SELL, order->id(), iop->id(), usedVolume, bestBuyDeque->price());
        }

        order->removeLeveragedVolume(usedVolume);
        iop->removeLeveragedVolume(usedVolume);

        order->setVolume(taosim::util::round(order->volume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));
        iop->setVolume(taosim::util::round(iop->volume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));

        if (taosim::util::round(iop->totalVolume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals) == 0_dec) {
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
}

//-------------------------------------------------------------------------

void PriceTimeBook::processAgainstTheSellQueue(Order::Ptr order, taosim::decimal_t maxPrice)
{
    
    auto bestSellDeque = &m_sellQueue.front();

    order->setVolume(taosim::util::round(order->volume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));
    order->setLeverage(taosim::util::round(order->leverage(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));
    
    while ( order->volume() > 0_dec && bestSellDeque->price() <= maxPrice) {
        LimitOrder::Ptr iop = bestSellDeque->front();
        iop->setPrice(taosim::util::round(iop->price(), m_simulation->exchange()->config().parameters().priceIncrementDecimals));
        iop->setLeverage(taosim::util::round(iop->leverage(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));
        const taosim::decimal_t usedVolume = std::min(iop->totalVolume(), order->totalVolume());

        OrderClientContext aggCtx, restCtx;
        if (m_simulation->debug()) {
            aggCtx = m_order2clientCtx[order->id()];
            const auto& aggBalances = m_simulation->exchange()->accounts()[aggCtx.agentId][m_id];        
            restCtx = m_order2clientCtx[iop->id()];
            const auto& restingBalances = m_simulation->exchange()->accounts()[restCtx.agentId][m_id];    
            // m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), aggCtx.agentId, m_id, aggBalances.quote, aggBalances.base);
            m_simulation->logDebug("{} | AGENT #{} BOOK {} : QUOTE : {}  BASE : {}", m_simulation->currentTimestamp(), restCtx.agentId, m_id, restingBalances.quote, restingBalances.base);
        }

        if (usedVolume > 0_dec) {
            logTrade(OrderDirection::BUY, order->id(), iop->id(), usedVolume, bestSellDeque->price());
        }

        order->removeLeveragedVolume(usedVolume);
        iop->removeLeveragedVolume(usedVolume);

        order->setVolume(taosim::util::round(order->volume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));
        iop->setVolume(taosim::util::round(iop->volume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals));

        if (taosim::util::round(iop->totalVolume(), m_simulation->exchange()->config().parameters().volumeIncrementDecimals) == 0_dec) {
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
}

//-------------------------------------------------------------------------

taosim::decimal_t PriceTimeBook::calculatCorrespondingVolume(taosim::decimal_t quotePrice)
{
    taosim::decimal_t volume = {};
    
    for (auto buyDequeIt = m_buyQueue.begin(); buyDequeIt != m_buyQueue.end(); ++buyDequeIt) {

        if (quotePrice - buyDequeIt->price() * buyDequeIt->totalVolume() > 0_dec){
            volume += buyDequeIt->totalVolume();
            quotePrice -= buyDequeIt->price() * buyDequeIt->totalVolume();
        }else{
            volume += taosim::util::round(quotePrice / buyDequeIt->price(), 
                m_simulation->exchange()->config().parameters().volumeIncrementDecimals);
            break;
        }
    }

    return volume;
}

