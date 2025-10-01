/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/accounting/AccountRegistry.hpp"
#include "taosim/exchange/FeePolicy.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

class FeePolicyWrapper
{
public:
    FeePolicyWrapper(
        std::unique_ptr<FeePolicy> feePolicy,
        accounting::AccountRegistry* accountRegistry) noexcept;

    auto&& operator[](this auto&& self, const std::string& agentBaseName)
    { 
        return self.m_agentBaseNameFeePolicies[agentBaseName];
    }

    [[nodiscard]] Fees calculateFees(const TradeDesc& trade);
    [[nodiscard]] Fees getRates(BookId bookId, AgentId agentId) const noexcept;
    [[nodiscard]] decimal_t agentVolume(BookId bookId, AgentId agentId) const noexcept;

    [[nodiscard]] bool contains(const std::string& agentBaseName) const noexcept;
    
    FeePolicy* defaultPolicy() noexcept { return m_feePolicy.get(); }
    void updateAgentsTiers(Timestamp time) noexcept;
    void updateHistory(BookId bookId, AgentId agentId, decimal_t volume) noexcept;
    void resetHistory() noexcept;
    void resetHistory(const std::unordered_set<AgentId>& agentIds) noexcept;

private:
    [[nodiscard]] decltype(auto) policiesView() const noexcept
    {
        return views::concat(
            views::values(m_agentBaseNameFeePolicies)
            | views::transform([](auto& feePolicy) { return feePolicy.get(); }),
            views::single(m_feePolicy.get()));
    }

    accounting::AccountRegistry* m_accountRegistry;
    std::map<std::string, std::unique_ptr<FeePolicy>> m_agentBaseNameFeePolicies;
    std::unique_ptr<FeePolicy> m_feePolicy;
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
