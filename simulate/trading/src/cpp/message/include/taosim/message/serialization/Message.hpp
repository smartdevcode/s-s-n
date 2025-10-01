/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/message/ExchangeAgentMessagePayloads.hpp"
#include "taosim/message/Message.hpp"
#include "taosim/message/MessagePayload.hpp"
#include "taosim/message/PayloadFactory.hpp"
#include "taosim/message/serialization/DistributedAgentResponsePayload.hpp"
#include "taosim/serialization/msgpack_util.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct convert<Message>
{
    const msgpack::object& operator()(const msgpack::object& o, Message& v)
    {
        if (o.type != msgpack::type::MAP) {
            throw taosim::serialization::MsgPackError{};
        }

        return o;
    }
};

template<>
struct pack<Message>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const Message& v) const
    {
        using namespace std::string_literals;

        o.pack_map(6);

        o.pack("timestamp"s);
        o.pack(v.occurrence);
    
        o.pack("delay"s);
        o.pack(v.arrival - v.occurrence);

        o.pack("source"s);
        o.pack(v.source);

        o.pack("target"s);
        o.pack(fmt::format("{}", fmt::join(v.targets, std::string{1, Message::s_targetDelim})));

        o.pack("type"s);
        o.pack(v.type);

        o.pack("payload"s);
        o.pack(v.payload);

        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------