/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Book.hpp"

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class PriceTimeBook : public Book
{
public:
    PriceTimeBook(
        Simulation* simulation,
        BookId id,
        size_t maxDepth,
        size_t detailedDepth);
    
protected:
    virtual taosim::decimal_t processAgainstTheBuyQueue(Order::Ptr order, taosim::decimal_t minPrice) override;
    virtual taosim::decimal_t processAgainstTheSellQueue(Order::Ptr order, taosim::decimal_t maxPrice) override;
    
    virtual TickContainer* preventSelfTrade(TickContainer* queue, LimitOrder::Ptr iop, Order::Ptr order, AgentId agentId);
};

//-------------------------------------------------------------------------
