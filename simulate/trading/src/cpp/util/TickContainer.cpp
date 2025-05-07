/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "TickContainer.hpp"

#include "util.hpp"

//-------------------------------------------------------------------------

void TickContainer::pop_front()
{
    const auto& elem = ContainerType::front();
    ContainerType::pop_front();
    m_volume -= elem->totalVolume();
}

//-------------------------------------------------------------------------

void TickContainer::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("price", rapidjson::Value{taosim::util::decimal2double(m_price)}, allocator);
        rapidjson::Value ordersJson{rapidjson::kArrayType};
        taosim::decimal_t volumeOnLevel{};
        for (const auto order : *this) {
            rapidjson::Document orderJson{&allocator};
            order->jsonSerialize(orderJson);
            orderJson.RemoveMember("price");
            ordersJson.PushBack(orderJson, allocator);
            volumeOnLevel += order->volume();
        }
        json.AddMember("orders", ordersJson, allocator);
        json.AddMember("volume", rapidjson::Value{taosim::util::decimal2double(volumeOnLevel)}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void TickContainer::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("price", rapidjson::Value{taosim::util::packDecimal(m_price)}, allocator);
        rapidjson::Value ordersJson{rapidjson::kArrayType};
        taosim::decimal_t volumeOnLevel{};
        for (const auto order : *this) {
            rapidjson::Document orderJson{&allocator};
            order->checkpointSerialize(orderJson);
            orderJson.RemoveMember("price");
            ordersJson.PushBack(orderJson, allocator);
            volumeOnLevel += order->volume();
        }
        json.AddMember("orders", ordersJson, allocator);
        json.AddMember(
            "volume", rapidjson::Value{taosim::util::packDecimal(volumeOnLevel)}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

TickContainer TickContainer::fromJson(const rapidjson::Value& json)
{
    TickContainer container{taosim::json::getDecimal(json["price"])};
    for (const auto& order : json["orders"].GetArray()) {
        rapidjson::Document orderWithPriceJson = [&] {
            rapidjson::Document orderWithPriceJson;
            auto& allocator = orderWithPriceJson.GetAllocator();
            orderWithPriceJson.CopyFrom(order, allocator);
            orderWithPriceJson.AddMember(
                "price",
                // Should maybe just have separate fromCkpt?
                rapidjson::Value{taosim::util::packDecimal(taosim::json::getDecimal(json["price"]))},
                allocator);
            return orderWithPriceJson;
        }();
        container.push_back(LimitOrder::fromJson(orderWithPriceJson, 16, 16));
    }
    return container;
}

//-------------------------------------------------------------------------
