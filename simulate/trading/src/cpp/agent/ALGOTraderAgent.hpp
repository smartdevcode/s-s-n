/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Agent.hpp"
#include "Distribution.hpp"
#include "Order.hpp"
#include "Trade.hpp"
#include "decimal.hpp"
#include <boost/math/distributions/rayleigh.hpp>

#include <memory>
#include <queue>
#include <random>

//-------------------------------------------------------------------------

namespace taosim::agent
{

//-------------------------------------------------------------------------

enum class ALGOTraderStatus : uint32_t
{
    ASLEEP,
    EXECUTING
};

struct ALGOTraderExecutionInfo
{
    OrderDirection dir;
    decimal_t volumeToBeExecuted;
};

struct TimestampedVolume
{
    Timestamp timestamp;
    decimal_t volume;

    [[nodiscard]] auto operator<=>(const TimestampedVolume& other) const noexcept
    {
        return timestamp <=> other.timestamp;
    }
};

class ALGOTraderVolumeStats
{
public:
    explicit ALGOTraderVolumeStats(Timestamp period = 60);

    void push(const Trade& trade);
    void push(TimestampedVolume timestampedVolume);
    
    [[nodiscard]] decimal_t rollingSum() const noexcept { return m_rollingSum; }

    [[nodiscard]] static ALGOTraderVolumeStats fromXML(pugi::xml_node node);

private:
    Timestamp m_period;
    std::priority_queue<
        TimestampedVolume,
        std::vector<TimestampedVolume>,
        std::greater<TimestampedVolume>> m_queue;
    decimal_t m_rollingSum{};
};

struct ALGOTraderState
{
    ALGOTraderStatus status;
    ALGOTraderVolumeStats volumeStats;
    decimal_t volumeToBeExecuted;
    OrderDirection direction;
};

//-------------------------------------------------------------------------

class ALGOTraderAgent : public Agent
{
public:
    explicit ALGOTraderAgent(Simulation* simulation) noexcept;

    virtual void configure(const pugi::xml_node& node) override;
    virtual void receiveMessage(Message::Ptr msg) override;

private:
    struct DelayBounds
    {
        Timestamp min, max;
    };

    void handleSimulationStart(Message::Ptr msg);
    void handleTrade(Message::Ptr msg);
    void handleWakeup(Message::Ptr msg);
    void handleMarketOrderResponse(Message::Ptr msg);

    void tryWakeup(BookId bookId, ALGOTraderState& state);
    void execute(BookId bookId, ALGOTraderState& state);
    
    Timestamp orderPlacementLatency();

    std::mt19937* m_rng;
    std::string m_exchange;
    uint32_t m_bookCount;
    float m_wakeupProb;
    float m_buyProb;
    decimal_t m_volumeProp;
    std::unique_ptr<stats::Distribution> m_volumeDistribution;
    std::vector<ALGOTraderState> m_state;
    Timestamp m_period;

    double m_opLatencyScaleRay;
    DelayBounds m_opl;
    boost::math::rayleigh_distribution<double> m_orderPlacementLatencyDistribution;
    std::uniform_real_distribution<double> m_placementDraw;


};

//-------------------------------------------------------------------------

}  // namespace taosim::agent

//-------------------------------------------------------------------------
