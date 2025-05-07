/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "FeePolicy.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

struct ZeroFeePolicy : FeePolicy
{
    virtual Fees calculateFees(const TradeDesc& tradeDesc) const override { return {}; }
    virtual Fees getRates() const noexcept override { return {}; }
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
