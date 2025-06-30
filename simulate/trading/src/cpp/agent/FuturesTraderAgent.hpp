/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Agent.hpp"
#include "GBMValuationModel.hpp"
#include "Order.hpp"

#include <boost/circular_buffer.hpp>
#include <boost/random/beta_distribution.hpp>
#include <boost/math/distributions/rayleigh.hpp>

#include <cmath>
#include <random>

//-------------------------------------------------------------------------

class FuturesTraderAgent : public Agent
{
public:
    FuturesTraderAgent(Simulation* simulation) noexcept;

    virtual void configure(const pugi::xml_node& node) override;
    virtual void receiveMessage(Message::Ptr msg) override;

private:
    struct DelayBounds
    {
        Timestamp min, max;
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
    void handleMarketOrderPlacementResponse(Message::Ptr msg);
    void handleMarketOrderPlacementErrorResponse(Message::Ptr msg);
    void handleCancelOrdersResponse(Message::Ptr msg);
    void handleCancelOrdersErrorResponse(Message::Ptr msg);
    void handleTrade(Message::Ptr msg);

    void placeOrder(BookId bookId);
    void placeBuy(BookId bookId,double volume);
    void placeSell(BookId bookId, double volume);
    double getProcessValue(BookId bookId, const std::string& name);
    uint64_t getProcessCount(BookId bookId, const std::string& name);
    Timestamp orderPlacementLatency();

    std::mt19937* m_rng;
    std::string m_exchange;

    std::vector<Timestamp> m_lastUpdate;
    uint32_t m_bookCount;

    double m_sigmaN;
    double m_sigmaEps;
    double m_priceIncrement;
    double m_volumeIncrement;
    double m_volume;
    std::vector<float> m_volumeFactor;
    std::vector<uint32_t> m_factorCounter;
    float m_lambda;

    DelayBounds m_opl;
    std::vector<bool> m_orderFlag;
    std::vector<boost::circular_buffer<double>> m_priceHist;
    std::vector<boost::circular_buffer<double>> m_logReturns;
    
    bool m_debug;

    Timestamp m_historySize;
    std::normal_distribution<double> m_marketFeedLatencyDistribution;
    std::vector<TimestampedTradePrice> m_tradePrice;
    boost::math::rayleigh_distribution<double> m_orderPlacementLatencyDistribution;
    std::uniform_real_distribution<double> m_placementDraw;
    boost::math::rayleigh_distribution<double> m_rayleigh;
    std::string m_baseName;
};

//-------------------------------------------------------------------------
