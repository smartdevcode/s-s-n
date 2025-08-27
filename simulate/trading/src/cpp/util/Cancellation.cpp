/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "Cancellation.hpp"

//-------------------------------------------------------------------------

void Cancellation::L3Serialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("e", rapidjson::Value{"cancel", allocator}, allocator);
        json.AddMember("i", rapidjson::Value{id}, allocator);
        taosim::json::setOptionalMember(json, "v", volume.transform(&taosim::util::decimal2double));
    };
    taosim::json::serializeHelper(json, key, serialize);
}

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

void CancellationLogContext::L3Serialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("a", rapidjson::Value{agentId}, allocator);
        json.AddMember("b", rapidjson::Value{bookId}, allocator);
        json.AddMember("j", rapidjson::Value{timestamp}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
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
        json.AddMember("timestamp", rapidjson::Value{timestamp}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void CancellationWithLogContext::L3Serialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        cancellation.L3Serialize(json, "c");
        logContext->L3Serialize(json, "g");
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

void CancellationEvent::jsonSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        auto& allocator = json.GetAllocator();
        cancellation.jsonSerialize(json);
        json.AddMember("timestamp", rapidjson::Value{timestamp}, allocator);
        json.AddMember("price", rapidjson::Value{taosim::util::decimal2double(price)}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------
