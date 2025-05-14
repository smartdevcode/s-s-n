/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "JsonSerializable.hpp"
#include "Order.hpp"
#include "decimal.hpp"

#include <deque>
#include <list>

//-------------------------------------------------------------------------

class OrderContainer;

//-------------------------------------------------------------------------

class TickContainer
    : public std::list<LimitOrder::Ptr>,
      public JsonSerializable,
      public CheckpointSerializable
{
public:
    using ContainerType = std::list<value_type>;

    TickContainer(OrderContainer* orderContainer, taosim::decimal_t price) noexcept;

    [[nodiscard]] taosim::decimal_t price() const noexcept { return m_price; }
    [[nodiscard]] taosim::decimal_t volume() const noexcept { return m_volume; }

    void updateVolume(taosim::decimal_t deltaVolume) noexcept;

    // TODO: Remove.
    [[nodiscard]] taosim::decimal_t totalVolume() const noexcept
    {
        taosim::decimal_t volume{};
        for (const auto tick : *this) {
            volume += tick->totalVolume();
        }
        return volume;
    };

    bool operator<(const TickContainer& rhs) const noexcept { return m_price < rhs.price(); }
    bool operator<(taosim::decimal_t price) const noexcept { return m_price < price; }

    void push_back(const value_type& elem);
    void pop_front();

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

private:
    OrderContainer* m_orderContainer;
    taosim::decimal_t m_price;
    taosim::decimal_t m_volume{};
};

//-------------------------------------------------------------------------
