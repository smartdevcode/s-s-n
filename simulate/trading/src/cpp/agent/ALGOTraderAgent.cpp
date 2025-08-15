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
    
    pugi::xml_attribute attr;
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



    m_volumeDistribution =
        stats::DistributionFactory::createFromXML(node.child("VolumeDistribution"));

    m_state = [&] {
        std::vector<ALGOTraderState> state;
        for (BookId bookId = 0; bookId < m_bookCount; ++bookId) {
            state.push_back(ALGOTraderState{
                .status = ALGOTraderStatus::ASLEEP,
                .volumeToBeExecuted = 0_dec,
                .direction = OrderDirection::BUY
            });
        }
        return state;
    }();


    m_marketFeedLatencyDistribution = std::normal_distribution<double>{
        [&] {
            static constexpr const char* name = "MFLmean";
            auto attr = node.attribute(name);
            return attr.empty() ? 1'000'000'000.0 : attr.as_double(); 
        }(),
        [&] {
            static constexpr const char* name = "MFLstd";
            auto attr = node.attribute(name);
            return attr.empty() ? 1'000'000'000.0 : attr.as_double(); 
        }()
    };

    if (attr = node.attribute("minOPLatency"); attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'minOPLatency' should have a value greater than 0", ctx));
    }
     m_opl.min = attr.as_ullong();
    if (attr = node.attribute("maxOPLatency"); attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'maxOPLatency' should have a value greater than 0", ctx));
    }
    m_opl.max = attr.as_ullong();
    if (m_opl.min >= m_opl.max) {
        throw std::invalid_argument(fmt::format(
            "{}: minOP ({}) should be strictly less maxOP ({})", ctx, m_opl.min, m_opl.max));
    }
    attr = node.attribute("opLatencyScaleRay"); 
    const double scale = (attr.empty() || attr.as_double() == 0.0) ? 0.235 : attr.as_double();
    m_orderPlacementLatencyDistribution = boost::math::rayleigh_distribution<double>{scale};
    const double percentile = 1-std::exp(-1/(2*scale*scale));
    m_placementDraw = std::uniform_real_distribution<double>{0.0, percentile};
    m_lastPrice =  std::vector<decimal_t>(m_bookCount, simulation()->exchange()->config2().initialPrice);

    attr = node.attribute("updateInterval");
    m_delay = (attr.empty() || attr.as_ullong() == 0) ? 300'000'000'000 : attr.as_ullong();

    attr = node.attribute("volumeDrawRayleighScale");
    const double scale2 = (attr.empty() || attr.as_double() == 0.0) ? 10'000.0 : attr.as_double();
    m_volumeDrawDistribution = boost::math::rayleigh_distribution<double>{scale2};
    m_volumeDraw = std::uniform_real_distribution<double>{0.0, 1.0};

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
        // Add the Delay as variable
    Timestamp initDelay = m_delay; 
    if (simulation()->currentTimestamp() != static_cast<Timestamp>(0)) {
        fmt::println("Initial timestamp is not zero");
        initDelay -= simulation()->currentTimestamp();
    } 
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
            initDelay,
            name(),
            name(),
            "WAKEUP_ALGOTRADER");
}



//-------------------------------------------------------------------------

void ALGOTraderAgent::handleTrade(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<EventTradePayload>(msg->payload);
    const BookId bookId = payload->bookId;
    m_lastPrice.at(bookId) = payload->trade.price();
}



void ALGOTraderAgent::handleWakeup(Message::Ptr msg)
{
    const uint64_t processCount = getProcessCount(0, "algotrigger");
    double newProcessValue = 0.0; 
    
    if (m_lastTriggerUpdate != processCount) {
        newProcessValue = getProcessValue(0, "algotrigger"); 
        m_lastTriggerUpdate = processCount;
    } 

    for (BookId bookId = 0; bookId < m_bookCount; ++bookId) {
        const auto& balances =  simulation()->account(name()).at(bookId);
        const auto& baseBalance = balances.base;
    
        auto& state = m_state.at(bookId);

        const double fundamental = getProcessValue(bookId, "fundamental");
        if (newProcessValue != 0 && util::double2decimal(fundamental) >= m_lastPrice.at(bookId)) {
            const decimal_t volumeToBeExecuted = drawNewVolume(balances.m_baseDecimals);
            state.status = ALGOTraderStatus::EXECUTING;
            state.direction = OrderDirection::BUY;
            state.marketFeedLatency = static_cast<Timestamp>(0);
            state.volumeToBeExecuted = std::min(volumeToBeExecuted,
               balances.quote.getFree()*m_lastPrice.at(bookId));
        } else if (newProcessValue != 0 && util::double2decimal(fundamental) <= m_lastPrice.at(bookId)) {
            const decimal_t volumeToBeExecuted = drawNewVolume(balances.m_baseDecimals);
            state.status = ALGOTraderStatus::EXECUTING;
            state.direction = OrderDirection::SELL;
            state.marketFeedLatency = static_cast<Timestamp>(0);
            state.volumeToBeExecuted = std::min(volumeToBeExecuted,
                                        baseBalance.getFree());
        }
        if (state.status == ALGOTraderStatus::EXECUTING) {
            execute(bookId, state);
        } 
    }

    simulation()->dispatchMessage(
            simulation()->currentTimestamp(),
            m_delay,
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
    const BookId bookId = requestPayload->bookId;
    auto& state = m_state.at(bookId);
    state.volumeToBeExecuted -= executedVolume;
    
    simulation()->logDebug("{} EXECUTED {}", name(), executedVolume);

    if (state.volumeToBeExecuted <= 1_dec) {
        simulation()->logDebug("{} FALLING ASLEEP", name());
        state.status = ALGOTraderStatus::ASLEEP; 
    } else {
        state.marketFeedLatency = static_cast<Timestamp>(std::min(
            std::abs(m_marketFeedLatencyDistribution(*m_rng)),
            m_marketFeedLatencyDistribution.mean()
            + 3.0 * m_marketFeedLatencyDistribution.stddev()));
        execute(bookId, state);
    }
}
//-------------------------------------------------------------------------

void ALGOTraderAgent::execute(BookId bookId, ALGOTraderState& state)
{
    const auto& balances = simulation()->account(name()).at(bookId) ;
    const auto& baseBalance = balances.base;
    const decimal_t volume = std::min(util::double2decimal(
                                        m_volumeDistribution->sample(*m_rng),
                                        balances.m_baseDecimals),
                                        state.volumeToBeExecuted);
    const decimal_t volumeToExecute = state.direction == OrderDirection::BUY ? 
    std::min(volume, balances.quote.getFree()*m_lastPrice.at(bookId))
        : std::min(volume, baseBalance.getFree());


    simulation()->logDebug(
        "{} ATTEMPTING TO EXECUTE {} OF {}", name(), state.direction, volumeToExecute);

    simulation()->dispatchMessage( 
        simulation()->currentTimestamp(),
        orderPlacementLatency() + state.marketFeedLatency,
        name(),
        m_exchange,
        "PLACE_ORDER_MARKET",
        MessagePayload::create<PlaceOrderMarketPayload>(
            state.direction, volumeToExecute, bookId));
}
//-------------------------------------------------------------------------
decimal_t ALGOTraderAgent::drawNewVolume(uint32_t baseDecimals) {
        const double rayleighDraw = boost::math::quantile(m_volumeDrawDistribution, m_volumeDraw(*m_rng));
        return  util::double2decimal(rayleighDraw,baseDecimals);
}
//-------------------------------------------------------------------------

Timestamp ALGOTraderAgent::orderPlacementLatency() {
    const double rayleighDraw = boost::math::quantile(m_orderPlacementLatencyDistribution, m_placementDraw(*m_rng));
    return static_cast<Timestamp>(std::lerp(m_opl.min, m_opl.max, rayleighDraw));
}

//-------------------------------------------------------------------------
double ALGOTraderAgent::getProcessValue(BookId bookId, const std::string& name)
{
    return simulation()->exchange()->process(name, bookId)->value();
}


//-------------------------------------------------------------------------
uint64_t ALGOTraderAgent::getProcessCount(BookId bookId, const std::string& name)
{
    return simulation()->exchange()->process(name, bookId)->count();
}
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

}  // namespace taosim::agent

//-------------------------------------------------------------------------
