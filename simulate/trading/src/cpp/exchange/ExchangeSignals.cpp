/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "ExchangeSignals.hpp"

//-------------------------------------------------------------------------

ExchangeSignals::ExchangeSignals() noexcept
{
    orderLog.connect([this](OrderWithLogContext item) {
        L3({ .item = item, .id = eventCounter++ });
    });
    tradeLog.connect([this](TradeWithLogContext item) {
        L3({ .item = item, .id = eventCounter++ });
    });
    cancelLog.connect([this](CancellationWithLogContext item) {
        L3({ .item = item, .id = eventCounter++ });
    });
}

//-------------------------------------------------------------------------