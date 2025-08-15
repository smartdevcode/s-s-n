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

class GBM : public Process
{
public:
    GBM(double X0, double mu, double sigma, double dt, Timestamp updatePeriod) noexcept;
    GBM(double X0, double mu, double sigma, double dt, uint64_t seed, Timestamp updatePeriod) noexcept;

    virtual double value() const override { return m_value; }

    virtual void update(Timestamp timestamp) override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static std::unique_ptr<GBM> fromXML(pugi::xml_node node, uint64_t bookId);
    [[nodiscard]] static std::unique_ptr<GBM> fromCheckpoint(const rapidjson::Value& json);

private:
    RNG m_rng;
    double m_X0, m_mu, m_sigma, m_dt;
    double m_t{}, m_W{};
    std::normal_distribution<double> m_gaussian;
    double m_value;
};

//-------------------------------------------------------------------------
