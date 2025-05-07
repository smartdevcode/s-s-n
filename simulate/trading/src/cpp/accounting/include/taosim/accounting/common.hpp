/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

//-------------------------------------------------------------------------

namespace taosim::accounting
{

struct RoundParams
{
    uint32_t baseDecimals;
    uint32_t quoteDecimals;
};

}  // namespace taosim::accounting

//-------------------------------------------------------------------------
