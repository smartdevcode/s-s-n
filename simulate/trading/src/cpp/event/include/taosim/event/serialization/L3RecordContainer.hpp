/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/event/L3RecordContainer.hpp"
#include "taosim/event/serialization/common.hpp"

#include <msgpack.hpp>

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct pack<taosim::event::L3Record>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::event::L3Record& v) const
    {
        o.pack_array(v.size());
        for (const auto& entry : v) {
            o.pack(entry);
        }
        return o;
    }
};

template<>
struct pack<taosim::event::L3RecordContainer>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::event::L3RecordContainer& v) const
    {
        o.pack_map(v.underlying().size());
        for (const auto& [bookId, record] : views::enumerate(v.underlying())) {
            o.pack(std::to_string(bookId));
            o.pack(record);
        }
        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------
