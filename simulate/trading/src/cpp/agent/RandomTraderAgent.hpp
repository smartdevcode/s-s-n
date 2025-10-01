/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Agent.hpp"
#include "GBMValuationModel.hpp"
#include "Order.hpp"

#include <cmath>
#include <random>

//-------------------------------------------------------------------------

namespace taosim::agent
{

//-------------------------------------------------------------------------


class RandomTraderAgent : public Agent
{
public:
    RandomTraderAgent(Simulation* simulation) noexcept;

    virtual void configure(const pugi::xml_node& node) override;
    virtual void receiveMessage(Message::Ptr msg) override;

private:

    struct TopLevel
    {
        double bid, ask;
    };


    void handleSimulationStart();
    void handleSimulationStop();
    void handleTradeSubscriptionResponse();
    void handleRetrieveResponse(Message::Ptr msg);
    void handleLimitOrderPlacementResponse(Message::Ptr msg);
    void handleLimitOrderPlacementErrorResponse(Message::Ptr msg);
    void handleCancelOrdersResponse(Message::Ptr msg);
    void handleCancelOrdersErrorResponse(Message::Ptr msg);
    void handleTrade(Message::Ptr msg);

    void sendOrder(BookId bookId, OrderDirection direction,
        double volume, double price, double leverage);


    std::vector<TopLevel> m_topLevel;
    std::vector<bool> m_orderFlag;
    uint32_t m_bookCount;
    std::string m_exchange;
    Timestamp m_tau;
    double m_quantityMin;
    double m_quantityMax;
};

//-------------------------------------------------------------------------

}  // namespace taosim::agent

//-------------------------------------------------------------------------
