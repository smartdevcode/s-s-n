/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "TradeFactory.hpp"

//-------------------------------------------------------------------------

void TradeFactory::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("idCounter", rapidjson::Value{m_idCounter}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

TradeFactory TradeFactory::fromJson(const rapidjson::Value& json)
{
    TradeFactory factory;
    factory.m_idCounter = json["idCounter"].GetUint64();
    return factory;
}

//-------------------------------------------------------------------------
