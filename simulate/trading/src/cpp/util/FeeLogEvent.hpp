/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "common.hpp"

//-------------------------------------------------------------------------

namespace taosim
{

//-------------------------------------------------------------------------

struct FeeLogEvent
{
    AgentId agentId;
    decimal_t fee;
    decimal_t feeRate;
    bool isMaker;
    decimal_t price;
    decimal_t volume;
};

//-------------------------------------------------------------------------

}  // namespace taosim

//-------------------------------------------------------------------------
