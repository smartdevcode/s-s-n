/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Order.hpp"
#include "decimal.hpp"

//-------------------------------------------------------------------------

namespace taosim::accounting
{

[[nodiscard]] decimal_t calculateMarginCallPrice(
    decimal_t price,
    decimal_t leverage,
    OrderDirection direction,
    decimal_t maintenanceMargin) noexcept;

}  // namespace taosim::accounting

//-------------------------------------------------------------------------
