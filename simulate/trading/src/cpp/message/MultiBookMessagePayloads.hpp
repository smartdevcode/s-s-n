/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ExchangeAgentMessagePayloads.hpp"
#include "MessagePayload.hpp"

//-------------------------------------------------------------------------

struct BookStateMessagePayload : public MessagePayload 
{
    using Ptr = std::shared_ptr<BookStateMessagePayload>;

    std::string bookStateJsonStr;

    BookStateMessagePayload(const rapidjson::Value& bookState)
        : bookStateJsonStr{taosim::json::json2str(bookState)}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct DistributedAgentResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<DistributedAgentResponsePayload>;

    AgentId agentId;
    MessagePayload::Ptr payload;

    DistributedAgentResponsePayload(AgentId agentId, MessagePayload::Ptr payload)
        : agentId{agentId}, payload{payload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------
