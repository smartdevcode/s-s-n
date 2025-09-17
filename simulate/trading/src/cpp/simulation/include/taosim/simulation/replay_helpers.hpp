/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ExchangeAgentMessagePayloads.hpp"
#include "Message.hpp"
#include "MessagePayload.hpp"
#include "MultiBookMessagePayloads.hpp"

//-------------------------------------------------------------------------

namespace taosim::simulation::replay_helpers
{

[[nodiscard]] Message::Ptr createMessageFromLogFileEntry(const std::string& entry, size_t lineCounter);
[[nodiscard]] MessagePayload::Ptr makePayload(const rapidjson::Value& json);

}  // namespace taosim::simulation::replay_helpers

//-------------------------------------------------------------------------
