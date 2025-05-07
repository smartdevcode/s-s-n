/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "Order.hpp"
#include "common.hpp"

//-------------------------------------------------------------------------

class OrderFactory : public CheckpointSerializable
{
public:
    OrderFactory() noexcept = default;

    [[nodiscard]] OrderID getCounterState() const noexcept { return m_idCounter; }

    [[nodiscard]] MarketOrder::Ptr makeMarketOrder(
        OrderDirection direction,
        Timestamp timestamp,
        taosim::decimal_t volume,
        taosim::decimal_t leverage = 0_dec) const noexcept;

    [[nodiscard]] LimitOrder::Ptr makeLimitOrder(
        OrderDirection direction,
        Timestamp timestamp,
        taosim::decimal_t volume,
        taosim::decimal_t price,
        taosim::decimal_t leverage = 0_dec) const noexcept;

    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] OrderFactory fromJson(const rapidjson::Value& json);

private:
    mutable OrderID m_idCounter{};

    friend class Simulation;
};

//-------------------------------------------------------------------------
