/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Fees.hpp"
#include "Trade.hpp"
#include "common.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

struct TradeDesc
{
    BookId bookId;
    AgentId restingAgentId;
    AgentId aggressingAgentId;
    Trade::Ptr trade;
};

//-------------------------------------------------------------------------

struct FeePolicy
{
    virtual ~FeePolicy() noexcept = default;

    virtual Fees calculateFees(const TradeDesc& tradeDesc) const = 0;
    [[nodiscard]] virtual Fees getRates() const noexcept = 0;

protected:
    virtual decimal_t checkFeeRate(double feeRate) const;
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
