/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Book.hpp"
#include "Cancellation.hpp"
#include "MessagePayload.hpp"
#include "Order.hpp"
#include "Trade.hpp"
#include "common.hpp"

#include <optional>
#include <vector>

//-------------------------------------------------------------------------

struct StartSimulationPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<StartSimulationPayload>;

    std::string logDir;

    StartSimulationPayload(const std::string& logDir) noexcept
        : logDir{logDir}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct PlaceOrderMarketPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<PlaceOrderMarketPayload>;

    OrderDirection direction;
    taosim::decimal_t volume;
    taosim::decimal_t leverage;
    BookId bookId;
    std::optional<ClientOrderID> clientOrderId{};

    PlaceOrderMarketPayload(
        OrderDirection direction,
        taosim::decimal_t volume,
        BookId bookId,
        std::optional<ClientOrderID> clientOrderId = {}) noexcept
        : direction{direction}, volume{volume}, leverage{0_dec}, bookId{bookId}, clientOrderId{clientOrderId}
    {}

    PlaceOrderMarketPayload(
        OrderDirection direction,
        taosim::decimal_t volume,
        taosim::decimal_t leverage,
        BookId bookId,
        std::optional<ClientOrderID> clientOrderId = {}) noexcept
        : direction{direction}, volume{volume}, leverage{leverage}, bookId{bookId}, clientOrderId{clientOrderId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct PlaceOrderMarketResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<PlaceOrderMarketResponsePayload>;

    OrderID id;
    PlaceOrderMarketPayload::Ptr requestPayload;

    PlaceOrderMarketResponsePayload(
        OrderID id, PlaceOrderMarketPayload::Ptr requestPayload) noexcept
        : id{id}, requestPayload{requestPayload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct PlaceOrderMarketErrorResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<PlaceOrderMarketErrorResponsePayload>;

    PlaceOrderMarketPayload::Ptr requestPayload;
    ErrorResponsePayload::Ptr errorPayload;

    PlaceOrderMarketErrorResponsePayload(
        PlaceOrderMarketPayload::Ptr requestPayload,
        ErrorResponsePayload::Ptr errorPayload) noexcept
        : requestPayload{requestPayload}, errorPayload{std::move(errorPayload)}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

enum class LimitOrderFlag : uint32_t
{
    NONE,
    POST_ONLY,
    IOC
};

struct PlaceOrderLimitPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<PlaceOrderLimitPayload>;

    OrderDirection direction;
    taosim::decimal_t volume;
    taosim::decimal_t price;
    taosim::decimal_t leverage;
    BookId bookId;
    std::optional<ClientOrderID> clientOrderId{};
    LimitOrderFlag flag{LimitOrderFlag::NONE};

    PlaceOrderLimitPayload(
        OrderDirection direction,
        taosim::decimal_t volume,
        taosim::decimal_t price,
        BookId bookId,
        std::optional<ClientOrderID> clientOrderId = {},
        LimitOrderFlag flag = LimitOrderFlag::NONE) noexcept
        : direction{direction},
          volume{volume},
          price{price},
          leverage{0_dec},
          bookId{bookId},
          clientOrderId{clientOrderId},
          flag{flag}
    {}

    PlaceOrderLimitPayload(
        OrderDirection direction,
        taosim::decimal_t volume,
        taosim::decimal_t price,
        taosim::decimal_t leverage,
        BookId bookId,
        std::optional<ClientOrderID> clientOrderId = {},
        LimitOrderFlag flag = LimitOrderFlag::NONE) noexcept
        : direction{direction},
          volume{volume},
          price{price},
          leverage{leverage},
          bookId{bookId},
          clientOrderId{clientOrderId},
          flag{flag}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

namespace taosim
{
    
[[nodiscard]] inline bool violatesNone(
    Book::Ptr book, PlaceOrderLimitPayload::Ptr limitOrderPayload) noexcept
{
    return false;
}

[[nodiscard]] bool violatesPostOnly(
    Book::Ptr book, PlaceOrderLimitPayload::Ptr limitOrderPayload) noexcept;

[[nodiscard]] bool violatesImmediateOrCancel(
    Book::Ptr book, PlaceOrderLimitPayload::Ptr limitOrderPayload) noexcept;

inline constexpr std::array limitOrderFlag2ViolationChecker {
    &violatesNone,
    &violatesPostOnly,
    &violatesImmediateOrCancel
};
    
}  // namespace taosim

//-------------------------------------------------------------------------

struct PlaceOrderLimitResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<PlaceOrderLimitResponsePayload>;

    OrderID id;
    PlaceOrderLimitPayload::Ptr requestPayload;

    PlaceOrderLimitResponsePayload(OrderID id, PlaceOrderLimitPayload::Ptr requestPayload) noexcept
        : id{id}, requestPayload{requestPayload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct PlaceOrderLimitErrorResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<PlaceOrderLimitErrorResponsePayload>;

    PlaceOrderLimitPayload::Ptr requestPayload;
    ErrorResponsePayload::Ptr errorPayload;

    PlaceOrderLimitErrorResponsePayload(
        PlaceOrderLimitPayload::Ptr requestPayload, ErrorResponsePayload::Ptr errorPayload) noexcept
        : requestPayload{requestPayload}, errorPayload{errorPayload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct RetrieveOrdersPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<RetrieveOrdersPayload>;

    std::vector<OrderID> ids;
    BookId bookId;

    RetrieveOrdersPayload(std::vector<OrderID> ids, BookId bookId) noexcept
        : ids{std::move(ids)}, bookId{bookId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct RetrieveOrdersResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<RetrieveOrdersResponsePayload>;

    std::vector<LimitOrder> orders;
    BookId bookId;

    RetrieveOrdersResponsePayload() = default;

    RetrieveOrdersResponsePayload(std::vector<LimitOrder> orders, BookId bookId) noexcept
        : orders{std::move(orders)}, bookId{bookId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct CancelOrdersPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<CancelOrdersPayload>;

    std::vector<Cancellation> cancellations;
    BookId bookId;

    CancelOrdersPayload() = default;

    CancelOrdersPayload(std::vector<Cancellation> cancellations, BookId bookId) noexcept
        : cancellations{std::move(cancellations)}, bookId{bookId}
    {}

    CancelOrdersPayload(Cancellation cancellation, BookId bookId) noexcept
        : cancellations{cancellation}, bookId{bookId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct CancelOrdersResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<CancelOrdersResponsePayload>;

    std::vector<OrderID> orderIds;
    CancelOrdersPayload::Ptr requestPayload;

    CancelOrdersResponsePayload(std::vector<OrderID> orderIds, CancelOrdersPayload::Ptr requestPayload) noexcept
        : orderIds{std::move(orderIds)}, requestPayload{requestPayload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct CancelOrdersErrorResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<CancelOrdersErrorResponsePayload>;

    std::vector<OrderID> orderIds;
    CancelOrdersPayload::Ptr requestPayload;
    ErrorResponsePayload::Ptr errorPayload;

    CancelOrdersErrorResponsePayload(
        std::vector<OrderID> orderIds,
        CancelOrdersPayload::Ptr requestPayload,
        ErrorResponsePayload::Ptr errorPayload) noexcept
        : orderIds{std::move(orderIds)}, requestPayload{requestPayload}, errorPayload{errorPayload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct RetrieveBookPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<RetrieveBookPayload>;

    size_t depth;
    BookId bookId;

    RetrieveBookPayload(size_t depth, BookId bookId) noexcept
        : depth{depth}, bookId{bookId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct RetrieveBookResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<RetrieveBookResponsePayload>;

    Timestamp time;
    std::vector<TickContainer> tickContainers;

    RetrieveBookResponsePayload(Timestamp time) noexcept
        : RetrieveBookResponsePayload(time, std::vector<TickContainer>{})
    {}

    RetrieveBookResponsePayload(
        Timestamp time, std::vector<TickContainer> tickContainers) noexcept
        : time{time}, tickContainers{std::move(tickContainers)}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct RetrieveL1Payload : public MessagePayload
{
    using Ptr = std::shared_ptr<RetrieveL1Payload>;

    BookId bookId;

    RetrieveL1Payload() = default;

    RetrieveL1Payload(BookId bookId) noexcept : bookId{bookId} {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct RetrieveL1ResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<RetrieveL1ResponsePayload>;

    Timestamp time{};
    taosim::decimal_t bestAskPrice{};
    taosim::decimal_t bestAskVolume{};
    taosim::decimal_t askTotalVolume{};
    taosim::decimal_t bestBidPrice{};
    taosim::decimal_t bestBidVolume{};
    taosim::decimal_t bidTotalVolume{};
    BookId bookId;

    RetrieveL1ResponsePayload() noexcept = default;

    RetrieveL1ResponsePayload(Timestamp time, BookId bookId) noexcept
        : time{time}, bookId{bookId}
    {}

    RetrieveL1ResponsePayload(
        Timestamp time,
        taosim::decimal_t bestAskPrice,
        taosim::decimal_t bestAskVolume,
        taosim::decimal_t askTotalVolume,
        taosim::decimal_t bestBidPrice,
        taosim::decimal_t bestBidVolume,
        taosim::decimal_t bidTotalVolume,
        BookId bookId) noexcept
        : time{time},
          bestAskPrice{bestAskPrice},
          bestAskVolume{bestAskVolume},
          askTotalVolume{askTotalVolume},
          bestBidPrice{bestBidPrice},
          bestBidVolume{bestBidVolume},
          bidTotalVolume{bidTotalVolume},
          bookId{bookId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct SubscribeEventTradeByOrderPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<SubscribeEventTradeByOrderPayload>;

    OrderID id;

    SubscribeEventTradeByOrderPayload(OrderID id) noexcept : id{id} {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct EventOrderMarketPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<EventOrderMarketPayload>;

    MarketOrder order;

    EventOrderMarketPayload(const MarketOrder& order) noexcept : order{order} {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct EventOrderLimitPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<EventOrderLimitPayload>;

    LimitOrder order;

    EventOrderLimitPayload(const LimitOrder& order) noexcept : order{order} {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct EventTradePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<EventTradePayload>;

    Trade trade;
    TradeLogContext context;
    BookId bookId;
    std::optional<ClientOrderID> clientOrderId{};

    EventTradePayload(
        const Trade& trade,
        const TradeLogContext& context,
        BookId bookId,
        std::optional<ClientOrderID> clientOrderId = {}) noexcept
        : trade{trade}, context{context}, bookId{bookId}, clientOrderId{clientOrderId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct ResetAgentsPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<ResetAgentsPayload>;

    std::vector<AgentId> agentIds;

    ResetAgentsPayload() = default;

    ResetAgentsPayload(std::vector<AgentId> agentIds) noexcept
        : agentIds{std::move(agentIds)}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct ResetAgentsResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<ResetAgentsResponsePayload>;

    std::vector<AgentId> agentIds;
    ResetAgentsPayload::Ptr requestPayload;

    ResetAgentsResponsePayload(
        std::vector<AgentId> agentIds,ResetAgentsPayload::Ptr requestPayload) noexcept
        : agentIds{std::move(agentIds)}, requestPayload{requestPayload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------

struct ResetAgentsErrorResponsePayload : public MessagePayload
{
    using Ptr = std::shared_ptr<ResetAgentsErrorResponsePayload>;

    std::vector<AgentId> agentIds;
    ResetAgentsPayload::Ptr requestPayload;
    ErrorResponsePayload::Ptr errorPayload;

    ResetAgentsErrorResponsePayload(
        std::vector<AgentId> agentIds,
        ResetAgentsPayload::Ptr requestPayload,
        ErrorResponsePayload::Ptr errorPayload) noexcept
        : agentIds{std::move(agentIds)}, requestPayload{requestPayload}, errorPayload{errorPayload}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static Ptr fromJson(const rapidjson::Value& json);
};

//-------------------------------------------------------------------------
// TODO: Useless?

struct WakeupForCancellationPayload : public MessagePayload
{
    using Ptr = std::shared_ptr<WakeupForCancellationPayload>;

    OrderID orderToCancelId;
    BookId bookId;

    WakeupForCancellationPayload(OrderID orderToCancelId, BookId bookId) noexcept
        : orderToCancelId{orderToCancelId}, bookId{bookId}
    {}

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
};

//-------------------------------------------------------------------------
