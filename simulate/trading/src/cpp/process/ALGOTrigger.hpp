/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Process.hpp"
#include "RNG.hpp"
#include "common.hpp"
#include "taosim/simulation/ISimulation.hpp"

#include <pugixml.hpp>

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class ALGOTrigger : public Process
{
public:
    ALGOTrigger(
        taosim::simulation::ISimulation* simulation,
        uint64_t bookId,
        double probability,
        uint64_t seed,
        Timestamp updatePeriod) noexcept;
    
    virtual void update(Timestamp timestamp) override;
    virtual double value() const override { return m_value; }
    virtual uint64_t count() const override { return m_last_count; }
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static std::unique_ptr<ALGOTrigger> fromXML(
        taosim::simulation::ISimulation* simulation, pugi::xml_node node, uint64_t bookId);
    [[nodiscard]] static std::unique_ptr<ALGOTrigger> fromCheckpoint(
        taosim::simulation::ISimulation* simulation, const rapidjson::Value& json, double probability);

private:
    taosim::simulation::ISimulation* m_simulation;
    uint64_t m_bookId;
    // uint64_t m_seedInterval;
    // std::string m_seedfile;
    RNG m_rng;
    double m_probability;
    double m_value;
    uint64_t m_last_count = 0;
    // double m_last_seed = 0;
    // Timestamp m_last_seed_time = 0;
};

//-------------------------------------------------------------------------
