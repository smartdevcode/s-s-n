/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "VIPFeePolicy.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

VIPFeePolicy::VIPFeePolicy(const VIPFeePolicyDesc& desc)
{
    makerFeeRate = checkFeeRate(desc.makerFeeRate);
    takerFeeRate = checkFeeRate(desc.takerFeeRate);

    if (desc.vipAgentIdRange.first >= desc.vipAgentIdRange.second) {
        throw std::invalid_argument{fmt::format(
            "{}: VIP agent ID upper bound ({}) must be greater than lower bound ({})",
            std::source_location::current().function_name(),
            desc.vipAgentIdRange.second, desc.vipAgentIdRange.first)};
    }
    vipAgentIdRange = desc.vipAgentIdRange;
}

//-------------------------------------------------------------------------

Fees VIPFeePolicy::calculateFees(const TradeDesc& tradeDesc) const
{
    [[maybe_unused]] const auto [_1, restingAgentId, aggressingAgentId, trade] = tradeDesc;

    auto isVIP = [this](AgentId agentId) -> bool {
        return vipAgentIdRange.first <= agentId && agentId <= vipAgentIdRange.second;
    };

    switch (trade->direction()) {
        case OrderDirection::BUY:
            return {
                .maker = !isVIP(restingAgentId) * makerFeeRate * trade->volume() * trade->price(),
                .taker = !isVIP(aggressingAgentId) * takerFeeRate * trade->volume() * trade->price()
            };
        case OrderDirection::SELL:
            return {
                .maker = !isVIP(restingAgentId) * makerFeeRate * trade->volume() * trade->price(),
                .taker = !isVIP(aggressingAgentId) * takerFeeRate * trade->volume() * trade->price()
            };
        default:
            std::unreachable();
    }
}

//-------------------------------------------------------------------------

std::unique_ptr<VIPFeePolicy> VIPFeePolicy::fromXML(pugi::xml_node node)
{
    return std::make_unique<VIPFeePolicy>(VIPFeePolicyDesc{
        .makerFeeRate = node.attribute("makerFee").as_double(),
        .takerFeeRate = node.attribute("takerFee").as_double(),
        .vipAgentIdRange = {
            node.attribute("agentIdLowerBound").as_int(),
            node.attribute("agentIdUpperBound").as_int()
        }
    });
}

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
