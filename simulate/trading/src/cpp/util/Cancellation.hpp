/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "JsonSerializable.hpp"
#include "Order.hpp"
#include "common.hpp"
#include "json_util.hpp"

#include <memory>
#include <optional>

//-------------------------------------------------------------------------

struct Cancellation : public JsonSerializable, public CheckpointSerializable
{
    using Ptr = std::shared_ptr<Cancellation>;

    OrderID id;
    std::optional<taosim::decimal_t> volume;

    Cancellation() = default;

    Cancellation(OrderID id, std::optional<taosim::decimal_t> volume = {}) noexcept
        : id{id}, volume{volume}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct CancellationLogContext : public JsonSerializable
{
    using Ptr = std::shared_ptr<CancellationLogContext>;

    AgentId agentId;
    BookId bookId;
    Timestamp timestamp;

    CancellationLogContext(AgentId agentId, BookId bookId, Timestamp timestamp)
        : agentId{agentId}, bookId{bookId}, timestamp{timestamp}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------

struct CancellationWithLogContext : public JsonSerializable
{
    using Ptr = std::shared_ptr<CancellationWithLogContext>;

    Cancellation cancellation;
    CancellationLogContext::Ptr logContext;

    CancellationWithLogContext(
        Cancellation cancellation,
        CancellationLogContext::Ptr logContext) noexcept
        : cancellation{cancellation}, logContext{logContext}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------
