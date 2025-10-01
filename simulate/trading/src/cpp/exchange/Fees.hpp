/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/decimal/decimal.hpp"

#include <msgpack.hpp>

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

struct Fees
{
    decimal_t maker{};
    decimal_t taker{};

    MSGPACK_DEFINE_MAP(maker, taker);
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------