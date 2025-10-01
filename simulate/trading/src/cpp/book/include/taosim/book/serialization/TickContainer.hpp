/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/book/TickContainer.hpp"
#include "taosim/serialization/msgpack_util.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct pack<taosim::book::TickContainer>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::book::TickContainer& v) const
    {
        using namespace std::string_literals;

        o.pack_map(3);

        o.pack("p"s);
        o.pack(v.price());
    
        o.pack("q"s);
        o.pack(v.volume());

        o.pack("o"s);
        if (v.empty()) {
            o.pack_nil();
        }
        else {
            o.pack_array(v.size());
            for (const auto order : v) {
                o.pack(order);
            }
        }

        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------
