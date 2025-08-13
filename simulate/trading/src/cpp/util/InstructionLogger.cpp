/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "InstructionLogger.hpp"
#include "MessagePayload.hpp"

//-------------------------------------------------------------------------

namespace taosim
{

void InstructionLogContext::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        payload->jsonSerialize(json, "instruction");
        json.AddMember("agentId", rapidjson::Value{agentId}, allocator);
        json.AddMember("orderId", rapidjson::Value{orderId}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

}  // namespace taosim

//-------------------------------------------------------------------------
