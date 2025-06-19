/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "TradeLogAgent.hpp"

#include "ExchangeAgentMessagePayloads.hpp"
#include "Simulation.hpp"

#include <iostream>


TradeLogAgent::TradeLogAgent(Simulation* simulation) : Agent(simulation)
{}


TradeLogAgent::TradeLogAgent(Simulation* simulation, const std::string& name)
    : Agent(simulation, name)
{}

void TradeLogAgent::receiveMessage(Message::Ptr messagePtr)
{
    const Timestamp currentTimestamp = simulation()->currentTimestamp();

    if (messagePtr->type == "EVENT_SIMULATION_START") {
        simulation()->dispatchMessage(
            currentTimestamp,
            currentTimestamp,
            name(),
            m_exchange,
            "SUBSCRIBE_EVENT_TRADE",
            std::make_shared<EmptyPayload>());
    }
    else if (messagePtr->type == "EVENT_TRADE") {
        auto pptr = std::dynamic_pointer_cast<EventTradePayload>(messagePtr->payload);

        std::cout << name() << ": ";
        std::cout << taosim::json::json2str([pptr] {
            rapidjson::Document json;
            pptr->jsonSerialize(json);
            return json;
        }());
        std::cout << std::endl;
    }
}

void TradeLogAgent::configure(const pugi::xml_node& node)
{
    Agent::configure(node);

    pugi::xml_attribute att;
    if (!(att = node.attribute("exchange")).empty()) {
        m_exchange = "EXCHANGE";
    }
}