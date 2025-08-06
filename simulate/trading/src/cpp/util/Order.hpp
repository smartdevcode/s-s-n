/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "JsonSerializable.hpp"
#include "common.hpp"
#include "Flags.hpp"

//-------------------------------------------------------------------------

using ClientOrderID = std::decay_t<OrderID>;
using STPFlag = taosim::STPFlag;
using SettleFlag = taosim::SettleFlag;
using SettleType = taosim::SettleType;

enum class OrderDirection : uint32_t
{
    BUY,
    SELL
};

enum class Currency : uint32_t
{
    BASE,
    QUOTE    
};


[[nodiscard]] constexpr std::string_view OrderDirection2StrView(OrderDirection dir) noexcept
{
    return magic_enum::enum_name(dir);
}

template<>
struct fmt::formatter<OrderDirection>
{
    constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(OrderDirection dir, FormatContext& ctx)
    {
        return fmt::format_to(ctx.out(), "{}", OrderDirection2StrView(dir));
    }
};

enum class OrderErrorCode : uint32_t
{
    VALID,
    NONEXISTENT_ACCOUNT,
    INSUFFICIENT_BASE,
    INSUFFICIENT_QUOTE,
    EMPTY_BOOK,
    PRICE_INCREMENT_VIOLATED,
    VOLUME_INCREMENT_VIOLATED,
    EXCEEDING_LOAN,
    CONTRACT_VIOLATION,
    INVALID_LEVERAGE,
    INVALID_VOLUME,
    INVALID_PRICE,
    EXCEEDING_MAX_ORDERS,
    DUAL_POSITION
};

[[nodiscard]] constexpr std::string_view OrderErrorCode2StrView(OrderErrorCode ec) noexcept
{
    return magic_enum::enum_name(ec);
}

template<>
struct fmt::formatter<OrderErrorCode>
{
    constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(OrderErrorCode ec, FormatContext& ctx)
    {
        return fmt::format_to(ctx.out(), "{}", OrderErrorCode2StrView(ec));
    }
};

//-------------------------------------------------------------------------

class BasicOrder : public JsonSerializable, public CheckpointSerializable
{
public:
    virtual ~BasicOrder() noexcept = default;

    [[nodiscard]] OrderID id() const noexcept { return m_id; }
    [[nodiscard]] Timestamp timestamp() const noexcept { return m_timestamp; }
    [[nodiscard]] taosim::decimal_t volume() const noexcept { return m_volume; }
    [[nodiscard]] taosim::decimal_t totalVolume() const noexcept { return m_volume * taosim::util::dec1p(m_leverage); }
    [[nodiscard]] taosim::decimal_t leverage() const noexcept { return m_leverage; }
    
    void removeVolume(taosim::decimal_t decrease);
    void removeLeveragedVolume(taosim::decimal_t decrease);
    void setVolume(taosim::decimal_t newVolume);
    void setLeverage(taosim::decimal_t newLeverage);

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

protected:
    BasicOrder(
        OrderID id,
        Timestamp timestamp,
        taosim::decimal_t volume,
        taosim::decimal_t leverage = 0_dec) noexcept;

private:
    OrderID m_id;
    Timestamp m_timestamp;
    taosim::decimal_t m_volume;
    taosim::decimal_t m_leverage;
};

//-------------------------------------------------------------------------

class Order : public BasicOrder
{
public:
    using Ptr = std::shared_ptr<Order>;

    [[nodiscard]] OrderDirection direction() const noexcept { return m_direction; }
    [[nodiscard]] STPFlag stpFlag() const noexcept { return m_stpFlag; }
    [[nodiscard]] SettleFlag settleFlag() const noexcept { return m_settleFlag; }
    [[nodiscard]] Currency currency() const noexcept { return m_currency; }

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

protected:
    Order(
        OrderID orderId,
        Timestamp timestamp,
        taosim::decimal_t volume,
        OrderDirection direction,
        taosim::decimal_t leverage = 0_dec,
        STPFlag stpFlag = STPFlag::CO,
        SettleFlag settleFlag = SettleType::FIFO,
        Currency currency = Currency::BASE) noexcept;

private:
    OrderDirection m_direction;
    STPFlag m_stpFlag;
    SettleFlag m_settleFlag;
    Currency m_currency;
};

//-------------------------------------------------------------------------

class MarketOrder : public Order
{
public:
    using Ptr = std::shared_ptr<MarketOrder>;

    MarketOrder(
        OrderID orderId,
        Timestamp timestamp,
        taosim::decimal_t volume,
        OrderDirection direction,
        taosim::decimal_t leverage = 0_dec,
        STPFlag stpFlag = STPFlag::CO,
        SettleFlag settleFlag = SettleType::FIFO,
        Currency currency = Currency::BASE) noexcept;

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

class LimitOrder : public Order
{
public:
    using Ptr = std::shared_ptr<LimitOrder>;

    LimitOrder(
        OrderID orderId,
        Timestamp timestamp,
        taosim::decimal_t volume,
        OrderDirection direction,
        taosim::decimal_t price,
        taosim::decimal_t leverage = 0_dec,
        STPFlag stpFlag = STPFlag::CO,
        SettleFlag settleFlag = SettleType::FIFO,
        bool postOnly = false,
        taosim::TimeInForce timeInForce = taosim::TimeInForce::GTC,
        std::optional<Timestamp> expiryPeriod = std::nullopt,
        Currency currency = Currency::BASE) noexcept;

    [[nodiscard]] taosim::decimal_t price() const noexcept { return m_price; };
    [[nodiscard]] bool postOnly() const noexcept { return m_postOnly; }
    [[nodiscard]] taosim::TimeInForce timeInForce() const noexcept { return m_timeInForce; }
    [[nodiscard]] std::optional<Timestamp> expiryPeriod() const noexcept { return m_expiryPeriod; }

    void setPrice(taosim::decimal_t newPrice);

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json, int priceDecimals, int volumeDecimals);

private:
    taosim::decimal_t m_price;
    bool m_postOnly;
    taosim::TimeInForce m_timeInForce;
    std::optional<Timestamp> m_expiryPeriod;
};

//-------------------------------------------------------------------------

struct OrderClientContext : public CheckpointSerializable
{
    AgentId agentId;
    std::optional<ClientOrderID> clientOrderId;

    OrderClientContext() noexcept = default;

    OrderClientContext(AgentId agentId, std::optional<ClientOrderID> clientOrderId = {}) noexcept
        : agentId{agentId}, clientOrderId{clientOrderId}
    {}

    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static OrderClientContext fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct OrderContext : public JsonSerializable
{
    AgentId agentId;
    BookId bookId;
    std::optional<ClientOrderID> clientOrderId;

    OrderContext() = default;

    OrderContext(
        AgentId agentId, BookId bookId, std::optional<ClientOrderID> clientOrderId = {}) noexcept
        : agentId{agentId}, bookId{bookId}, clientOrderId{clientOrderId}
    {}

    void jsonSerialize(rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static OrderContext fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct OrderEvent : public JsonSerializable
{
    using Ptr = std::shared_ptr<OrderEvent>;

    OrderID id;
    Timestamp timestamp;
    taosim::decimal_t volume;
    taosim::decimal_t leverage;
    OrderDirection direction;
    STPFlag stpFlag;
    std::optional<taosim::decimal_t> price{};
    OrderContext ctx;
    std::optional<bool> postOnly{};
    std::optional<taosim::TimeInForce> timeInForce{};
    std::optional<std::optional<Timestamp>> expiryPeriod{};
    Currency currency{Currency::BASE};

    OrderEvent(Order::Ptr order, OrderContext ctx) noexcept
        : id{order->id()},
          timestamp{order->timestamp()},
          volume{order->volume()},
          leverage{order->leverage()},
          direction{order->direction()},
          stpFlag{order->stpFlag()},
          ctx{ctx}
    {
        if (auto limitOrder = std::dynamic_pointer_cast<LimitOrder>(order)) {
            price = limitOrder->price();
            postOnly = std::make_optional(limitOrder->postOnly());
            timeInForce = std::make_optional(limitOrder->timeInForce());
            expiryPeriod = std::make_optional(limitOrder->expiryPeriod());
        }
    }

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------

struct OrderLogContext : public JsonSerializable
{
    using Ptr = std::shared_ptr<OrderLogContext>;

    AgentId agentId;
    BookId bookId;

    OrderLogContext(AgentId agentId, BookId bookId) noexcept
        : agentId{agentId}, bookId{bookId}
    {}

    void jsonSerialize(rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------

struct OrderWithLogContext : public JsonSerializable
{
    using Ptr = std::shared_ptr<OrderWithLogContext>;

    Order::Ptr order;
    OrderLogContext::Ptr logContext;

    OrderWithLogContext(Order::Ptr order, OrderLogContext::Ptr logContext) noexcept
        : order{order}, logContext{logContext}
    {}

    void jsonSerialize(rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------
