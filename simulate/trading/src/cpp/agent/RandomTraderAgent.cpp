/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "RandomTraderAgent.hpp"

#include "ExchangeAgentMessagePayloads.hpp"
#include "MessagePayload.hpp"
#include "Simulation.hpp"

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/random.hpp>
#include <boost/random/laplace_distribution.hpp>
#include <unsupported/Eigen/NonLinearOptimization>

#include <algorithm>

//-------------------------------------------------------------------------

namespace taosim::agent
{

//-------------------------------------------------------------------------

namespace br = boost::random;

//-------------------------------------------------------------------------

RandomTraderAgent::RandomTraderAgent(Simulation* simulation) noexcept
    : Agent{simulation}
{}

//-------------------------------------------------------------------------

void RandomTraderAgent::configure(const pugi::xml_node& node)
{
    Agent::configure(node);

    pugi::xml_attribute attr;
    static constexpr auto ctx = std::source_location::current().function_name();

    if (attr = node.attribute("exchange"); attr.empty()) {
        throw std::invalid_argument(fmt::format(
            "{}: missing required attribute 'exchange'", ctx));
    }
    m_exchange = attr.as_string();

    if (simulation()->exchange() == nullptr) {
        throw std::runtime_error(fmt::format(
            "{}: exchange must be configured a priori", ctx));
    }
    m_bookCount = simulation()->exchange()->books().size();
    m_topLevel = std::vector<TopLevel>(m_bookCount, TopLevel{});
    m_orderFlag = std::vector<bool>(m_bookCount, false);

    if (attr = node.attribute("tau"); attr.empty() || attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'tau' should have a value greater than 0", ctx));
    }
    m_tau = attr.as_double();
}

//-------------------------------------------------------------------------

void RandomTraderAgent::receiveMessage(Message::Ptr msg)
{
    if (msg->type == "EVENT_SIMULATION_START") {
        handleSimulationStart();
    }
    else if (msg->type == "EVENT_SIMULATION_STOP") {
        handleSimulationStop();
    }
    else if (msg->type == "RESPONSE_SUBSCRIBE_EVENT_TRADE") {
        handleTradeSubscriptionResponse();
    }
    else if (msg->type == "RESPONSE_RETRIEVE_L1") {
        handleRetrieveL1Response(msg);
    }
    else if (msg->type == "RESPONSE_PLACE_ORDER_LIMIT") {
        handleLimitOrderPlacementResponse(msg);
    }
    else if (msg->type == "ERROR_RESPONSE_PLACE_ORDER_LIMIT") {
        handleLimitOrderPlacementErrorResponse(msg);
    }
    else if (msg->type == "RESPONSE_CANCEL_ORDERS") {
        handleCancelOrdersResponse(msg);
    }
    else if (msg->type == "ERROR_RESPONSE_CANCEL_ORDERS") {
        handleCancelOrdersErrorResponse(msg);
    }
    else if (msg->type == "EVENT_TRADE") {
        handleTrade(msg);
    }
}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleSimulationStart()
{
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        static_cast<Timestamp>(m_tau/3),
        name(),
        m_exchange,
        "SUBSCRIBE_EVENT_TRADE");
}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleSimulationStop()
{}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleTradeSubscriptionResponse()
{
    for (BookId bookId = 0; bookId < m_bookCount; ++bookId) {
        simulation()->dispatchMessage(
            simulation()->currentTimestamp(),
            1,
            name(),
            m_exchange,
            "RETRIEVE_L1",
            MessagePayload::create<RetrieveL1Payload>(bookId));
    }
}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleRetrieveL1Response(Message::Ptr msg)
{

    // Seed the random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::lognormal_distribution<> lognormal_dist(0, 1); // Default mean=0, stddev=1
    std::normal_distribution<> normal_dist(0, 1); // Default mean=0, stddev=1

    const auto payload = std::dynamic_pointer_cast<RetrieveL1ResponsePayload>(msg->payload);

    const BookId bookId = payload->bookId;

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        1,
        name(),
        m_exchange,
        "RETRIEVE_L1",
        MessagePayload::create<RetrieveL1Payload>(bookId));

    auto& topLevel = m_topLevel.at(bookId);
    topLevel.bid = taosim::util::decimal2double(payload->bestBidPrice);
    topLevel.ask = taosim::util::decimal2double(payload->bestAskPrice);

    if (m_orderFlag.at(bookId) || topLevel.bid == 0.0 || topLevel.ask == 0.0) return;

    const double midPrice = (topLevel.bid + topLevel.ask) / 2;
    double noisePrice = normal_dist(gen);
    if (noisePrice < -10.)
        noisePrice = -9.2343;
    if (noisePrice > 10.)
        noisePrice = 9.2353;
    const double limitPrice = midPrice + noisePrice;

    double limitVolume = lognormal_dist(gen) / 3;
    if (limitVolume > .2)
        limitVolume = 0.19876543;
    if (limitVolume < 0.)
        limitVolume = 0.13343;

    const OrderDirection direction = (noisePrice > 0.) ? OrderDirection::SELL : OrderDirection::BUY;

    double leverage = lognormal_dist(gen) / 11;
    if (leverage > 1.)
        leverage = 0.98;
    leverage += 3.;

    sendOrder(bookId, direction, limitVolume, limitPrice, leverage);
}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleLimitOrderPlacementResponse(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<PlaceOrderLimitResponsePayload>(msg->payload);

    // simulation()->dispatchMessage(
    //     simulation()->currentTimestamp(),
    //     m_tau,
    //     name(),
    //     m_exchange,
    //     "CANCEL_ORDERS",
    //     MessagePayload::create<CancelOrdersPayload>(
    //         std::vector{Cancellation(payload->id)}, payload->requestPayload->bookId));

    m_orderFlag.at(payload->requestPayload->bookId) = false;
}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleLimitOrderPlacementErrorResponse(Message::Ptr msg)
{
    const auto payload =
        std::dynamic_pointer_cast<PlaceOrderLimitErrorResponsePayload>(msg->payload);

    const BookId bookId = payload->requestPayload->bookId;

    m_orderFlag.at(bookId) = false;
}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleCancelOrdersResponse(Message::Ptr msg)
{}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleCancelOrdersErrorResponse(Message::Ptr msg)
{}

//-------------------------------------------------------------------------

void RandomTraderAgent::handleTrade(Message::Ptr msg)
{}

//-------------------------------------------------------------------------

void RandomTraderAgent::sendOrder(BookId bookId, OrderDirection direction,
    double volume, double price, double leverage) {

    m_orderFlag.at(bookId) = true;

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        1,
        name(),
        m_exchange,
        "PLACE_ORDER_LIMIT",
        MessagePayload::create<PlaceOrderLimitPayload>(
            direction,
            taosim::util::double2decimal(volume),
            taosim::util::double2decimal(price),
            taosim::util::double2decimal(leverage),
            bookId));
}

//-------------------------------------------------------------------------

}  // namespace taosim::agent

//-------------------------------------------------------------------------