/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "taosim/exchange/FeePolicyWrapper.hpp"

#include <mutex>

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

FeePolicyWrapper::FeePolicyWrapper(
    std::unique_ptr<FeePolicy> feePolicy,
    accounting::AccountRegistry* accountRegistry) noexcept
    : m_feePolicy{std::move(feePolicy)},
      m_accountRegistry{accountRegistry},
      m_mtx{std::make_unique<std::shared_mutex>()}
{}

//-------------------------------------------------------------------------

Fees FeePolicyWrapper::calculateFees(const TradeDesc& tradeDesc)
{
    std::shared_lock lock{*m_mtx};
    const auto [bookId, restingAgentId, aggressingAgentId, trade] = tradeDesc;
    const auto volumeWeightedPrice = trade->volume() * trade->price();
    return {
        .maker = getRates(bookId, restingAgentId).maker * volumeWeightedPrice,
        .taker = getRates(bookId, aggressingAgentId).taker * volumeWeightedPrice
    };
}

//-------------------------------------------------------------------------

Fees FeePolicyWrapper::getRates(BookId bookId, AgentId agentId) noexcept
{
    std::shared_lock lock{*m_mtx};
    const auto agentBaseName = m_accountRegistry->getAgentBaseName(agentId);
    if (agentBaseName.has_value()) {
        auto it = m_agentBaseNameFeePolicies.find(agentBaseName.value());
        if (it != m_agentBaseNameFeePolicies.end()) {
            return it->second->getRates(bookId, agentId);
        }
    }
    return m_feePolicy->getRates(bookId, agentId);
}

//-------------------------------------------------------------------------

decimal_t FeePolicyWrapper::agentVolume(BookId bookId, AgentId agentId) const noexcept
{
    std::unique_lock lock{*m_mtx};
    const auto agentBaseName = m_accountRegistry->getAgentBaseName(agentId);
    if (agentBaseName.has_value()) {
        auto agentFeePolicyIt = m_agentBaseNameFeePolicies.find(agentBaseName.value().get());
        if (agentFeePolicyIt != m_agentBaseNameFeePolicies.end()) {
            const auto& agentFeePolicy = agentFeePolicyIt->second;
            auto agentIdIt = agentFeePolicy->agentVolumes().find(agentId);
            if (agentIdIt != agentFeePolicy->agentVolumes().end()) {
                const auto& bookIdToVolumeHistory = agentIdIt->second;
                auto bookIdIt = bookIdToVolumeHistory.find(bookId);
                if (bookIdIt != bookIdToVolumeHistory.end()) {
                    return ranges::accumulate(bookIdIt->second, 0_dec);
                }
            }
        }
    }
    auto agentIdIt = m_feePolicy->agentVolumes().find(agentId);
    if (agentIdIt == m_feePolicy->agentVolumes().end()) return 0_dec;
    const auto& bookIdToVolumeHistory = agentIdIt->second;
    auto bookIdIt = bookIdToVolumeHistory.find(bookId);
    if (bookIdIt == bookIdToVolumeHistory.end()) return 0_dec;
    return ranges::accumulate(bookIdIt->second, 0_dec);
}

//-------------------------------------------------------------------------

bool FeePolicyWrapper::contains(const std::string& agentBaseName) const noexcept
{
    std::shared_lock lock{*m_mtx};
    return m_agentBaseNameFeePolicies.contains(agentBaseName);
}

//-------------------------------------------------------------------------

void FeePolicyWrapper::updateAgentsTiers(Timestamp time) noexcept
{
    std::unique_lock lock{*m_mtx};
    ranges::for_each(
        policiesView()
        | views::filter([time](auto feePolicy) { return time % feePolicy->slotPeriod() == 0; }),
        [time](auto feePolicy) { feePolicy->updateAgentsTiers(); });
}

//-------------------------------------------------------------------------

void FeePolicyWrapper::updateHistory(BookId bookId, AgentId agentId, decimal_t volume) noexcept
{
    std::unique_lock lock{*m_mtx};
    for (auto feePolicy : policiesView()) {
        feePolicy->updateHistory(bookId, agentId, volume);
    }
}

//-------------------------------------------------------------------------

void FeePolicyWrapper::resetHistory() noexcept
{
    std::unique_lock lock{*m_mtx};
    for (auto feePolicy : policiesView()) {
        feePolicy->resetHistory();
    }
}

//-------------------------------------------------------------------------

void FeePolicyWrapper::resetHistory(const std::unordered_set<AgentId>& agentIds) noexcept
{
    std::unique_lock lock{*m_mtx};
    for (auto feePolicy : policiesView()) {
        feePolicy->resetHistory(agentIds);
    }
}

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
