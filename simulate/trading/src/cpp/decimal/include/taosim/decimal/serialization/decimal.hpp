/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/decimal/decimal.hpp"
#include "taosim/serialization/msgpack_util.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct convert<taosim::decimal_t>
{
    const msgpack::object& operator()(const msgpack::object& o, taosim::decimal_t& v) const
    {
        if (o.type == msgpack::type::FLOAT64) {
            v = taosim::util::double2decimal(o.as<double>());
        }
        else if (o.type == msgpack::type::POSITIVE_INTEGER) {
            v = taosim::util::unpackDecimal(o.as<uint64_t>());
        }
        else {
            throw taosim::serialization::MsgPackError{};
        }
        return o;
    }
};

template<>
struct pack<taosim::decimal_t>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const taosim::decimal_t& v) const
    {
        if constexpr (std::same_as<Stream, taosim::serialization::HumanReadableStream>) {
            o.pack_double(taosim::util::decimal2double(v));
        }
        else if constexpr (std::same_as<Stream, taosim::serialization::BinaryStream>) {
            o.pack_uint64(taosim::util::packDecimal(v));
        }
        else {
            static_assert(false, "Unrecognized Stream type");
        }
        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------
