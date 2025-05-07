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

struct VIPFeePolicyDesc
{
    double makerFeeRate;
    double takerFeeRate;
    std::pair<AgentId, AgentId> vipAgentIdRange;
};

//-------------------------------------------------------------------------

struct VIPFeePolicy : FeePolicy
{
    decimal_t makerFeeRate;
    decimal_t takerFeeRate;
    std::pair<AgentId, AgentId> vipAgentIdRange;

    explicit VIPFeePolicy(const VIPFeePolicyDesc& desc);

    virtual Fees calculateFees(const TradeDesc& tradeDesc) const override;

    virtual Fees getRates() const noexcept override { return {makerFeeRate, takerFeeRate}; }

    [[nodiscard]] static std::unique_ptr<VIPFeePolicy> fromXML(pugi::xml_node node);
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
