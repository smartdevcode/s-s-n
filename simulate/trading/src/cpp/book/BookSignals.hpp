/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Order.hpp"
#include "Trade.hpp"
#include "common.hpp"

#include "rapidjson/document.h"

//-------------------------------------------------------------------------

struct BookSignals
{
    bs2::signal<void(Order::Ptr, OrderContext)> orderCreated;
    bs2::signal<void(LimitOrder::Ptr, OrderContext)> limitOrderProcessed;
    bs2::signal<void(MarketOrder::Ptr, OrderContext)> marketOrderProcessed;
    bs2::signal<void(Trade::Ptr, BookId)> trade;
    bs2::signal<void(OrderID, taosim::decimal_t)> cancel;
    bs2::signal<void(LimitOrder::Ptr, taosim::decimal_t, BookId)> cancelOrderDetails;
    bs2::signal<void(LimitOrder::Ptr, BookId)> unregister;
    bs2::signal<void(const rapidjson::Value&)> L2;
};

//-------------------------------------------------------------------------