/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/message/serialization/EmptyPayload.hpp"
#include "taosim/message/serialization/Message.hpp"
#include "taosim/message/ExchangeAgentMessagePayloads.hpp"
#include "taosim/message/Message.hpp"
#include "taosim/message/MessagePayload.hpp"
#include "taosim/message/MultiBookMessagePayloads.hpp"

#include <msgpack.hpp>

//-------------------------------------------------------------------------

namespace taosim::simulation::serialization
{

struct ValidatorResponse
{
    std::vector<Message::Ptr> responses;

    MSGPACK_DEFINE_MAP(responses);
};

}  // taosim::simulation::serialization

//-------------------------------------------------------------------------
