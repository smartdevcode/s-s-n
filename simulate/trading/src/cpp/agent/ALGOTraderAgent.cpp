/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "ALGOTraderAgent.hpp"

#include "DistributionFactory.hpp"
#include "Simulation.hpp"

//-------------------------------------------------------------------------

namespace taosim::agent
{

//-------------------------------------------------------------------------

ALGOTraderVolumeStats::ALGOTraderVolumeStats(size_t period)
    : m_period{period}
{
    if (m_period == 0) {
        throw std::invalid_argument{fmt::format(
            "{}: period should be > 0, was {}",
            std::source_location::current().file_name(), m_period)};
    }
}

//-------------------------------------------------------------------------

void ALGOTraderVolumeStats::push(const Trade& trade)
{
    push({.timestamp = trade.timestamp(), .volume = trade.volume()});
}

//-------------------------------------------------------------------------

void ALGOTraderVolumeStats::push(TimestampedVolume timestampedVolume)
{
    auto acc = [&] {
        m_queue.push(timestampedVolume);
        m_rollingSum += timestampedVolume.volume;
    };

    if (m_queue.empty()) [[unlikely]] {
        return acc();
    }

    if (timestampedVolume.timestamp < m_queue.top().timestamp) [[unlikely]] {
        throw std::runtime_error{fmt::format(
            "{}: Attempt adding volume {} with timestamp {} earlier than the top of the queue ({})",
            std::source_location::current().function_name(),
            timestampedVolume.volume, timestampedVolume.timestamp, m_queue.top().timestamp)};
    }

    const bool withinQueueWindow =
        timestampedVolume.timestamp - m_queue.top().timestamp < m_period;
    
    if (!withinQueueWindow) {
        const auto cutoff = timestampedVolume.timestamp - m_period;
        do {
            m_rollingSum -= m_queue.top().volume;
            m_queue.pop();
        } while (!m_queue.empty() && m_queue.top().timestamp <= cutoff);
    }
    
    acc();
}

//-------------------------------------------------------------------------

ALGOTraderVolumeStats ALGOTraderVolumeStats::fromXML(pugi::xml_node node)
{
    const auto period = node.attribute("volumeStatsPeriod").as_ullong();
    if (period == 0) {
        throw std::invalid_argument{fmt::format(
            "{}: attribute 'volumeStatsPeriod' should be > 0, was {}",
            std::source_location::current().file_name(), period)};
    }
    return ALGOTraderVolumeStats{period};
}

//-------------------------------------------------------------------------

ALGOTraderAgent::ALGOTraderAgent(Simulation* simulation) noexcept
    : Agent{simulation}
{}

//-------------------------------------------------------------------------

void ALGOTraderAgent::configure(const pugi::xml_node& node)
{
    static constexpr auto ctx = std::source_location::current().function_name();

    if (simulation()->exchange() == nullptr) {
        throw std::runtime_error{fmt::format(
            "{}: exchange must be configured a priori", ctx)};
    }

    Agent::configure(node);

    m_rng = &simulation()->rng();
    
    if (m_exchange = node.attribute("exchange").as_string(); m_exchange.empty()) {
        throw std::invalid_argument{fmt::format(
            "{}: attribute 'exchange' should be non-empty", ctx)};
    }

    m_bookCount = simulation()->exchange()->books().size();

    auto getProbAttr = [](pugi::xml_node node, const char* attrName) {
        const float val = node.attribute(attrName).as_float();
        if (!(0.0f <= val && val <= 1.0f)) {
            throw std::invalid_argument{fmt::format(
                "{}: attribute '{}' should be within [0,1], was {}",
                ctx, attrName, val)};
        }
        return val;
    };

    m_wakeupProb = getProbAttr(node, "wakeupProb");
    m_buyProb = getProbAttr(node, "buyProb");

    if (auto volumeProp = node.attribute("volumeProp").as_float(); volumeProp <= 0.0f) {
        throw std::invalid_argument{fmt::format(
            "{}: attribute 'volumeProp' should be > 0.0, was {}",
            ctx, m_volumeProp)};
    } else {
        m_volumeProp = decimal_t{volumeProp};
    }

    m_volumeDistribution =
        stats::DistributionFactory::createFromXML(node.child("VolumeDistribution"));

    m_state = [&] {
        std::vector<ALGOTraderState> state;
        for (BookId bookId = 0; bookId < m_bookCount; ++bookId) {
            state.push_back(ALGOTraderState{
                .status = ALGOTraderStatus::ASLEEP,
                .volumeStats = ALGOTraderVolumeStats::fromXML(node),
                .volumeToBeExecuted = 0_dec,
                .direction = OrderDirection::BUY
            });
        }
        return state;
    }();

    if (m_period = node.attribute("period").as_ullong(); m_period == 0) {
        throw std::invalid_argument{fmt::format(
            "{}: attribute 'period' should be > 0, was {}", ctx, m_period)};
    }
}

//-------------------------------------------------------------------------

void ALGOTraderAgent::receiveMessage(Message::Ptr msg)
{
    if (msg->type == "EVENT_SIMULATION_START") {
        handleSimulationStart(msg);
    }
    else if (msg->type == "EVENT_TRADE") {
        handleTrade(msg);
    }
    else if (msg->type == "WAKEUP_ALGOTRADER") {
        handleWakeup(msg);
    }
    else if (msg->type == "RESPONSE_PLACE_ORDER_MARKET") {
        handleMarketOrderResponse(msg);
    }
}

//-------------------------------------------------------------------------

void ALGOTraderAgent::handleSimulationStart(Message::Ptr msg)
{
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        1,
        name(),
        m_exchange,
        "SUBSCRIBE_EVENT_TRADE");
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        m_period,
        name(),
        name(),
        "WAKEUP_ALGOTRADER");
}

//-------------------------------------------------------------------------

void ALGOTraderAgent::handleTrade(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<EventTradePayload>(msg->payload);
    m_state.at(payload->bookId).volumeStats.push(payload->trade);
}

//-------------------------------------------------------------------------

void ALGOTraderAgent::handleWakeup(Message::Ptr msg)
{
    for (BookId bookId = 0; bookId < m_bookCount; ++bookId) {
        auto& state = m_state.at(bookId);
        if (state.status == ALGOTraderStatus::ASLEEP) {
            tryWakeup(bookId, state);
        } else if (state.status == ALGOTraderStatus::EXECUTING) {
            execute(bookId, state);
        }
    }

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        m_period,
        name(),
        name(),
        "WAKEUP_ALGOTRADER");
}

//-------------------------------------------------------------------------

void ALGOTraderAgent::handleMarketOrderResponse(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<PlaceOrderMarketResponsePayload>(msg->payload);
    const auto requestPayload = payload->requestPayload;

    const decimal_t executedVolume = requestPayload->volume;
    auto& state = m_state.at(requestPayload->bookId);
    state.volumeToBeExecuted -= executedVolume;
    
    simulation()->logDebug("{} EXECUTED {}", name(), executedVolume);

    if (state.volumeToBeExecuted == 0_dec) {
        simulation()->logDebug("{} FALLING ASLEEP", name());
        state.status = ALGOTraderStatus::ASLEEP;
    }
}

//-------------------------------------------------------------------------

void ALGOTraderAgent::tryWakeup(BookId bookId, ALGOTraderState& state)
{
    // TODO: Advancing an RNG is not thread-safe, should have separate RNGs for each book.
    if (!std::bernoulli_distribution{m_wakeupProb}(*m_rng)) return;

    const auto& baseBalance = simulation()->account(name()).at(bookId).base;

    state.direction = std::bernoulli_distribution{m_buyProb}(*m_rng)
        ? OrderDirection::BUY : OrderDirection::SELL;

    if (state.direction == OrderDirection::BUY) {
        state.volumeToBeExecuted = util::double2decimal(
            m_volumeDistribution->sample(*m_rng),
            baseBalance.getRoundingDecimals());
    }
    else {
        state.volumeToBeExecuted = std::min(
            util::double2decimal(
                m_volumeDistribution->sample(*m_rng),
                baseBalance.getRoundingDecimals()),
            baseBalance.getFree());
        if (state.volumeToBeExecuted == 0_dec) {
            return;
        }
    }

    state.status = ALGOTraderStatus::EXECUTING;
}

//-------------------------------------------------------------------------

void ALGOTraderAgent::execute(BookId bookId, ALGOTraderState& state)
{
    const auto& baseBalance = simulation()->account(name()).at(bookId).base;

    const decimal_t volumeToExecute = std::min(
        util::round(
            m_volumeProp * state.volumeStats.rollingSum(),
            baseBalance.getRoundingDecimals()),
        state.volumeToBeExecuted);

    simulation()->logDebug(
        "{} ATTEMPTING TO EXECUTE {} OF {}", name(), state.direction, volumeToExecute);

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        1,
        name(),
        m_exchange,
        "PLACE_ORDER_MARKET",
        MessagePayload::create<PlaceOrderMarketPayload>(
            state.direction, volumeToExecute, bookId));
}

//-------------------------------------------------------------------------

}  // namespace taosim::agent

//-------------------------------------------------------------------------
