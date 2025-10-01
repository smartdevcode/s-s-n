/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/accounting/Balance.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct pack<taosim::accounting::Balance>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::accounting::Balance& v) const
    {
        using namespace std::string_literals;

        o.pack_map(6);

        o.pack("initial"s);
        o.pack(v.getInitial());

        o.pack("free"s);
        o.pack(v.getFree());

        o.pack("reserved"s);
        o.pack(v.getReserved());

        o.pack("total"s);
        o.pack(v.getTotal());
    
        o.pack("symbol"s);
        if (v.getSymbol().empty()) [[likely]] {
            o.pack_nil();
        } else [[unlikely]] {
            o.pack(v.getSymbol());
        }
    
        o.pack("roundingDecimals"s);
        o.pack(v.getRoundingDecimals());
    
        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------
