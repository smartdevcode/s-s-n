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

struct ClosePosition : public JsonSerializable, public CheckpointSerializable
{
    using Ptr = std::shared_ptr<ClosePosition>;

    OrderID id;
    std::optional<taosim::decimal_t> volume;

    ClosePosition() = default;

    ClosePosition(OrderID id, std::optional<taosim::decimal_t> volume = {}) noexcept
        : id{id}, volume{volume}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------
