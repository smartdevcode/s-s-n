/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "FeePolicyFactory.hpp"

#include "ZeroFeePolicy.hpp"
#include "StaticFeePolicy.hpp"
#include "VIPFeePolicy.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

std::unique_ptr<FeePolicy> FeePolicyFactory::createFromXML(pugi::xml_node node)
{
    std::string_view policyType = node.attribute("type").as_string();

    if (policyType == "static") {
        return StaticFeePolicy::fromXML(node);
    } else if (policyType == "vip") {
        return VIPFeePolicy::fromXML(node);
    }

    return std::make_unique<ZeroFeePolicy>();
}

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
