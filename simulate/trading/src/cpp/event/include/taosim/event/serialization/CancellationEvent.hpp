/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/event/CancellationEvent.hpp"
#include "taosim/serialization/msgpack_util.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct convert<taosim::event::CancellationEvent>
{
    const msgpack::object& operator()(
        const msgpack::object& o, taosim::event::CancellationEvent& v) const
    {
        if (o.type != msgpack::type::MAP) {
            throw taosim::serialization::MsgPackError{};
        }

        return o;
    }
};

template<>
struct pack<taosim::event::CancellationEvent>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::event::CancellationEvent& v) const
    {
        using namespace std::string_literals;

        o.pack_map(5);
    
        o.pack("y"s);
        o.pack("c"s);

        o.pack("i"s);
        o.pack(v.cancellation.id);

        o.pack("t"s);
        o.pack(v.timestamp);

        o.pack("p"s);
        o.pack(v.price);

        o.pack("q"s);
        o.pack(v.cancellation.volume);

        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------
