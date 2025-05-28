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

enum class LimitOrderFlag : uint32_t
{
    NONE,
    POST_ONLY,
    IOC
};


//-------------------------------------------------------------------------

}  // namespace taosim

//-------------------------------------------------------------------------