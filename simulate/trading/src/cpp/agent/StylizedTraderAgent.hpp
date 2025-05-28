/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Agent.hpp"
#include "GBMValuationModel.hpp"
#include "LimitedDeque.hpp"
#include "Order.hpp"

#include <boost/random/beta_distribution.hpp>
#include <boost/math/distributions/rayleigh.hpp>

#include <cmath>
#include <random>

//-------------------------------------------------------------------------

class StylizedTraderAgent : public Agent
{
public:
    StylizedTraderAgent(Simulation* simulation) noexcept;

    virtual void configure(const pugi::xml_node& node) override;
    virtual void receiveMessage(Message::Ptr msg) override;

private:
    struct ForecastResult
    {
        double price;
        double varianceOfLastLogReturns;
    };

    struct TopLevel
    {
        double bid, ask;
    };

    struct OptimizationResult
    {
        double value;
        bool converged;
    };

    struct DelayBounds
    {
        Timestamp min, max;
    };

    struct Weight { double F, C, N; };

    struct TimestampedTradePrice
    {
        Timestamp timestamp{};
        double price{};
    };
    
    enum RegimeState {
        NORMAL,
        REGIME_A,
        REGIME_B
    };


    void handleSimulationStart();
    void handleSimulationStop();
    void handleTradeSubscriptionResponse();
    void handleRetrieveL1Response(Message::Ptr msg);
    void handleLimitOrderPlacementResponse(Message::Ptr msg);
    void handleLimitOrderPlacementErrorResponse(Message::Ptr msg);
    void handleCancelOrdersResponse(Message::Ptr msg);
    void handleCancelOrdersErrorResponse(Message::Ptr msg);
    void handleTrade(Message::Ptr msg);

    ForecastResult forecast(BookId bookId);
    void placeOrderChiarella(BookId bookId);
    OptimizationResult calculateIndifferencePrice(
        const ForecastResult& forecastResult, double freeBase);
    OptimizationResult calculateMinimumPrice(
        const ForecastResult& forecastResult, double freeBase, double freeQuote);
    void placeLimitBuy(
        BookId bookId,
        const ForecastResult& forecastResult,
        double sampledPrice,
        double freeBase,
        double freeQuote);
    void placeLimitSell(
        BookId bookId,
        const ForecastResult& forecastResult,
        double sampledPrice,
        double freeBase);
    double getProcessValue(BookId bookId, const std::string& name);
    void updateRegime(BookId bookId);
    Timestamp orderPlacementLatency();

    std::mt19937* m_rng;
    std::string m_exchange;
    uint32_t m_bookCount;
    Weight m_weight;
    double m_weightNormalizer;
    double m_priceF0;
    double m_price0;
    Timestamp m_tau;
    Timestamp m_tau0;
    Timestamp m_tauHist;
    std::vector<double> m_tauF;
    double m_sigmaEps;
    double m_riskAversion;
    double m_riskAversion0;
    double m_priceIncrement;
    double m_volumeIncrement;

    std::vector<TopLevel> m_topLevel;
    DelayBounds m_opl;
    double m_price;
    std::vector<bool> m_orderFlag;
    std::vector<LimitedDeque<double>> m_priceHist;
    std::vector<LimitedDeque<double>> m_logReturns;
    std::vector<LimitedDeque<double>> m_priceHistExternal;
    std::vector<LimitedDeque<double>> m_logReturnsExternal;
    
    bool m_debug;

    float m_sigmaFRegime;
    float m_sigmaCRegime;
    float m_sigmaNRegime;
    double m_tauFRegime;
    bool m_regimeChangeFlag;
    std::vector<float> m_regimeChangeProb;
    std::vector<RegimeState> m_regimeState;
    Weight m_weightOrig;
    double m_tauFOrig;

    double m_alpha;

    Timestamp m_historySize;
    std::normal_distribution<double> m_marketFeedLatencyDistribution;
    std::normal_distribution<double> m_decisionMakingDelayDistribution;
    std::vector<TimestampedTradePrice> m_tradePrice;
    boost::math::rayleigh_distribution<double> m_orderPlacementLatencyDistribution;
    std::uniform_real_distribution<double> m_placementDraw;
    boost::math::rayleigh_distribution<double> m_rayleigh;
    std::string m_baseName;
};

//-------------------------------------------------------------------------
