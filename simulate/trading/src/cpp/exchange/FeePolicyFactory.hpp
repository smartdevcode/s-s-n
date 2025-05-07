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

class FeePolicyFactory
{
public:
    [[nodiscard]] static std::unique_ptr<FeePolicy> createFromXML(pugi::xml_node node);

private:
    FeePolicyFactory() noexcept = default;
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
