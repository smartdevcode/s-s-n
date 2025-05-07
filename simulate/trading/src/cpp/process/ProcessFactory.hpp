/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <pugixml.hpp>

#include "Process.hpp"

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class ProcessFactory
{
public:
    explicit ProcessFactory(Simulation* simulation) noexcept;

    [[nodiscard]] std::unique_ptr<Process> createFromXML(pugi::xml_node node, uint64_t seedShift = 0);
    [[nodiscard]] std::unique_ptr<Process> createFromCheckpoint(const rapidjson::Value& json);

private:
    Simulation* m_simulation;
};

//-------------------------------------------------------------------------