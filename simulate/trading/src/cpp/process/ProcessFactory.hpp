/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <pugixml.hpp>

#include "Process.hpp"

//-------------------------------------------------------------------------

class Simulation;

namespace taosim::exchange
{

class ExchangeConfig;

}  // namespace taosim::exchange

//-------------------------------------------------------------------------

class ProcessFactory
{
public:
    ProcessFactory(Simulation* simulation, taosim::exchange::ExchangeConfig* exchangeConfig) noexcept;

    [[nodiscard]] std::unique_ptr<Process> createFromXML(pugi::xml_node node, uint64_t seedShift = 0);
    [[nodiscard]] std::unique_ptr<Process> createFromCheckpoint(const rapidjson::Value& json);

private:
    Simulation* m_simulation;
    taosim::exchange::ExchangeConfig* m_exchangeConfig;
};

//-------------------------------------------------------------------------