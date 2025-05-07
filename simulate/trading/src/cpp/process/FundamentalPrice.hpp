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

class FundamentalPrice : public Process
{
public:
    FundamentalPrice(
        Simulation* simulation,
        uint64_t bookId,
        uint64_t seedInterval,
        double X0,
        double mu,
        double sigma,
        double dt) noexcept;
    
    virtual void update(Timestamp timestamp) override;
    virtual double value() const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static std::unique_ptr<FundamentalPrice> fromXML(
        Simulation* simulation, pugi::xml_node node, uint64_t bookId);
    [[nodiscard]] static std::unique_ptr<FundamentalPrice> fromCheckpoint(
        Simulation* simulation, const rapidjson::Value& json);

private:
    Simulation* m_simulation;
    uint64_t m_bookId;
    uint64_t m_seedInterval;
    std::string m_seedfile;
    RNG m_rng;
    double m_X0, m_mu, m_sigma, m_dt;
    double m_t{}, m_W{};
    std::normal_distribution<double> m_gaussian;
    double m_value;
    int m_last_count = 0;
    uint64_t m_last_seed = 0;
    Timestamp m_last_seed_time = 0;
};

//-------------------------------------------------------------------------
