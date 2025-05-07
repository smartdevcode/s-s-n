/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "StylizedTraderAgent.hpp"

#include "ExchangeAgentMessagePayloads.hpp"
#include "MessagePayload.hpp"
#include "Simulation.hpp"

#include <boost/algorithm/string/regex.hpp>
#include <boost/bimap.hpp>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/random.hpp>
#include <boost/random/laplace_distribution.hpp>
#include <unsupported/Eigen/NonLinearOptimization>

#include <algorithm>

//-------------------------------------------------------------------------

namespace br = boost::random;

//-------------------------------------------------------------------------

StylizedTraderAgent::StylizedTraderAgent(Simulation* simulation) noexcept
    : Agent{simulation}
{}

//-------------------------------------------------------------------------

void StylizedTraderAgent::configure(const pugi::xml_node& node)
{
    Agent::configure(node);

    m_rng = &simulation()->rng();

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

    if (attr = node.attribute("sigmaF"); attr.empty() || attr.as_double() < 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'sigmaF' should have a value of at least 0.0f", ctx));
    }
    const double sigmaF = attr.as_double();
    if (attr = node.attribute("sigmaC"); attr.empty() || attr.as_double() < 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'sigmaC' should have a value of at least 0.0f", ctx));
    }
    const double sigmaC = attr.as_double();
    if (attr = node.attribute("sigmaN"); attr.empty() || attr.as_double() < 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'sigmaN' should have a value of at least 0.0f", ctx));
    }
    const double sigmaN = attr.as_double();
    m_weight = {
        .F = std::abs(br::laplace_distribution{sigmaF, sigmaF}(*m_rng)),
        .C = std::abs(br::laplace_distribution{sigmaC, sigmaC}(*m_rng)),
        .N = std::abs(br::laplace_distribution{sigmaN, sigmaN}(*m_rng))
    };
    m_weightNormalizer = 1.0f / (m_weight.F + m_weight.C + m_weight.N);

    if (attr = node.attribute("priceF0"); attr.empty() || attr.as_double() <= 0.0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'priceF0' should have a value greater than 0.0", ctx));
    }
    m_priceF0 = attr.as_double();
    if (attr = node.attribute("price0"); attr.empty() || attr.as_double() <= 0.0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'price0' should have a value greater than 0.0", ctx));
    }
    m_price0 = attr.as_double();

    if (attr = node.attribute("tau"); attr.empty() || attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'tau' should have a value greater than 0", ctx));
    }
    m_tau0 = attr.as_ullong();
    m_tau = std::min(
        static_cast<Timestamp>(std::ceil(
            m_tau0 * (1.0f + m_weight.F) / (1.0f + m_weight.C))),
        simulation()->duration() - 1);
    if (attr = node.attribute("tauF"); attr.empty() || attr.as_double() == 0.0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'tauF' should have a value greater than 0.0", ctx));
    }
    m_tauF = attr.as_double();

    if (attr = node.attribute("sigmaEps"); attr.empty() || attr.as_double() <= 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'sigmaEps' should have a value greater than 0.0f", ctx));
    }
    m_sigmaEps = attr.as_double();

    if (attr = node.attribute("r_aversion"); attr.empty() || attr.as_double() <= 0.0f) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'r_aversion' should have a value greater than 0.0f", ctx));
    }
    m_riskAversion0 = attr.as_double();
    m_riskAversion = m_riskAversion0 * (1.0f + m_weight.F) / (1.0f + m_weight.C);

    if (attr = node.attribute("minOPLatency"); attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'minLatency' should have a value greater than 0", ctx));
    }
    m_opl.min = attr.as_ullong();
    if (attr = node.attribute("maxOPLatency"); attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'maxLatency' should have a value greater than 0", ctx));
    }
    m_opl.max = attr.as_ullong();
    if (m_opl.min >= m_opl.max) {
        throw std::invalid_argument(fmt::format(
            "{}: 'minOPLatency' ({}) should be strictly less 'maxOPLatency' ({})",
            ctx, m_opl.min, m_opl.max));
    }

    m_price = m_priceF0;

    m_orderFlag = std::vector<bool>(m_bookCount, false);

    if (attr = node.attribute("tauHist"); attr.empty() || attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'tauHist' should have a value greater than 0", ctx));
    }
    m_tauHist = attr.as_ullong();
    m_historySize = std::min(std::max(
        50ul, static_cast<Timestamp>(std::ceil(m_tauHist * (1.0 + m_weight.F) / (1.0 + m_weight.C)))),500ul);

    if (attr = node.attribute("tauFore"); attr.empty() || attr.as_ullong() == 0) {
        throw std::invalid_argument(fmt::format(
            "{}: attribute 'tauFore' should have a value greater than 0", ctx));
    }
    m_tauFore0 = attr.as_ullong();
    m_tauFore = std::min(std::max(
        1ul, static_cast<Timestamp>(m_tauFore0*std::ceil((1.0 + m_weight.F) / (1.0 + m_weight.C)))),10ul);

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
        m_topLevel.push_back(TopLevel{});
        GBMValuationModel gbmPrice{gbmX0, gbmMu, gbmSigma, gbmSeed + bookId + 1};
        const auto Xt = gbmPrice.generatePriceSeries(1, m_historySize);
        m_priceHist.push_back([&] {
            decltype(m_priceHist)::value_type hist{m_historySize};
            for (uint32_t i = 0; i < m_historySize; ++i) {
                hist.push_back(m_price0 * (1.0 + Xt[i]));
            }
            return hist;
        }());
        m_logReturns.push_back([&] {
            decltype(m_logReturns)::value_type logReturns{m_historySize};
            const auto& priceHist = m_priceHist.at(bookId);
            logReturns.push_back(Xt[0]);
            for (uint32_t i = 1; i < priceHist.capacity(); ++i) {
                logReturns.push_back(std::log(priceHist[i] / priceHist[i - 1]));
            }
            return logReturns;
        }());
    }

    m_priceIncrement = 1 / std::pow(10, simulation()->exchange()->config().parameters().priceIncrementDecimals);
    m_volumeIncrement = 1 / std::pow(10, simulation()->exchange()->config().parameters().volumeIncrementDecimals);

    m_debug = node.attribute("debug").as_bool();

    m_sigmaFRegime = node.attribute("sigmaFRegime").as_float();
    m_sigmaCRegime = node.attribute("sigmaCRegime").as_float();
    m_sigmaNRegime = node.attribute("sigmaNRegime").as_float();
    m_regimeChangeFlag = node.attribute("regimeChangeFlag").as_bool();
    m_regimeChangeProb = std::clamp(node.attribute("regimeProb").as_float(), 0.0f, 1.0f);
    m_weightOrig = m_weight;
    // fmt::println(
        // "{} : regimeChangeFlag={} regimeProb={}", name(), m_regimeChangeFlag, m_regimeChangeProb);

    if (attr = node.attribute("pO_alpha"); attr.empty() || attr.as_double() < 0.0f || attr.as_double() >= 1.0f){
        // throw std::invalid_argument(fmt::format(
        //     "{}: attribute 'pO_alpha' should have a between [0,1)", ctx));
        m_alpha = 0;
    } else {
        m_alpha = attr.as_double();
    }

    m_marketFeedLatencyDistribution = std::normal_distribution<double>{
        [&] {
            static constexpr const char* name = "MFLmean";
            if (auto attr = node.attribute(name); attr.empty()) {
                throw std::invalid_argument{fmt::format(
                    "{}: Missing attribute '{}'", ctx, name)};
            } else {
                return attr.as_double();
            }
        }(),
        [&] {
            static constexpr const char* name = "MFLstd";
            if (auto attr = node.attribute(name); attr.empty()) {
                throw std::invalid_argument{fmt::format(
                    "{}: Missing attribute '{}'", ctx, name)};
            } else {
                return attr.as_double();
            }
        }()
    };
    m_decisionMakingDelayDistribution = std::normal_distribution<double>{
        [&] {
            static constexpr const char* name = "delayMean";
            if (auto attr = node.attribute(name); attr.empty()) {
                throw std::invalid_argument{fmt::format(
                    "{}: Missing attribute '{}'", ctx, name)};
            } else {
                return attr.as_double();
            }
        }(),
        [&] {
            static constexpr const char* name = "delaySTD";
            if (auto attr = node.attribute(name); attr.empty()) {
                throw std::invalid_argument{fmt::format(
                    "{}: Missing attribute '{}'", ctx, name)};
            } else {
                return attr.as_double();
            }
        }()
    };

    m_tradePrice.resize(m_bookCount);

    m_orderPlacementLatencyDistribution = boost::random::beta_distribution<double>{
        [&] {
            static constexpr const char* name = "alphaDelay";
            if (auto alpha = node.attribute(name).as_double(); !(alpha > 0)) {
                throw std::invalid_argument{fmt::format(
                    "{}: Attribute '{}' should be > 0, was {}", ctx, name, alpha)};
            } else {
                return alpha;
            }
        }(),
        [&] {
            static constexpr const char* name = "betaDelay";
            if (auto beta = node.attribute(name).as_double(); !(beta > 0)) {
                throw std::invalid_argument{fmt::format(
                    "{}: Attribute '{}' should be > 0, was {}", ctx, name, beta)};
            } else {
                return beta;
            }
        }()
    };

    m_rayleigh = boost::math::rayleigh_distribution{
        [&] {
            static constexpr const char* name = "scaleR";
            if (auto sigma = node.attribute(name).as_double(); !(sigma >= 0)) {
                throw std::invalid_argument{fmt::format(
                    "{}: Attribute '{}' should be >= 0, was {}", ctx, name, sigma)};
            } else {
                return sigma;
            }
        }()
    };

    m_baseName = [&] {
        std::string res = name();
        boost::algorithm::erase_regex(res, boost::regex("(_\\d+)$"));
        return res;
    }();
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::receiveMessage(Message::Ptr msg)
{
    updateRegime();

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

void StylizedTraderAgent::handleSimulationStart()
{
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        1,
        name(),
        m_exchange,
        "SUBSCRIBE_EVENT_TRADE");
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::handleSimulationStop()
{}

//-------------------------------------------------------------------------

void StylizedTraderAgent::handleTradeSubscriptionResponse()
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

void StylizedTraderAgent::handleRetrieveL1Response(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<RetrieveL1ResponsePayload>(msg->payload);

    const BookId bookId = payload->bookId;

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        static_cast<Timestamp>(std::min(
            std::abs(m_marketFeedLatencyDistribution(*m_rng))
            + std::abs(m_decisionMakingDelayDistribution(*m_rng)),
            m_marketFeedLatencyDistribution.mean()
            + m_decisionMakingDelayDistribution.mean()
            + 3.0 * (m_marketFeedLatencyDistribution.stddev()
                + m_decisionMakingDelayDistribution.stddev()))),
        name(),
        m_exchange,
        "RETRIEVE_L1",
        MessagePayload::create<RetrieveL1Payload>(bookId));

    auto& topLevel = m_topLevel.at(bookId);
    topLevel.bid = taosim::util::decimal2double(payload->bestBidPrice);
    topLevel.ask = taosim::util::decimal2double(payload->bestAskPrice);

    const double midPrice = 0.5 * (topLevel.bid + topLevel.ask);
    const double spotPrice =
        m_tradePrice.at(payload->bookId).timestamp - simulation()->currentTimestamp() < 1'000'000'000
        ? m_tradePrice.at(payload->bookId).price
        : midPrice;
    // update the price history with the midquote or last close price in 5 seconds
    const double closePrice = 
        m_tradePrice.at(payload->bookId).timestamp - simulation()->currentTimestamp() < 5'000'000'000
        ? m_tradePrice.at(payload->bookId).price
        : midPrice;
    m_logReturns.at(payload->bookId).push_back(
                    std::log(closePrice / m_priceHist.at(payload->bookId).back()));
    m_priceHist.at(payload->bookId).push_back(closePrice);
    if (m_orderFlag.at(bookId) || topLevel.bid == 0.0 || topLevel.ask == 0.0) return;

    auto rng = std::mt19937{simulation()->currentTimestamp()};
    
    auto linspace = [](double start, double stop, int num) -> std::vector<double> {
        if (!(num > 1)) {
            throw std::invalid_argument{fmt::format(
                "{}: parameter 'num' should be > 1, was {}",
                std::source_location::current().function_name(), num)};
        }
        const double step = (stop - start) / (num - 1);
        return views::iota(0, num)
            | views::transform([=](int k) { return start + k * step; })
            | ranges::to<std::vector>;
    };

    const uint32_t numActingAgents = [&] {
        static std::uniform_real_distribution<double> s_unif{0.0, 1.0};
        const double rayleighDraw = boost::math::quantile(m_rayleigh, s_unif(rng));
        const auto bins = linspace(0.0, 5.0, 10);
        return std::lower_bound(bins.begin(), bins.end(), rayleighDraw) - bins.begin();
    }();

    const auto& agentBaseNamesToCounts =
        simulation()->localAgentManager()->roster()->baseNamesToCounts();

    const auto categoryIdToAgentType = [&] {
        boost::bimap<uint32_t, std::string> res;
        auto filteredBaseNames = agentBaseNamesToCounts
            | views::keys
            | views::filter([](const auto& baseName) { return baseName.contains("STYLIZED_TRADER_AGENT"); });
        for (const auto& [id, baseName] : views::enumerate(filteredBaseNames)) {
            res.insert({static_cast<uint32_t>(id), baseName});
        }
        return res;
    }();

    auto multinomial = [&] {
        const auto weights = agentBaseNamesToCounts
            | views::filter([](const auto& kv) { return kv.first.contains("STYLIZED_TRADER_AGENT"); })
            | views::values
            | ranges::to<std::vector>;
        return std::discrete_distribution{weights.begin(), weights.end()};
    }();

    const auto categoryIdDraws =
        views::iota(0u, numActingAgents)
        | views::transform([&](auto) { return multinomial(rng); })
        | ranges::to<std::vector>;

    const auto actorIdsNonCanon =
        categoryIdDraws
        | views::transform([&](auto draw) {
            return std::uniform_int_distribution<uint32_t>{
                0, agentBaseNamesToCounts.at(categoryIdToAgentType.left.at(draw)) - 1}(rng);
        })
        | ranges::to<std::vector>;

    for (auto [categoryId, actorId] : views::zip(categoryIdDraws, actorIdsNonCanon)) {
        if (categoryIdToAgentType.right.at(m_baseName) != categoryId) continue;
        if (!name().ends_with(fmt::format("_{}", actorId))) continue;
        m_price = spotPrice;
        placeOrderChiarella(bookId);
    }
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::handleLimitOrderPlacementResponse(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<PlaceOrderLimitResponsePayload>(msg->payload);

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        m_tau,
        name(),
        m_exchange,
        "CANCEL_ORDERS",
        MessagePayload::create<CancelOrdersPayload>(
            std::vector{Cancellation(payload->id)}, payload->requestPayload->bookId));

    m_orderFlag.at(payload->requestPayload->bookId) = false;
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::handleLimitOrderPlacementErrorResponse(Message::Ptr msg)
{
    const auto payload =
        std::dynamic_pointer_cast<PlaceOrderLimitErrorResponsePayload>(msg->payload);

    const BookId bookId = payload->requestPayload->bookId;

    m_orderFlag.at(bookId) = false;
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::handleCancelOrdersResponse(Message::Ptr msg)
{}

//-------------------------------------------------------------------------

void StylizedTraderAgent::handleCancelOrdersErrorResponse(Message::Ptr msg)
{}

//-------------------------------------------------------------------------

void StylizedTraderAgent::handleTrade(Message::Ptr msg)
{
    const auto payload = std::dynamic_pointer_cast<EventTradePayload>(msg->payload);

    const double tradePrice = taosim::util::decimal2double(payload->trade.price());

    m_tradePrice.at(payload->bookId) = {
        .timestamp = msg->arrival,
        .price = tradePrice
    };


}

//-------------------------------------------------------------------------

StylizedTraderAgent::ForecastResult StylizedTraderAgent::forecast(BookId bookId)
{
    const auto& logReturns = m_logReturns.at(bookId);

    const double compF = 1.0 / m_tauF * std::log(getProcessValue(bookId, "fundamental") / m_price);
    const double compC = 1.0 / m_tau * ranges::accumulate(logReturns, 0.0);
    const double compN = std::normal_distribution{0.0, m_sigmaEps}(*m_rng);

    const double logReturnForecast = m_weightNormalizer
        * (m_weight.F * compF + m_weight.C * compC + m_weight.N * compN);
   
    return {
        .price = m_price * std::exp(logReturnForecast),
        .varianceOfLastLogReturns = [&] {
            namespace bacc = boost::accumulators;
            bacc::accumulator_set<double, bacc::stats<bacc::tag::lazy_variance>> acc;
            const auto n = logReturns.capacity();
            for (auto logRet : logReturns) {
                acc(logRet);
            }
            return bacc::variance(acc) * (n - 1) / n;
        }()
    };
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::placeOrderChiarella(BookId bookId)
{
    const ForecastResult forecastResult = forecast(bookId);

    if (m_riskAversion * forecastResult.varianceOfLastLogReturns == 0.0) {
        return;
    }

    const auto freeBase =
        taosim::util::decimal2double(simulation()->account(name()).at(bookId).base.getFree());
    const auto freeQuote =
        taosim::util::decimal2double(simulation()->account(name()).at(bookId).quote.getFree());

    const auto [indifferencePrice, indifferencePriceConverged] =
        calculateIndifferencePrice(forecastResult, freeBase);
    if (!indifferencePriceConverged) return;

    const auto [minimumPrice, minimumPriceConverged] =
        calculateMinimumPrice(forecastResult, freeBase, freeQuote);
    if (!minimumPriceConverged) return;

    const auto maximumPrice = forecastResult.price;

 

    if (minimumPrice <= 0.0
        || minimumPrice > indifferencePrice
        || indifferencePrice > maximumPrice) {
        return;
    }

    const double sampledPrice = std::uniform_real_distribution{minimumPrice, maximumPrice}(*m_rng);
    if (sampledPrice < indifferencePrice) {
        placeLimitBuy(bookId, forecastResult, sampledPrice, freeBase, freeQuote);
    }
    else if (sampledPrice > indifferencePrice) {
        placeLimitSell(bookId, forecastResult, sampledPrice, freeBase);
    }
}

//-------------------------------------------------------------------------

StylizedTraderAgent::OptimizationResult StylizedTraderAgent::calculateIndifferencePrice(
    const StylizedTraderAgent::ForecastResult& forecastResult, double freeBase)
{
    struct Functor
    {
        ForecastResult forecastResult;
        double riskAversion;
        double freeBase;

        Functor(ForecastResult forecastResult, double riskAversion, double freeBase) noexcept
            : forecastResult{forecastResult}, riskAversion{riskAversion}, freeBase{freeBase}
        {}

        int inputs() const noexcept { return 1; }
        int values() const noexcept { return 1; }

        int operator()(const Eigen::VectorXd& x, Eigen::VectorXd& fvec) const
        {
            fvec[0] = std::log(forecastResult.price / x[0])
                / (riskAversion * forecastResult.varianceOfLastLogReturns * x[0])
                - freeBase;
            return 0;
        }
    };

    Functor functor{forecastResult, m_riskAversion, freeBase};
    Eigen::HybridNonLinearSolver<Functor> solver{functor};
    // See https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.fsolve.html.
    solver.parameters.xtol = 1.49012e-8;
    Eigen::VectorXd x{1};
    x[0] = 1.0;
    Eigen::HybridNonLinearSolverSpace::Status status = solver.hybrd1(x);
    return {
        .value = x[0],
        .converged = status == Eigen::HybridNonLinearSolverSpace::RelativeErrorTooSmall
    };
}

//-------------------------------------------------------------------------

StylizedTraderAgent::OptimizationResult StylizedTraderAgent::calculateMinimumPrice(
    const StylizedTraderAgent::ForecastResult& forecastResult, double freeBase, double freeQuote)
{
    struct Functor
    {
        ForecastResult forecastResult;
        double riskAversion;
        double freeBase;
        double freeQuote;

        Functor(
            ForecastResult forecastResult,
            double riskAversion,
            double freeBase,
            double freeQuote) noexcept
            : forecastResult{forecastResult},
              riskAversion{riskAversion},
              freeBase{freeBase},
              freeQuote{freeQuote}
        {}

        int inputs() const noexcept { return 1; }
        int values() const noexcept { return 1; }

        int operator()(const Eigen::VectorXd& x, Eigen::VectorXd& fvec) const
        {
            fvec[0] = x[0] * (std::log(forecastResult.price / x[0])
                / (riskAversion * forecastResult.varianceOfLastLogReturns * x[0])
                - freeBase) - freeQuote;
            return 0;
        }
    };

    Functor functor{forecastResult, m_riskAversion, freeBase, freeQuote};
    Eigen::HybridNonLinearSolver<Functor> solver{functor};
    // See https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.fsolve.html.
    solver.parameters.xtol = 1.49012e-8;
    Eigen::VectorXd x{1};
    x[0] = 1.0;
    Eigen::HybridNonLinearSolverSpace::Status status = solver.hybrd1(x);
    return {
        .value = x[0],
        .converged = status == Eigen::HybridNonLinearSolverSpace::RelativeErrorTooSmall
    };
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::placeLimitBuy(
    BookId bookId,
    const StylizedTraderAgent::ForecastResult& forecastResult,
    double sampledPrice,
    double freeBase,
    double freeQuote)
{
    const double price = std::round(sampledPrice / m_priceIncrement) * m_priceIncrement;

    const double realPrice = std::min(sampledPrice, m_topLevel.at(bookId).ask);

    double volume = std::log(forecastResult.price / realPrice)
        / (m_riskAversion * forecastResult.varianceOfLastLogReturns * realPrice)
        - freeBase;
    if (const auto attainableVolume = freeQuote / price; volume > attainableVolume) {
        volume = attainableVolume;
    }
    volume = std::floor(volume / m_volumeIncrement) * m_volumeIncrement;
    if (volume <= 0.0) {
        return;
    }

    m_orderFlag.at(bookId) = true;

    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        static_cast<Timestamp>(
            std::lerp(m_opl.min, m_opl.max, m_orderPlacementLatencyDistribution(*m_rng))),
        name(),
        m_exchange,
        "PLACE_ORDER_LIMIT",
        MessagePayload::create<PlaceOrderLimitPayload>(
            OrderDirection::BUY,
            taosim::util::double2decimal(volume),
            taosim::util::double2decimal(price),
            bookId));
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::placeLimitSell(
    BookId bookId,
    const StylizedTraderAgent::ForecastResult& forecastResult,
    double sampledPrice,
    double freeBase)
{
    const double price = std::round(sampledPrice / m_priceIncrement) * m_priceIncrement;

    const double realPrice = std::max(price, m_topLevel.at(bookId).bid);

    double volume = freeBase - std::log(forecastResult.price / realPrice)
        / (m_riskAversion * forecastResult.varianceOfLastLogReturns * realPrice);
    if (volume > freeBase) {
        volume = freeBase;
    }
    volume = std::floor(volume / m_volumeIncrement) * m_volumeIncrement;
    if (volume <= 0.0) {
        return;
    }

    m_orderFlag.at(bookId) = true;
    const float draw = std::uniform_real_distribution{0.0f, 1.0f}(*m_rng);
    LimitOrderFlag flag = (draw < m_alpha) ? LimitOrderFlag::POST_ONLY : LimitOrderFlag::NONE;
    simulation()->dispatchMessage(
        simulation()->currentTimestamp(),
        static_cast<Timestamp>(
            std::lerp(m_opl.min, m_opl.max, m_orderPlacementLatencyDistribution(*m_rng))),
        name(),
        m_exchange,
        "PLACE_ORDER_LIMIT",
        MessagePayload::create<PlaceOrderLimitPayload>(
            OrderDirection::SELL,
            taosim::util::double2decimal(volume),
            taosim::util::double2decimal(price),
            bookId,
            std::nullopt,
            flag));
}

//-------------------------------------------------------------------------

double StylizedTraderAgent::getProcessValue(BookId bookId, const std::string& name)
{
    return simulation()->exchange()->process(name, bookId)->value();
}

//-------------------------------------------------------------------------

void StylizedTraderAgent::updateRegime()
{
    if (!m_regimeChangeFlag) return;

    if (std::bernoulli_distribution{m_regimeChangeProb}(*m_rng)) {
        m_weight = {
            .F = std::abs(br::laplace_distribution{m_sigmaFRegime, m_sigmaFRegime}(*m_rng)),
            .C = std::abs(br::laplace_distribution{m_sigmaCRegime, m_sigmaCRegime}(*m_rng)),
            .N = std::abs(br::laplace_distribution{m_sigmaNRegime, m_sigmaNRegime}(*m_rng))
        };
        m_tau = std::min(
            static_cast<Timestamp>(std::ceil(
                m_tau0 * (1.0f + m_weight.F) / (1.0f + m_weight.C))),
            simulation()->duration() - 1);
        m_riskAversion = m_riskAversion0 * (1.0f + m_weight.F) / (1.0f + m_weight.C);
        m_weightNormalizer = 1.0f / (m_weight.F + m_weight.C + m_weight.N);
    } else {
        m_weight = m_weightOrig;
    }
}

//-------------------------------------------------------------------------