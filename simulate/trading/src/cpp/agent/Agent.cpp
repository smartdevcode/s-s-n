/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "Agent.hpp"

#include "MultiBookExchangeAgent.hpp"
#include "ParameterStorage.hpp"
#include "Simulation.hpp"

//-------------------------------------------------------------------------

void Agent::configure(const pugi::xml_node& node)
{
    if (auto att = node.attribute("name")) {
        setName(simulation()->parameters().processString(att.as_string()));
    }
    m_type = node.name();
}

//-------------------------------------------------------------------------

Agent::Agent(Simulation* simulation, const std::string& name) noexcept
    : IMessageable{simulation, name}
{}

//-------------------------------------------------------------------------
