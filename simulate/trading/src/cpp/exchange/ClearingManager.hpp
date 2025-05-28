/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "AccountRegistry.hpp"
#include "OrderPlacementValidator.hpp"
#include "taosim/exchange/FeePolicyWrapper.hpp"

#include <map>
#include <memory>
#include <set>
#include <variant>

//-------------------------------------------------------------------------

class MultiBookExchangeAgent;

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

struct MarketOrderDesc
{
    std::variant<AgentId, LocalAgentId> agentId;
    PlaceOrderMarketPayload::Ptr payload;
};

struct LimitOrderDesc
{
    std::variant<AgentId, LocalAgentId> agentId;
    PlaceOrderLimitPayload::Ptr payload;
};

using OrderDesc = std::variant<
    MarketOrderDesc,
    LimitOrderDesc>;

struct CancelOrderDesc
{
    BookId bookId;
    LimitOrder::Ptr order;
    decimal_t volumeToCancel;
};

//-------------------------------------------------------------------------

class ClearingManager
{
public:
    struct MarginCallContext
    {
        OrderID orderId;
        AgentId agentId;
    };

    using MarginCallContainer = std::map<BookId, std::map<decimal_t, std::vector<MarginCallContext>>>;

    explicit ClearingManager(
        MultiBookExchangeAgent* exchange,
        std::unique_ptr<FeePolicyWrapper> feePolicy,
        OrderPlacementValidator::Parameters validatorParams) noexcept;

    [[nodiscard]] MultiBookExchangeAgent* exchange() noexcept;
    [[nodiscard]] accounting::AccountRegistry& accounts() noexcept;
    [[nodiscard]] MarginCallContainer& getMarginBuys() { return m_marginBuy; };
    [[nodiscard]] MarginCallContainer& getMarginSells() { return m_marginSell; };
    
    [[nodiscard]] FeePolicyWrapper* feePolicy() { return m_feePolicy.get(); };

    [[nodiscard]] OrderErrorCode handleOrder(const OrderDesc& orderDesc);
    void handleCancelOrder(const CancelOrderDesc& cancelDesc);
    Fees handleTrade(const TradeDesc& tradeDesc);
    void updateFeeTiers(Timestamp time) noexcept;

private:
    MultiBookExchangeAgent* m_exchange;
    std::unique_ptr<FeePolicyWrapper> m_feePolicy;
    MarginCallContainer m_marginBuy;
    MarginCallContainer m_marginSell;
    OrderPlacementValidator m_orderPlacementValidator;

    void removeMarginOrders(
        BookId bookId, OrderDirection direction, std::span<std::pair<OrderID, taosim::decimal_t>> ids);
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------