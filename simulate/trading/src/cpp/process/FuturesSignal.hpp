/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Process.hpp"
#include "RNG.hpp"
#include "common.hpp"

#include <pugixml.hpp>

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class FuturesSignal : public Process
{
public:
    FuturesSignal(
        Simulation* simulation,
        uint64_t bookId,
        uint64_t seedInterval,
        double X0) noexcept;

    
    virtual void update(Timestamp timestamp) override;
    virtual double value() const override;
    virtual uint64_t count() const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static std::unique_ptr<FuturesSignal> fromXML(
        Simulation* simulation, pugi::xml_node node, uint64_t bookId, double X0, uint64_t updatePeriod);
    [[nodiscard]] static std::unique_ptr<FuturesSignal> fromCheckpoint(
        Simulation* simulation, const rapidjson::Value& json, double X0);

private:
    Simulation* m_simulation;
    uint64_t m_bookId;
    uint64_t m_seedInterval;
    std::string m_seedfile;
    double m_X0; //, m_mu, m_sigma, m_dt;
    double m_value;
    uint64_t m_last_count = 0;
    double m_last_seed = 0;
    Timestamp m_last_seed_time = 0;
};

//-------------------------------------------------------------------------
