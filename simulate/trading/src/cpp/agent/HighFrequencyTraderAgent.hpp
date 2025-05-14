/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ExchangeAgentMessagePayloads.hpp"
#include "MessagePayload.hpp"
#include "Agent.hpp"
#include "GBMValuationModel.hpp"
#include "LimitedDeque.hpp"
#include "Order.hpp"

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/math/distributions/rayleigh.hpp>
#include <iostream>
#include <vector>
#include <fstream>
#include <cmath>
#include <numeric>
#include <random>


namespace ba = boost::accumulators;

class HighFrequencyTraderAgent : public Agent
{
public:
    HighFrequencyTraderAgent(Simulation* simulation) noexcept;

    virtual void configure(const pugi::xml_node& node) override;
    virtual void receiveMessage(Message::Ptr msg) override;

private:
    struct TopLevel
    {
        double bid, ask;
    };

    struct DelayBounds
    {
        Timestamp min, max;
    };

    struct RecordedOrder {
        OrderID orderId;
        double price;
        double volume;
        OrderDirection direction;
        bool traded;
        bool canceled;
    };

    struct TimestampedTradePrice
    {
        Timestamp timestamp{};
        double price{};
    };
    
    void handleSimulationStart();
    void handleSimulationStop();
    void handleTradeSubscriptionResponse();
    void handleRetrieveL1Response(Message::Ptr msg);
    void handleLimitOrderPlacementResponse(Message::Ptr msg);
    void handleLimitOrderPlacementErrorResponse(Message::Ptr msg);
    void handleMarketOrderPlacementResponse(Message::Ptr msg);
    void handleMarketOrderPlacementErrorResponse(Message::Ptr msg);
    void handleCancelOrdersResponse(Message::Ptr msg);
    void handleCancelOrdersErrorResponse(Message::Ptr msg);
    void handleTrade(Message::Ptr msg);

    void placeOrder(BookId bookId, TopLevel& topLevel);
    std::optional<PlaceOrderLimitPayload::Ptr> makeOrder(BookId bookId, OrderDirection direction, 
        double volume, double limitPrice, double wealth);
    void sendOrder(std::optional<PlaceOrderLimitPayload::Ptr> payload);
    void cancelClosestToBestPrice(BookId bookId, OrderDirection direction, double bestPrice);
    Timestamp orderPlacementLatency();
    void recordOrder(PlaceOrderLimitResponsePayload::Ptr payload);
    void removeOrder(BookId bookId, OrderID orderId, std::optional<double> amount = {});


    std::mt19937 *m_rng;
    std::string m_exchange;
    uint32_t m_bookCount;

    double m_wealthFrac;
    double m_priceInit;
    double m_gHFT;
    double m_kappa;
    double m_spread;
    double m_delta;
    Timestamp m_tau;
    Timestamp m_minMFLatency;
    double m_psi;
    
    double m_noiseRay;
    double m_shiftPercentage;
    double m_orderMean;

    double m_pRes;
    double m_sigmaSqrInit;
    Timestamp m_sigmaScalingBase;
    DelayBounds m_opl;

    uint32_t m_priceDecimals;
    uint32_t m_volumeDecimals;
    double m_priceIncrement;
    double m_volumeIncrement;
    double m_maxLoan;
    double m_maxLeverage;
    bool m_debug;
    AgentId m_id;

    std::vector<TopLevel> m_topLevel;
    std::vector<double> m_inventory;
    std::vector<double> m_baseFree;
    std::vector<double> m_quoteFree;
    std::vector<bool> m_orderFlag;
    std::map<BookId, std::vector<RecordedOrder>> m_recordedOrders;

    std::vector<double> m_deltaHFT;
    std::vector<Timestamp> m_tauHFT;

    std::vector<LimitedDeque<double>> m_priceHist;
    std::vector<LimitedDeque<double>> m_logReturns;
    std::vector<TimestampedTradePrice> m_tradePrice;
    boost::math::rayleigh_distribution<double> m_orderPlacementLatencyDistribution;
    boost::math::rayleigh_distribution<double> m_rayleighSample;
    std::uniform_real_distribution<double> m_placementDraw;

};
