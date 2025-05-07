/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "StaticFeePolicy.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

StaticFeePolicy::StaticFeePolicy(const StaticFeePolicyDesc& desc) noexcept
{
    makerFeeRate = checkFeeRate(desc.makerFeeRate);
    takerFeeRate = checkFeeRate(desc.takerFeeRate);
}

//-------------------------------------------------------------------------

Fees StaticFeePolicy::calculateFees(const TradeDesc& tradeDesc) const
{
    const auto trade = tradeDesc.trade;

    switch (trade->direction()) {
        case OrderDirection::BUY:
            return {
                .maker = makerFeeRate * trade->volume() * trade->price(),
                .taker = takerFeeRate * trade->volume() * trade->price()
            };
        case OrderDirection::SELL:
            return {
                .maker = makerFeeRate * trade->volume() * trade->price(),
                .taker = takerFeeRate * trade->volume() * trade->price()
            };
        default:
            std::unreachable();
    }
}

//-------------------------------------------------------------------------

std::unique_ptr<StaticFeePolicy> StaticFeePolicy::fromXML(pugi::xml_node node)
{
    return std::make_unique<StaticFeePolicy>(StaticFeePolicyDesc{
        .makerFeeRate = node.attribute("makerFee").as_double(),
        .takerFeeRate = node.attribute("takerFee").as_double()
    });
}

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
