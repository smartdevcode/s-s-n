/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "Cancellation.hpp"

//-------------------------------------------------------------------------

void Cancellation::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("event", rapidjson::Value{"cancel", allocator}, allocator);
        json.AddMember("orderId", rapidjson::Value{id}, allocator);
        taosim::json::setOptionalMember(json, "volume", volume.transform(&taosim::util::decimal2double));
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void Cancellation::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("event", rapidjson::Value{"cancel", allocator}, allocator);
        json.AddMember("orderId", rapidjson::Value{id}, allocator);
        taosim::json::setOptionalMember(json, "volume", volume.transform(&taosim::util::packDecimal));
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

Cancellation::Ptr Cancellation::fromJson(const rapidjson::Value& json)
{
    return Cancellation::Ptr{new Cancellation(
        json["orderId"].GetUint64(),
        !json["volume"].IsNull()
            ? std::make_optional(taosim::json::getDecimal(json["volume"]))
            : std::nullopt)};
}

//-------------------------------------------------------------------------

void CancellationLogContext::jsonSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("agentId", rapidjson::Value{agentId}, allocator);
        json.AddMember("bookId", rapidjson::Value{bookId}, allocator);
        json.AddMember("timestamp", rapidjson::Value{bookId}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void CancellationWithLogContext::jsonSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        cancellation.jsonSerialize(json, "cancellation");
        logContext->jsonSerialize(json, "logContext");
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------
