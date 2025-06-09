/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

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

}  // namespace taosim

//-------------------------------------------------------------------------