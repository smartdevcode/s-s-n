/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "margin_utils.hpp"

//-------------------------------------------------------------------------

namespace taosim::accounting
{

decimal_t calculateMarginCallPrice(
    decimal_t price,
    decimal_t leverage,
    OrderDirection direction,
    decimal_t maintenanceMargin) noexcept
{
    using namespace util;
    return direction == OrderDirection::BUY
        ? price * leverage / (dec1p(leverage) * dec1m(maintenanceMargin))
        : price * (2_dec + leverage) / (dec1p(leverage) * dec1p(maintenanceMargin));
}

}  // namespace taosim::accounting

//-------------------------------------------------------------------------