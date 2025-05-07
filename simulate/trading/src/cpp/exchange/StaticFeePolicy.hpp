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

struct StaticFeePolicyDesc
{
    double makerFeeRate;
    double takerFeeRate;
};

//-------------------------------------------------------------------------

struct StaticFeePolicy : FeePolicy
{
    decimal_t makerFeeRate;
    decimal_t takerFeeRate;

    explicit StaticFeePolicy(const StaticFeePolicyDesc& desc) noexcept;

    virtual Fees calculateFees(const TradeDesc& tradeDesc) const override;

    virtual Fees getRates() const noexcept override { return {makerFeeRate, takerFeeRate}; }

    [[nodiscard]] static std::unique_ptr<StaticFeePolicy> fromXML(pugi::xml_node node);
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
