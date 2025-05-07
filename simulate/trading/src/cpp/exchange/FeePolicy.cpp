/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "FeePolicy.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

decimal_t FeePolicy::checkFeeRate(double feeRate) const
{
    static constexpr double feeRateMin{-1.0}, feeRateMax{1.0};
    if (!(feeRateMin < feeRate && feeRate < feeRateMax)) {
        throw std::invalid_argument{fmt::format(
            "{}: Fee should be between {} and {}; was {}",
            std::source_location::current().function_name(),
            feeRateMin, feeRateMax, feeRate)};
    }
    return decimal_t{feeRate};
}

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
