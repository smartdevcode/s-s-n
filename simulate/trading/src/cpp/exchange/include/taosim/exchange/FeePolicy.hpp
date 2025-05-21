/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Fees.hpp"
#include "Trade.hpp"
#include "common.hpp"

#include <unordered_set>

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

struct TradeDesc
{
    BookId bookId;
    AgentId restingAgentId;
    AgentId aggressingAgentId;
    Trade::Ptr trade;
};

struct Tier
{
    decimal_t volumeRequired;
    decimal_t makerFeeRate;
    decimal_t takerFeeRate;
};

struct FeePolicyDesc
{
    Simulation* simulation;
    int historySlots;
    Timestamp slotPeriod;
    std::vector<Tier> tiers;
};

//-------------------------------------------------------------------------

class FeePolicy
{
public:
    explicit FeePolicy(const FeePolicyDesc& desc);

    virtual Fees calculateFees(const TradeDesc& tradeDesc) const;
    
    void updateAgentsTiers() noexcept;
    void updateHistory(BookId bookId, AgentId agentId, decimal_t volume) noexcept;
    void resetHistory() noexcept;
    void resetHistory(const std::unordered_set<AgentId>& agentIds) noexcept;
    virtual Fees getRates(BookId bookId, AgentId agentId) const noexcept;
    
    [[nodiscard]] static std::unique_ptr<FeePolicy> fromXML(
        pugi::xml_node node, Simulation* simulation);
        
    [[nodiscard]] auto&& historySlots(this auto&& self) noexcept { return self.m_historySlots; }
    [[nodiscard]] auto&& slotPeriod(this auto&& self) noexcept { return self.m_slotPeriod; }
    [[nodiscard]] auto&& tiers(this auto&& self) noexcept { return self.m_tiers; }
    [[nodiscard]] auto&& agentTiers(this auto&& self) noexcept { return self.m_agentTiers; }
    [[nodiscard]] auto&& agentVolumes(this auto&& self) noexcept { return self.m_agentVolumes; }
        
protected:
    using TierIdx = int32_t;

    [[nodiscard]] const Tier& findTierForVolume(decimal_t volume) const noexcept;
    [[nodiscard]] const Tier& findTierForAgent(BookId bookId, AgentId agentId) const noexcept;

    Simulation* m_simulation;
    int m_historySlots;
    Timestamp m_slotPeriod;
    std::vector<Tier> m_tiers;
    std::map<AgentId, std::map<BookId, TierIdx>> m_agentTiers;
    std::map<AgentId, std::map<BookId, std::vector<decimal_t>>> m_agentVolumes;

    static decimal_t checkFeeRate(double feeRate);
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
