/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "ProcessFactory.hpp"

#include "GBM.hpp"
#include "FundamentalPrice.hpp"
#include "Simulation.hpp"

//-------------------------------------------------------------------------

ProcessFactory::ProcessFactory(Simulation* simulation) noexcept
    : m_simulation{simulation}
{}

//-------------------------------------------------------------------------

std::unique_ptr<Process> ProcessFactory::createFromXML(pugi::xml_node node, uint64_t seedShift)
{
    std::string_view name = node.name();

    if (name == "GBM") {
        return GBM::fromXML(node, seedShift);
    } else if (name == "FundamentalPrice") {
        return FundamentalPrice::fromXML(m_simulation, node, seedShift);
    }

    throw std::invalid_argument(fmt::format(
        "{}: Unknown Process type {}", std::source_location::current().function_name(), name));
}

//-------------------------------------------------------------------------

std::unique_ptr<Process> ProcessFactory::createFromCheckpoint(const rapidjson::Value& json)
{
    std::string_view name = json["name"].GetString();

    if (name == "GBM") {
        return GBM::fromCheckpoint(json);
    } else if (name == "FundamentalPrice") {
        return FundamentalPrice::fromCheckpoint(m_simulation, json);
    }

    throw std::invalid_argument(fmt::format(
        "{}: Unknown Process type {}", std::source_location::current().function_name(), name));
}

//-------------------------------------------------------------------------