/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/event/TradeEvent.hpp"
#include "taosim/serialization/msgpack_util.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct convert<taosim::event::TradeEvent>
{
    const msgpack::object& operator()(const msgpack::object& o, taosim::event::TradeEvent& v) const
    {
        if (o.type != msgpack::type::MAP) {
            throw taosim::serialization::MsgPackError{};
        }

        return o;
    }
};

template<>
struct pack<taosim::event::TradeEvent>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::event::TradeEvent& v) const
    {
        using namespace std::string_literals;

        o.pack_map(12);

        o.pack("y"s);
        o.pack("t"s);

        o.pack("i"s);
        o.pack(v.trade->m_id);

        o.pack("s"s);
        o.pack(std::to_underlying(v.trade->m_direction));

        o.pack("t"s);
        o.pack(v.trade->m_timestamp);
        
        o.pack("q"s);
        o.pack(v.trade->m_volume);

        o.pack("p"s);
        o.pack(v.trade->m_price);
    
        o.pack("Ti"s);
        o.pack(v.trade->m_aggressingOrderID);

        o.pack("Ta"s);
        o.pack(v.ctx.aggressingAgentId);

        o.pack("Tf"s);
        o.pack(v.ctx.fees.taker);

        o.pack("Mi"s);
        o.pack(v.trade->m_restingOrderID);

        o.pack("Ma"s);
        o.pack(v.ctx.restingAgentId);

        o.pack("Mf"s);
        o.pack(v.ctx.fees.maker);

        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------

