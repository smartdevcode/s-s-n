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

enum class STPFlag : uint32_t
{
    NONE,
    CO, // Cancel the resting
    CN, // Cancel the aggressing
    CB, // Cancel both
    DC  // Decrement and Cancel
};

//-------------------------------------------------------------------------

enum class TimeInForce : uint32_t
{
    GTC,
    GTT,
    IOC,
    FOK
};

//-------------------------------------------------------------------------

enum class SettleType: int32_t 
{
    NONE = -2,
    FIFO = -1
};

using SettleFlag = std::variant<SettleType, OrderID>;

//-------------------------------------------------------------------------



}  // namespace taosim

//-------------------------------------------------------------------------