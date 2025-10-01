/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/event/OrderEvent.hpp"
#include "taosim/serialization/msgpack_util.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct convert<taosim::event::OrderEvent>
{
    const msgpack::object& operator()(
        const msgpack::object& o, taosim::event::OrderEvent& v) const
    {
        if (o.type != msgpack::type::MAP) {
            throw taosim::serialization::MsgPackError{};
        }

        return o;
    }
};

template<>
struct pack<taosim::event::OrderEvent>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::event::OrderEvent& v) const
    {
        using namespace std::string_literals;

        if constexpr (std::same_as<Stream, taosim::serialization::HumanReadableStream>) {
            o.pack_map(8);

            o.pack("y"s);
            o.pack("o"s);

            o.pack("i"s);
            o.pack(v.id);

            o.pack("c"s);
            o.pack(v.ctx.clientOrderId);

            o.pack("t"s);
            o.pack(v.timestamp);

            o.pack("q"s);
            o.pack(v.volume);

            o.pack("s"s);
            o.pack(std::to_underlying(v.direction));

            o.pack("p"s);
            o.pack(v.price);

            o.pack("l"s);
            o.pack(v.leverage);
        }
    
        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------