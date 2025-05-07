/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "MessagePayload.hpp"

//-------------------------------------------------------------------------

void ErrorResponsePayload::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("message", rapidjson::Value{message.c_str(), allocator}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void ErrorResponsePayload::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    jsonSerialize(json, key);
}

//-------------------------------------------------------------------------

ErrorResponsePayload::Ptr ErrorResponsePayload::fromJson(const rapidjson::Value& json)
{
    return MessagePayload::create<ErrorResponsePayload>(json["message"].GetString());
}

//-------------------------------------------------------------------------

void SuccessResponsePayload::jsonSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("message", rapidjson::Value{message.c_str(), allocator}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void SuccessResponsePayload::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    jsonSerialize(json, key);
}

//-------------------------------------------------------------------------

SuccessResponsePayload::Ptr SuccessResponsePayload::fromJson(const rapidjson::Value& json)
{
    return MessagePayload::create<SuccessResponsePayload>(json["message"].GetString());
}

//-------------------------------------------------------------------------

void EmptyPayload::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [](rapidjson::Document& json) {
        json.SetNull();
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void EmptyPayload::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    jsonSerialize(json, key);
}

//-------------------------------------------------------------------------

void GenericPayload::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        for (const auto& [key, val] : *this) {
            json.AddMember(
                rapidjson::Value{key.c_str(), allocator},
                rapidjson::Value{val.c_str(), allocator},
                allocator);
        }
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void GenericPayload::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    jsonSerialize(json, key);
}

//-------------------------------------------------------------------------

GenericPayload::Ptr GenericPayload::fromJson(const rapidjson::Value& json)
{
    return MessagePayload::create<GenericPayload>([&] {
        std::map<std::string, std::string> map;
        for (const auto& member : json.GetObject()) {
            map[member.name.GetString()] = member.value.GetString();
        }
        return map;
    }());
}

//-------------------------------------------------------------------------
