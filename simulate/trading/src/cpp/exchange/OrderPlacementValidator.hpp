/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Account.hpp"
#include "Book.hpp"
#include "ExchangeAgentMessagePayloads.hpp"
#include "Order.hpp"
#include "taosim/exchange/FeePolicyWrapper.hpp"

#include <expected>

//-------------------------------------------------------------------------

class MultiBookExchangeAgent;

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

class OrderPlacementValidator
{
public:
    struct Result
    {
        OrderDirection direction;
        decimal_t amount;
        decimal_t leverage;
    };

    struct Parameters
    {
        uint32_t volumeIncrementDecimals = 8;
        uint32_t priceIncrementDecimals = 4;
        uint32_t baseIncrementDecimals = 8;
        uint32_t quoteIncrementDecimals = 10;
    };

    using ExpectedResult = std::expected<Result, OrderErrorCode>;

    OrderPlacementValidator(const Parameters& params, MultiBookExchangeAgent* exchange) noexcept;

    [[nodiscard]] auto& parameters(this auto&& self) noexcept { return self.m_params; }

    [[nodiscard]] ExpectedResult validateMarketOrderPlacement(
        const accounting::Account& account,
        Book::Ptr book,
        PlaceOrderMarketPayload::Ptr payload,
        FeePolicyWrapper& feePolicy,
        decimal_t maxLeverage,
        decimal_t maxLoan,
        AgentId agentId) const;

    [[nodiscard]] ExpectedResult validateLimitOrderPlacement(
        const accounting::Account& account,
        Book::Ptr book,
        PlaceOrderLimitPayload::Ptr payload,
        FeePolicyWrapper& feePolicy,
        decimal_t maxLeverage,
        decimal_t maxLoan,
        AgentId agentId) const;

private:
    [[nodiscard]] bool checkTimeInForce(
        Book::Ptr book, PlaceOrderLimitPayload::Ptr payload, AgentId agentId) const noexcept;
    [[nodiscard]] bool checkIOC(
        Book::Ptr book, PlaceOrderLimitPayload::Ptr payload, AgentId agentId) const noexcept;
    [[nodiscard]] bool checkFOK(
        Book::Ptr book, PlaceOrderLimitPayload::Ptr payload, AgentId agentId) const noexcept;
    [[nodiscard]] bool checkPostOnly(
        Book::Ptr book, PlaceOrderLimitPayload::Ptr payload, AgentId agentId) const noexcept;

    Parameters m_params;
    MultiBookExchangeAgent* m_exchange;
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
