/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/message/ExchangeAgentMessagePayloads.hpp"
#include "taosim/message/Message.hpp"
#include "taosim/message/MessagePayload.hpp"
#include "taosim/message/MultiBookMessagePayloads.hpp"

#include <stdexcept>

//-------------------------------------------------------------------------

namespace taosim::simulation::replay_helpers
{

[[nodiscard]] Message::Ptr createMessageFromLogFileEntry(const std::string& entry, size_t lineCounter);
[[nodiscard]] MessagePayload::Ptr makePayload(const rapidjson::Value& json);

struct ReplayError : std::exception
{
    std::string message;

    ReplayError(
        std::string_view msg = {},
        std::source_location sl = std::source_location::current()) noexcept
    {
        message = fmt::format(
            "Replay error @ {}#L{}{}",
            sl.file_name(),
            sl.line(),
            msg.empty() ? "" : fmt::format(": {}", msg));
    }

    const char* what() const noexcept override { return message.c_str(); }
};

}  // namespace taosim::simulation::replay_helpers

//-------------------------------------------------------------------------
