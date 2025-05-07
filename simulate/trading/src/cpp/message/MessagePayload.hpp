/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "JsonSerializable.hpp"
#include "common.hpp"
#include "mp.hpp"
#include "util.hpp"

//-------------------------------------------------------------------------

struct MessagePayload : public JsonSerializable, public CheckpointSerializable
{
    using Ptr = std::shared_ptr<MessagePayload>;

    virtual ~MessagePayload() noexcept = default;

    template<typename T, typename... Args>
    requires(
        std::derived_from<T, MessagePayload> && std::constructible_from<T, Args...> &&
        taosim::mp::IsPointer<typename T::Ptr>)
    [[nodiscard]] static T::Ptr create(Args&&... args) noexcept
    {
        return typename T::Ptr{new T(std::forward<Args>(args)...)};
    }

protected:
    MessagePayload() noexcept = default;
};

//-------------------------------------------------------------------------

struct ErrorResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<ErrorResponsePayload>;

    std::string message;

    ErrorResponsePayload(std::string message) : message{std::move(message)} {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct SuccessResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<SuccessResponsePayload>;

    std::string message;

    SuccessResponsePayload(std::string message) : message{std::move(message)} {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct EmptyPayload : public MessagePayload
{
    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------

struct GenericPayload : public MessagePayload, public std::map<std::string, std::string>
{
    GenericPayload(std::map<std::string, std::string> initMap)
        : MessagePayload{}, map{std::move(initMap)}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------
