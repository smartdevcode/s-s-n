/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "L3LogEvent.hpp"
#include "FeeLogEvent.hpp"

//-------------------------------------------------------------------------

struct ExchangeSignals
{
    bs2::signal<void(OrderWithLogContext)> orderLog;
    bs2::signal<void(TradeWithLogContext)> tradeLog;
    bs2::signal<void(CancellationWithLogContext)> cancelLog;
    bs2::signal<void(taosim::L3LogEvent)> L3;
    bs2::signal<void(taosim::FeeLogEvent)> feeLog;
    uint32_t eventCounter{};

    ExchangeSignals() noexcept;
};

//-------------------------------------------------------------------------
