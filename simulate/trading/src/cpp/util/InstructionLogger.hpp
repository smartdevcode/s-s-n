/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "common.hpp"
#include "JsonSerializable.hpp"

//-------------------------------------------------------------------------

struct MessagePayload;


namespace taosim
{

struct InstructionLogContext : public JsonSerializable
{
    using Ptr = std::shared_ptr<InstructionLogContext>;

    AgentId agentId;
    OrderID orderId;
    std::shared_ptr<MessagePayload> payload;

    InstructionLogContext(
        AgentId agentId,
        OrderID orderId,
        const std::shared_ptr<MessagePayload> payload) noexcept
        : agentId{agentId}, orderId{orderId}, payload{payload}
    {}

    void jsonSerialize(rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------

}  // namespace taosim

//-------------------------------------------------------------------------