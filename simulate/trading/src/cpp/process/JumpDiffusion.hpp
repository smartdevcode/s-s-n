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

class JumpDiffusion : public Process
{
public:
    JumpDiffusion(double X0, double mu, double sigma, double dt,  double lambda,  double muJump ,double sigmaJump) noexcept;
    JumpDiffusion(double X0, double mu, double sigma, double dt, double lambda, double muJump, double sigmaJump, uint64_t seed) noexcept;

    virtual double value() const override { return m_value; }

    virtual void update(Timestamp timestamp) override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static std::unique_ptr<JumpDiffusion> fromXML(pugi::xml_node node, uint64_t bookId, uint64_t updatePeriod);
    [[nodiscard]] static std::unique_ptr<JumpDiffusion> fromCheckpoint(const rapidjson::Value& json);

private:
    RNG m_rng;
    double m_X0, m_mu, m_sigma, m_dt;
    double m_dJ;
    double m_t{}, m_W{};
    std::normal_distribution<double> m_gaussian;
    std::normal_distribution<double> m_jump;
    std::poisson_distribution<int> m_poisson;
    double m_value;
};

//-------------------------------------------------------------------------
