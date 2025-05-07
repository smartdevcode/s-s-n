/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "HighFrequencyTraderAgent.hpp"
#include "Simulation.hpp"

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/random.hpp>
#include <boost/random/laplace_distribution.hpp>
#include <boost/math/distributions/rayleigh.hpp>
#include <unsupported/Eigen/NonLinearOptimization>

#include <algorithm>
#include <regex>




HighFrequencyTraderAgent::HighFrequencyTraderAgent(Simulation *simulation) noexcept
    : Agent{simulation}
{}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::configure(const pugi::xml_node &node)
{
    Agent::configure(node);
    m_rng = &simulation()->rng();
    m_wealthFrac = 1.0;

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

    if (attr = node.attribute("tau"); attr.empty() || attr.as_double() == 0.0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'tau' should have a value greater than 0.0", ctx));
    }
    m_tau = static_cast<Timestamp>(attr.as_ullong());

    if (attr = node.attribute("delta"); attr.empty() || attr.as_double() == 0.0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'delta' should have a value greater than 0.0", ctx));
    }
    m_delta = attr.as_double();

    if (attr = node.attribute("gHFT"); attr.empty() || attr.as_double() == 0.0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'gHFT' should have a value greater than 0.0", ctx));
    }
    m_gHFT = attr.as_double();

    if (attr = node.attribute("price0"); attr.empty() || attr.as_double() == 0.0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'price0' should have a value greater than 0.0", ctx));
    }
    m_priceInit = attr.as_double();
    if (attr = node.attribute("minOPLatency"); attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'minOPLatency' should have a value greater than 0", ctx));
    }
    m_opLatency.min = attr.as_ullong();
    if (attr = node.attribute("maxOPLatency"); attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'maxOPLatency' should have a value greater than 0", ctx));
    }
    m_opLatency.max = attr.as_ullong();
    if (m_opLatency.min >= m_opLatency.max) {
        throw std::invalid_argument(fmt::format(
            "{}: minD ({}) should be strictly less maxD ({})", ctx, m_opLatency.min, m_opLatency.max));
    }

    if (attr = node.attribute("psiHFT_constant"); attr.empty()) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'psiHFT_constant' should have a value greater than or equal to 0.0", ctx));
    }
    m_psi = attr.as_double();
    m_orderFlag = std::vector<bool>(m_bookCount, false);
    m_topLevel = std::vector<TopLevel>(m_bookCount, TopLevel{});
    m_baseFree = std::vector<double>(m_bookCount, 0.);
    m_quoteFree = std::vector<double>(m_bookCount, 0.);
    m_inventory = std::vector<double>(m_bookCount, 0.);
    m_deltaHFT = std::vector<double>(m_bookCount, 0.);
    m_tauHFT = std::vector<Timestamp>(m_bookCount, Timestamp{});
    //---------------------------------------------------------------------------------
    
    if (attr = node.attribute("GBM_X0"); attr.empty() || attr.as_double() <= 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'GBM_X0' should have a value greater than 0.0f", ctx));
    }
    const double gbmX0 = attr.as_double();
    if (attr = node.attribute("GBM_mu"); attr.empty() || attr.as_double() < 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'GBM_mu' should have a value of at least 0.0f", ctx));
    }
    const double gbmMu = attr.as_double();
    if (attr = node.attribute("GBM_sigma"); attr.empty() || attr.as_double() < 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'GBM_sigma' should have a value of at least 0.0f", ctx));
    }
    const double gbmSigma = attr.as_double();
    if (attr = node.attribute("GBM_seed"); attr.empty()) {
        throw std::invalid_argument(fmt::format(
            "{}: missing required attribute 'GBM_seed", ctx));
    }
    const uint64_t gbmSeed = attr.as_ullong();
    for (BookId bookId = 0; bookId < m_bookCount; ++bookId) {
        GBMValuationModel gbmPrice{gbmX0, gbmMu, gbmSigma, gbmSeed * (bookId + 1)}; //#
        // should be adapted, duration is not efficient
        const Timestamp adjustedDuration = static_cast<Timestamp>(simulation()->duration() / std::pow(10, 9));
        const auto Xt = gbmPrice.generatePriceSeries(1, adjustedDuration);
        m_priceHist.push_back([&] {
            decltype(m_priceHist)::value_type hist{adjustedDuration};
            for (uint32_t i = 0; i < adjustedDuration; ++i) {
                hist.push_back(m_priceInit * (1.0 + Xt[i]));
            }
            return hist;
        }());
        m_logReturns.push_back([&] {
            decltype(m_logReturns)::value_type logReturns{adjustedDuration};
            const auto& priceHist = m_priceHist.at(bookId);
            logReturns.push_back(Xt[0]);
            for (uint32_t i = 1; i < priceHist.capacity(); ++i) {
                logReturns.push_back(std::log(priceHist[i] / priceHist[i - 1]));
            }
            return logReturns;
        }());
    }

    m_opLatencyScaleRay = node.attribute("opLatencyScaleRay").as_double();
    m_orderMean = node.attribute("orderMean").as_double();
    m_noiseRay = node.attribute("noiseRay").as_double();
    m_minMFLatency = node.attribute("minMFLatency").as_ullong();
    m_shiftPercentage = node.attribute("shiftPercentage").as_double();

    m_sigmaScalingBase = node.parent().child("MultiBookExchangeAgent").child("Books")
        .child("Processes").attribute("updatePeriod").as_llong();
    m_sigmaSqrInit = std::pow(node.parent().child("MultiBookExchangeAgent").child("Books")
        .child("Processes").child("GBM").attribute("sigma").as_double(), 2) * std::pow((m_delta/m_sigmaScalingBase),2);

    m_debug = node.attribute("debug").as_bool();

    m_priceIncrement = 1 / std::pow(10, simulation()->exchange()->config().parameters().priceIncrementDecimals);
    m_volumeIncrement = 1 / std::pow(10, simulation()->exchange()->config().parameters().volumeIncrementDecimals);
    m_maxLeverage = taosim::util::decimal2double(simulation()->exchange()->getMaxLeverage());
    m_maxLoan = taosim::util::decimal2double(simulation()->exchange()->getMaxLoan());

     
    
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::receiveMessage(Message::Ptr msg)
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
    else if (msg->type == "RESPONSE_PLACE_ORDER_Market") {
        handleMarketOrderPlacementResponse(msg);
    }
    else if (msg->type == "ERROR_RESPONSE_PLACE_ORDER_Market") {
        handleMarketOrderPlacementErrorResponse(msg);
    }
    else if (msg->type == "RESPONSE_CANCEL_ORDERS") {
        handleCancelOrdersResponse(msg);
    }
    else if (msg->type == "ERROR_RESPONSE_CANCEL_ORDERS") {
        handleCancelOrdersErrorResponse(msg);
    }
    else if (msg->type == "EVENT_TRADE") {
        handleTrade(msg);
    }else{
        simulation()->logDebug("{}", msg->type);
    }
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleSimulationStart()
{
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        1,
        name(),
        m_exchange,
        "SUBSCRIBE_EVENT_TRADE");
    
    m_id = simulation()->exchange()->accounts().idBimap().left.at(name());
    
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

void HighFrequencyTraderAgent::handleSimulationStop()
{
    simulation()->logDebug("-----The simulation ends now----");
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleTradeSubscriptionResponse()
{
    
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleRetrieveL1Response(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<RetrieveL1ResponsePayload>(msg->payload);
    const BookId bookId = payload->bookId;
    m_deltaHFT[bookId] = m_delta / (1.0 + std::exp(std::abs(m_inventory[bookId]) - m_psi));
    m_tauHFT[bookId] = std::max(
        static_cast<Timestamp>(m_tau * m_minMFLatency),
        static_cast<Timestamp>(std::ceil(m_tau * m_deltaHFT[bookId]))
    );
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        std::max(static_cast<Timestamp>(m_deltaHFT[bookId]), static_cast<Timestamp>(m_minMFLatency)),
        name(),
        m_exchange,
        "RETRIEVE_L1",
        MessagePayload::create<RetrieveL1Payload>(bookId));

    auto& topLevel = m_topLevel.at(bookId);
    topLevel.bid = taosim::util::decimal2double(payload->bestBidPrice);
    topLevel.ask = taosim::util::decimal2double(payload->bestAskPrice);
    
    if (topLevel.bid == 0.0 || topLevel.ask == 0.0) 
        return;

    const double midPrice = (topLevel.bid + topLevel.ask) / 2;

    m_baseFree[bookId] = m_wealthFrac * 
        taosim::util::decimal2double(simulation()->exchange()->account(name()).at(bookId).base.getFree());
    m_quoteFree[bookId] = m_wealthFrac * 
        taosim::util::decimal2double(simulation()->exchange()->account(name()).at(bookId).quote.getFree());
    
    m_pRes = midPrice - m_gHFT * m_inventory[bookId] * m_sigmaSqrInit;

    placeOrder(bookId, topLevel);
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleLimitOrderPlacementResponse(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<PlaceOrderLimitResponsePayload>(msg->payload);
    const BookId bookId = payload->requestPayload->bookId;

    static const std::regex pattern("^HIGH_FREQUENCY_TRADER_AGENT_(?:[0-9]|1[0-9]|20)$");
    if (std::regex_match(name(), pattern)) {
        recordOrder(payload);
    }

    m_deltaHFT[bookId] = m_delta / (1.0 + std::exp(std::abs(m_inventory[bookId]) - m_psi));
    m_tauHFT[bookId] = std::max(
        static_cast<Timestamp>(m_tau * m_minMFLatency),
        static_cast<Timestamp>(std::ceil(m_tau * m_deltaHFT[bookId]))
    );

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        m_tauHFT[bookId],
        name(),
        m_exchange,
        "CANCEL_ORDERS",
        MessagePayload::create<CancelOrdersPayload>(
            std::vector{Cancellation(payload->id)}, payload->requestPayload->bookId));
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleLimitOrderPlacementErrorResponse(Message::Ptr msg)
{
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleMarketOrderPlacementResponse(Message::Ptr msg)
{
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleMarketOrderPlacementErrorResponse(Message::Ptr msg)
{
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleCancelOrdersResponse(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<CancelOrdersResponsePayload>(msg->payload);
    const BookId bookId = payload->requestPayload->bookId;
    for (auto& cancel: payload->requestPayload->cancellations){
        removeOrder(bookId, cancel.id);
    }
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleCancelOrdersErrorResponse(Message::Ptr msg)
{
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::handleTrade(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<EventTradePayload>(msg->payload);
    BookId bookId = payload->bookId;

    if (m_id == payload->context.aggressingAgentId) {
        m_inventory[bookId] += payload->trade.direction() == OrderDirection::BUY ? 
            taosim::util::decimal2double(payload->trade.volume()) : 
            taosim::util::decimal2double(-payload->trade.volume());
        // NOTE order not recorded
        removeOrder(bookId, payload->trade.aggressingOrderID(), taosim::util::decimal2double(payload->trade.volume()));
    }
    if (m_id == payload->context.restingAgentId) {
        m_inventory[bookId] += payload->trade.direction() == OrderDirection::BUY ? 
            taosim::util::decimal2double(-payload->trade.volume()) : 
            taosim::util::decimal2double(payload->trade.volume());
        removeOrder(bookId, payload->trade.restingOrderID(), taosim::util::decimal2double(payload->trade.volume()));
    }
}


//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::sendOrder(std::optional<PlaceOrderLimitPayload::Ptr> payload) {
    
    if (payload.has_value()) {
        simulation()->dispatchMessage(
            simulation()->currentTimestamp(),
            orderPlacementLatency(),
            name(),
            m_exchange,
            "PLACE_ORDER_LIMIT",
            payload.value());
    }
}

//-------------------------------------------------------------------------

std::optional<PlaceOrderLimitPayload::Ptr> HighFrequencyTraderAgent::makeOrder(BookId bookId, OrderDirection direction,
    double volume, double limitPrice, double wealth) {
    
    if (limitPrice <= 0 || volume <= 0 || wealth <= 0) {
        return std::nullopt;
    }

    double leverage = (volume * limitPrice - wealth) / wealth;
    if (leverage > 0) {
        if (leverage > m_maxLeverage) {
            leverage = m_maxLeverage;
        }
        volume = volume / (1. + leverage);
    } else {
        leverage = 0.;
    }
    
    return std::make_optional(MessagePayload::create<PlaceOrderLimitPayload>(
        direction,
        taosim::util::double2decimal(volume),
        taosim::util::double2decimal(limitPrice),
        taosim::util::double2decimal(leverage),
        bookId));
 }

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::placeOrder(BookId bookId, TopLevel& topLevel) {

    // Seed the random number generator
    std::random_device rd;
    m_rng->seed(rd());
    std::lognormal_distribution<> lognormalDist(m_orderMean, 1); // mean=m_orderMean, stddev=1
    
    const double rayleighShift = m_noiseRay * std::sqrt(-2.0 * std::log(1.0 - m_shiftPercentage));
    const double actualSpread = topLevel.ask - topLevel.bid;

    // ----- Bid Placement -----
    double wealthBid = topLevel.ask * m_baseFree[bookId] + m_quoteFree[bookId];
    double orderVolumeBid = lognormalDist(*m_rng);
    double noiseBid = sampleRayleigh(*m_rng, m_noiseRay) - rayleighShift;
    double priceOrderBid = m_pRes - (actualSpread / 2.0) - noiseBid;
    double limitPriceBid = std::round(priceOrderBid / m_priceIncrement) * m_priceIncrement;
    const auto bidPayload = makeOrder(bookId, OrderDirection::BUY, orderVolumeBid, limitPriceBid, wealthBid);

    // ----- Ask Placement -----
    double wealthAsk = topLevel.bid * m_baseFree[bookId] + m_quoteFree[bookId];
    double orderVolumeAsk = lognormalDist(*m_rng);
    double noiseAsk = sampleRayleigh(*m_rng, m_noiseRay) - rayleighShift;
    double priceOrderAsk = m_pRes + (actualSpread / 2.0) + noiseAsk;
    double limitPriceAsk = std::round(priceOrderAsk / m_priceIncrement) * m_priceIncrement;
    const auto askPayload = makeOrder(bookId, OrderDirection::SELL, orderVolumeAsk, limitPriceAsk, wealthAsk);

    if (m_debug) {
        simulation()->logDebug("BOOK {} | p_bid_raw={}, limit={}, volume={}\n", bookId, priceOrderBid, limitPriceBid, orderVolumeBid);
        simulation()->logDebug("BOOK {} | p_ask_raw={}, limit={}, volume={}\n", bookId, priceOrderAsk, limitPriceAsk, orderVolumeAsk);
    }

    if (std::abs(m_inventory[bookId]) > m_psi){
        if (m_inventory[bookId] < 0){
            sendOrder(bidPayload);
            if (std::uniform_real_distribution{0.0,1.0}(*m_rng) < 0.75) {
                cancelClosestToBestPrice(bookId, OrderDirection::SELL, topLevel.ask);
            }
            else
                sendOrder(askPayload);
        } else {
            sendOrder(askPayload);
            if (std::uniform_real_distribution{0.0,1.0}(*m_rng) < 0.75) {
                cancelClosestToBestPrice(bookId, OrderDirection::BUY, topLevel.bid);
            } else {
                sendOrder(bidPayload);
            }
        }
    } else {
        sendOrder(bidPayload);
        sendOrder(askPayload);
    }
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::cancelClosestToBestPrice(BookId bookId, OrderDirection direction, double bestPrice) {

    if (m_recordedOrders.find(bookId) == m_recordedOrders.end()) {
        return;
    }

    OrderID closestId = -1;
    double closestDelta = 999999.;
    RecordedOrder closestOrder;
    for (auto it = m_recordedOrders[bookId].rbegin(); it != m_recordedOrders[bookId].rend(); ++it) {
        const auto& order = *it;
        if (!order.traded && !order.canceled && order.direction == direction) {
            double delta = std::abs(order.price - bestPrice);
            if (closestId == -1 || delta < closestDelta) {
                closestId = order.orderId;
                closestDelta = delta;
                closestOrder = order;
            }
        }
    }

    if (closestId != -1) {
        // This is unnecessary if we remove this immediately from records
        closestOrder.canceled = true;

        removeOrder(bookId, closestId);
        simulation()->dispatchMessage(
            simulation()->currentTimestamp(),
            orderPlacementLatency(),
            name(),
            m_exchange,
            "CANCEL_ORDERS",
            MessagePayload::create<CancelOrdersPayload>(
                std::vector{Cancellation(closestId)}, bookId)
        );
    }
}

//-------------------------------------------------------------------------

double HighFrequencyTraderAgent::sampleRayleigh(std::mt19937& gen, double scale) const
{
    double u = std::generate_canonical<double, 10>(gen);
    return scale * std::sqrt(-2.0 * std::log(1.0 - u));
}

//-------------------------------------------------------------------------

Timestamp HighFrequencyTraderAgent::orderPlacementLatency() const {
    double raySample = sampleRayleigh(*m_rng, m_opLatencyScaleRay);
    Timestamp latency = static_cast<Timestamp>(m_opLatency.min + (m_opLatency.max - m_opLatency.min) * raySample);
    return std::min(latency, m_opLatency.max);
}

//-------------------------------------------------------------------------

Timestamp HighFrequencyTraderAgent::rayleighLatencyNs(double scale) const {
    double raySample = sampleRayleigh(*m_rng, m_opLatencyScaleRay);
    Timestamp latency = static_cast<Timestamp>(raySample + m_opLatency.min);
    return std::min(latency, m_opLatency.max);
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::recordOrder(PlaceOrderLimitResponsePayload::Ptr payload) {
    BookId bookId = payload->requestPayload->bookId;

    RecordedOrder order;
    order.orderId = payload->id;
    order.traded = false;
    order.canceled = false;
    order.volume = taosim::util::decimal2double(payload->requestPayload->volume);
    order.price = taosim::util::decimal2double(payload->requestPayload->price);
    order.direction = payload->requestPayload->direction;

    m_recordedOrders[bookId].emplace_back(std::move(order));
}

//-------------------------------------------------------------------------

void HighFrequencyTraderAgent::removeOrder(BookId bookId, OrderID orderId, std::optional<double> amount) {
    auto it = m_recordedOrders.find(bookId);
    if (it == m_recordedOrders.end())
        return;

    auto& orders = it->second;
    for (auto iter = orders.begin(); iter != orders.end(); ++iter) {
        if (iter->orderId == orderId) {
            if (amount.has_value()) {
                iter->volume -= amount.value();
                if (iter->volume < m_volumeIncrement) {
                    orders.erase(iter);
                }
            } else {
                orders.erase(iter);
            }
            break;
        }
    }
}