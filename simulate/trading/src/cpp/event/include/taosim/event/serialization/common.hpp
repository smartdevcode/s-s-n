/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/event/L3RecordContainer.hpp"
#include "taosim/serialization/msgpack_util.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct convert<taosim::event::L3Record::Entry>
{
    const msgpack::object& operator()(
        const msgpack::object& o, taosim::event::L3Record::Entry& v) const
    {
        using namespace std::literals::string_view_literals;

        static constexpr auto ctx = std::source_location::current().function_name();

        if (o.type != msgpack::type::MAP) {
            throw taosim::serialization::MsgPackError{};
        }
        const auto eventTypePtr = taosim::serialization::msgpackFind(o, "event"sv);
        if (eventTypePtr == nullptr) {
            throw std::runtime_error{fmt::format("{}: Missing field 'event'", ctx)};
        }
        std::string_view eventType{eventTypePtr->via.str.ptr, eventTypePtr->via.str.size};
        if (eventType == "cancel") {
            v = o.as<taosim::event::CancellationEvent>();
        }
        else if (eventType == "place") {
            v = o.as<taosim::event::OrderEvent>();
        }
        else if (eventType == "trade") {
            v = o.as<taosim::event::TradeEvent>();
        }
        else {
            throw std::runtime_error{fmt::format("{}: Invalid event type '{}'", ctx, eventType)};
        }
    }
};

template<>
struct pack<taosim::event::L3Record::Entry>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::event::L3Record::Entry& v) const
    {
        std::visit([&](auto&& entry) { o.pack(entry); }, v);
        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------

