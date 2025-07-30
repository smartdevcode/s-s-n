/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "Order.hpp"

#include "util.hpp"

//-------------------------------------------------------------------------

void BasicOrder::removeVolume(taosim::decimal_t decrease)
{
    if (decrease > m_volume) {
        throw std::runtime_error(fmt::format(
            "{}: Volume to be removed ({}) is greater than standing volume ({})",
            std::source_location::current().function_name(),
            decrease,
            m_volume));
    }
    m_volume -= decrease;
}

//-------------------------------------------------------------------------

void BasicOrder::removeLeveragedVolume(taosim::decimal_t decrease)
{
    taosim::decimal_t leveragedVolume = m_volume * (1_dec + m_leverage);
    if (decrease > leveragedVolume) {
        throw std::runtime_error(fmt::format(
            "{}: Volume to be removed ({}) is greater than standing volume ({})",
            std::source_location::current().function_name(),
            decrease,
            leveragedVolume));
    }
    m_volume -= decrease / (1_dec + m_leverage);
}

//-------------------------------------------------------------------------

void BasicOrder::setVolume(taosim::decimal_t newVolume)
{
    if (newVolume < 0_dec) {
        throw std::invalid_argument(fmt::format(
            "{}: Negative volume ({})",
            std::source_location::current().function_name(),
            newVolume));
    }
    m_volume = newVolume;
}

//-------------------------------------------------------------------------

void BasicOrder::setLeverage(taosim::decimal_t newLeverage)
{
    if (newLeverage < 0_dec) {
        throw std::invalid_argument(fmt::format(
            "{}: Negative leverage ({})",
            std::source_location::current().function_name(),
            newLeverage));
    }
    m_leverage = newLeverage;
}

//-------------------------------------------------------------------------

void BasicOrder::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("orderId", rapidjson::Value{m_id}, allocator);
        json.AddMember("timestamp", rapidjson::Value{m_timestamp}, allocator);
        json.AddMember(
            "volume", rapidjson::Value{taosim::util::decimal2double(m_volume)}, allocator);
        json.AddMember(
            "leverage", rapidjson::Value{taosim::util::decimal2double(m_leverage)}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void BasicOrder::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("orderId", rapidjson::Value{m_id}, allocator);
        json.AddMember("timestamp", rapidjson::Value{m_timestamp}, allocator);
        json.AddMember("volume", rapidjson::Value{taosim::util::packDecimal(m_volume)}, allocator);
        json.AddMember("leverage", rapidjson::Value{taosim::util::packDecimal(m_leverage)}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

BasicOrder::BasicOrder(
    OrderID id,
    Timestamp timestamp,
    taosim::decimal_t volume,
    taosim::decimal_t leverage) noexcept
    : m_id{id},
      m_timestamp{timestamp},
      m_volume{volume},
      m_leverage{leverage}
      
{}

//-------------------------------------------------------------------------

Order::Order(
    OrderID orderId,
    Timestamp timestamp,
    taosim::decimal_t volume,
    OrderDirection direction,
    taosim::decimal_t leverage,
    STPFlag stpFlag,
    SettleFlag settleFlag) noexcept
    : BasicOrder(orderId, timestamp, volume, leverage),
      m_direction{direction}, m_stpFlag{stpFlag}, m_settleFlag{settleFlag}
{}

//-------------------------------------------------------------------------

void Order::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        BasicOrder::jsonSerialize(json);
        auto& allocator = json.GetAllocator();
        json.AddMember(
            "direction", rapidjson::Value{std::to_underlying(m_direction)}, allocator);
        json.AddMember(
            "stpFlag",
            rapidjson::Value{magic_enum::enum_name(m_stpFlag).data(), allocator},
            allocator);
        std::visit([&](auto&& flag) {
            using T = std::decay_t<decltype(flag)>;
            if constexpr (std::is_same_v<T, SettleType>) {
                json.AddMember(
                    "settleFlag",
                    rapidjson::Value{magic_enum::enum_name(flag).data(), allocator},
                    allocator);
            } else if constexpr (std::is_same_v<T, OrderID>) {
                json.AddMember("settleFlag", rapidjson::Value{flag}, allocator);
            }
        }, m_settleFlag);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void Order::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        BasicOrder::checkpointSerialize(json);
        auto& allocator = json.GetAllocator();
        json.AddMember(
            "direction", rapidjson::Value{std::to_underlying(m_direction)}, allocator);
        json.AddMember(
            "stpFlag",
            rapidjson::Value{magic_enum::enum_name(m_stpFlag).data(), allocator},
            allocator);
        std::visit([&](auto&& flag) {
            using T = std::decay_t<decltype(flag)>;
            if constexpr (std::is_same_v<T, SettleType>) {
                json.AddMember(
                    "settleFlag",
                    rapidjson::Value{magic_enum::enum_name(flag).data(), allocator},
                    allocator);
            } else if constexpr (std::is_same_v<T, OrderID>) {
                json.AddMember("settleFlag", rapidjson::Value{flag}, allocator);
            }
        }, m_settleFlag);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

MarketOrder::MarketOrder(
    OrderID orderId,
    Timestamp timestamp,
    taosim::decimal_t volume,
    OrderDirection direction,
    taosim::decimal_t leverage,
    STPFlag stpFlag,
    SettleFlag settleFlag) noexcept
    : Order(orderId, timestamp, volume, direction, leverage, stpFlag, settleFlag)
{}

//-------------------------------------------------------------------------

void MarketOrder::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        Order::jsonSerialize(json);
        auto& allocator = json.GetAllocator();
        json.AddMember("price", rapidjson::Value{}.SetNull(), allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void MarketOrder::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        Order::checkpointSerialize(json);
        auto& allocator = json.GetAllocator();
        json.AddMember("price", rapidjson::Value{}.SetNull(), allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

MarketOrder::Ptr MarketOrder::fromJson(const rapidjson::Value& json)
{
    return Ptr{new MarketOrder(
        json["orderId"].GetUint64(),
        json["timestamp"].GetUint64(),
        taosim::json::getDecimal(json["volume"]),
        OrderDirection{json["direction"].GetUint()},
        taosim::json::getDecimal(json["leverage"]),
        STPFlag{json["stpFlag"].GetUint()},
        json["settleFlag"].IsInt() && magic_enum::enum_cast<SettleType>(json["settleFlag"].GetInt()).has_value()
            ? SettleFlag(magic_enum::enum_cast<SettleType>(json["settleFlag"].GetInt()).value())
            : SettleFlag(static_cast<OrderID>(json["settleFlag"].GetUint()))
    )};
}

//-------------------------------------------------------------------------

LimitOrder::LimitOrder(
    OrderID orderId,
    Timestamp timestamp,
    taosim::decimal_t volume,
    OrderDirection direction,
    taosim::decimal_t price,
    taosim::decimal_t leverage,
    STPFlag stpFlag,
    SettleFlag settleFlag) noexcept
    : Order(orderId, timestamp, volume, direction, leverage, stpFlag, settleFlag),
      m_price{price}
{}

//-------------------------------------------------------------------------

void LimitOrder::setPrice(taosim::decimal_t newPrice)
{
    if (newPrice <= 0_dec) {
        throw std::invalid_argument(fmt::format(
            "{}: Non-positive price ({})",
            std::source_location::current().function_name(),
            newPrice));
    }
    m_price = newPrice;
}

//-------------------------------------------------------------------------

void LimitOrder::jsonSerialize(rapidjson::Document &json, const std::string &key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        Order::jsonSerialize(json);
        auto& allocator = json.GetAllocator();
        json.AddMember("price", rapidjson::Value{taosim::util::decimal2double(m_price)}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void LimitOrder::checkpointSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        Order::checkpointSerialize(json);
        auto& allocator = json.GetAllocator();
        json.AddMember("price", rapidjson::Value{taosim::util::packDecimal(m_price)}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

LimitOrder::Ptr LimitOrder::fromJson(
    const rapidjson::Value& json, int priceDecimals, int volumeDecimals)
{
    return Ptr{new LimitOrder(
        json["orderId"].GetUint64(),
        json["timestamp"].GetUint64(),
        taosim::util::round(taosim::json::getDecimal(json["volume"]), volumeDecimals),
        OrderDirection{json["direction"].GetUint()},
        taosim::json::getDecimal(json["price"]),
        taosim::json::getDecimal(json["leverage"]),
        STPFlag{json["stpFlag"].GetUint()},
        json["settleFlag"].IsInt() && magic_enum::enum_cast<SettleType>(json["settleFlag"].GetInt()).has_value()
            ? SettleFlag(magic_enum::enum_cast<SettleType>(json["settleFlag"].GetInt()).value())
            : SettleFlag(static_cast<OrderID>(json["settleFlag"].GetUint()))
    )};
}

//-------------------------------------------------------------------------

void OrderClientContext::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("agentId", rapidjson::Value{agentId}, allocator);
        taosim::json::setOptionalMember(json, "clientOrderId", clientOrderId);
    };
    return taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

OrderClientContext OrderClientContext::fromJson(const rapidjson::Value& json)
{
    return OrderClientContext{
        json["agentId"].GetInt(),
        !json["clientOrderId"].IsNull()
            ? std::make_optional(json["clientOrderId"].GetUint())
            : std::nullopt};
}

//-------------------------------------------------------------------------

void OrderContext::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("agentId", rapidjson::Value{agentId}, allocator);
        json.AddMember("bookId", rapidjson::Value{bookId}, allocator);
        taosim::json::setOptionalMember(json, "clientOrderId", clientOrderId);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

OrderContext OrderContext::fromJson(const rapidjson::Value& json)
{
    return OrderContext(
        json["agentId"].GetInt(),
        json["bookId"].GetUint(),
        !json["clientOrderId"].IsNull()
            ? std::make_optional(json["clientOrderId"].GetUint64()) 
            : std::nullopt);
}

//-------------------------------------------------------------------------

void OrderEvent::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("orderId", rapidjson::Value{id}, allocator);
        json.AddMember("timestamp", rapidjson::Value{timestamp}, allocator);
        json.AddMember(
            "volume", rapidjson::Value{taosim::util::decimal2double(volume)}, allocator);
        json.AddMember(
            "leverage", rapidjson::Value{taosim::util::decimal2double(leverage)}, allocator);
        json.AddMember(
            "direction", rapidjson::Value{std::to_underlying(direction)}, allocator);
        json.AddMember(
            "stpFlag",
            rapidjson::Value{magic_enum::enum_name(stpFlag).data(), allocator},
            allocator);
        if (price) {
            json.AddMember("price", taosim::util::decimal2double(*price), allocator);
        } else {
            json.AddMember("price", rapidjson::Value{}.SetNull(), allocator);
        }
        json.AddMember("event", rapidjson::Value{"place", allocator}, allocator);
        json.AddMember("agentId", rapidjson::Value{ctx.agentId}, allocator);
        taosim::json::setOptionalMember(json, "clientOrderId", ctx.clientOrderId);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void OrderLogContext::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("agentId", rapidjson::Value{agentId}, allocator);
        json.AddMember("bookId", rapidjson::Value{bookId}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void OrderWithLogContext::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        order->jsonSerialize(json, "order");
        logContext->jsonSerialize(json, "logContext");
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------
