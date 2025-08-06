/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "OrderFactory.hpp"

//-------------------------------------------------------------------------

MarketOrder::Ptr OrderFactory::makeMarketOrder(
    OrderDirection direction,
    Timestamp timestamp,
    taosim::decimal_t volume,
    taosim::decimal_t leverage,
    taosim::STPFlag stpFlag,
    taosim::SettleFlag settleFlag,
    Currency currency) const noexcept
{
    return MarketOrder::Ptr{
        new MarketOrder(
            m_idCounter++, timestamp, volume, direction, leverage, stpFlag, settleFlag, currency)};
}

//-------------------------------------------------------------------------

LimitOrder::Ptr OrderFactory::makeLimitOrder(
    OrderDirection direction,
    Timestamp timestamp,
    taosim::decimal_t volume,
    taosim::decimal_t price,
    taosim::decimal_t leverage,
    taosim::STPFlag stpFlag,
    taosim::SettleFlag settleFlag,
    bool postOnly,
    taosim::TimeInForce timeInForce,
    std::optional<Timestamp> expiryPeriod,
    Currency currency) const noexcept
{
    return LimitOrder::Ptr{
        new LimitOrder(
            m_idCounter++,
            timestamp,
            volume,
            direction,
            price,
            leverage,
            stpFlag,
            settleFlag,
            postOnly,
            timeInForce,
            expiryPeriod,
            currency)};
}

//-------------------------------------------------------------------------

void OrderFactory::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("idCounter", rapidjson::Value{m_idCounter}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

OrderFactory OrderFactory::fromJson(const rapidjson::Value& json)
{
    OrderFactory factory;
    factory.m_idCounter = json["idCounter"].GetUint64();
    return factory;
}

//-------------------------------------------------------------------------
