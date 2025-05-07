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

    virtual taosim::decimal_t calculatCorrespondingVolume(taosim::decimal_t quotePrice) override;
    
protected:
    virtual void processAgainstTheBuyQueue(Order::Ptr order, taosim::decimal_t minPrice) override;
    virtual void processAgainstTheSellQueue(Order::Ptr order, taosim::decimal_t maxPrice) override;
    
};

//-------------------------------------------------------------------------
