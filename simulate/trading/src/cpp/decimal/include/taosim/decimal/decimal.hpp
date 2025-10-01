/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <bdldfp_decimal.h>
#include <bdldfp_decimalconvertutil.h>
#include <bdldfp_decimalutil.h>
#include <fmt/format.h>

#include <bit>
#include <spanstream>

//-------------------------------------------------------------------------

#define DEC(lit) BDLDFP_DECIMAL_DD(lit)

//-------------------------------------------------------------------------

namespace taosim
{

using decimal_t = BloombergLP::bdldfp::Decimal64;

}  // namespace taosim

//-------------------------------------------------------------------------

namespace taosim::util
{

inline constexpr uint32_t kDefaultDecimalPlaces = 8;

[[nodiscard]] inline decimal_t round(
    decimal_t val, uint32_t decimalPlaces = kDefaultDecimalPlaces)
{
    return BloombergLP::bdldfp::DecimalUtil::trunc(val, decimalPlaces);
}

[[nodiscard]] inline decimal_t roundUp(decimal_t val, uint32_t decimalPlaces)
{
    using namespace BloombergLP::bdldfp;
    const auto factor = DecimalUtil::multiplyByPowerOf10(decimal_t{1}, decimalPlaces);
    return DecimalUtil::ceil(val * factor) / factor;
}

[[nodiscard]] inline double decimal2double(decimal_t val)
{
    return BloombergLP::bdldfp::DecimalConvertUtil::decimalToDouble(val);
}

[[nodiscard]] inline decimal_t double2decimal(
    double val, uint32_t decimalPlaces = kDefaultDecimalPlaces)
{
    return round(decimal_t{val}, decimalPlaces);
}

[[nodiscard]] inline uint64_t packDecimal(decimal_t val)
{
    uint64_t packed;
    BloombergLP::bdldfp::DecimalConvertUtil::decimalToDPD(
        std::bit_cast<uint8_t*>(&packed), val);
    return packed;
}

[[nodiscard]] inline decimal_t unpackDecimal(uint64_t val)
{
    decimal_t unpacked;
    BloombergLP::bdldfp::DecimalConvertUtil::decimalFromDPD(
        &unpacked, std::bit_cast<uint8_t*>(&val));
    return unpacked;
}

[[nodiscard]] inline decimal_t fma(decimal_t a, decimal_t b, decimal_t c) noexcept
{
    return BloombergLP::bdldfp::DecimalUtil::fma(a, b, c);
}

[[nodiscard]] inline decimal_t pow(decimal_t a, decimal_t b)
{
    return BloombergLP::bdldfp::DecimalUtil::pow(a, b);
}

[[nodiscard]] inline decimal_t dec1p(decimal_t val) noexcept
{
    return 1 + val;
}

[[nodiscard]] inline decimal_t dec1m(decimal_t val) noexcept
{
    return 1 - val;
}

[[nodiscard]] inline decimal_t decInv1p(decimal_t val) noexcept
{
    return 1 / dec1p(val);
}

[[nodiscard]] inline decimal_t abs(decimal_t val) noexcept
{
    return val < decimal_t{} ? -val : val;
}

}  // namespace taosim::util

//-------------------------------------------------------------------------

namespace taosim::literals
{

[[nodiscard]] constexpr decimal_t operator"" _dec(unsigned long long int val)
{
    return decimal_t{val};
}

}  // namespace taosim::literals

//-------------------------------------------------------------------------

template<>
struct fmt::formatter<taosim::decimal_t>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(taosim::decimal_t val, FormatContext& ctx) const
    {
        using namespace taosim::literals;
        char buf[32]{};
        std::ospanstream oss{buf};
        if (val == 0_dec) [[unlikely]] {
            oss << "0.0";
        } else {
            oss << val;
        }
        return fmt::format_to(ctx.out(), "{}", buf);
    }
};

//-------------------------------------------------------------------------
