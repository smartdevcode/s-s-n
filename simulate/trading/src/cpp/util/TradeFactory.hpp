/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "Trade.hpp"
#include "common.hpp"

//-------------------------------------------------------------------------

class TradeFactory : public CheckpointSerializable
{
public:
    TradeFactory() noexcept = default;

    template<typename... Args>
    requires std::constructible_from<Trade, TradeID, Args...>
    [[nodiscard]] Trade::Ptr makeRecord(Args&&... args) const noexcept
    {
        const auto trade = Trade::create(m_idCounter++, std::forward<Args>(args)...);
        return trade;
    }

    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    
    [[nodiscard]] TradeFactory fromJson(const rapidjson::Value& json);

private:
    mutable TradeID m_idCounter{};

    friend class Simulation;
};

//-------------------------------------------------------------------------
