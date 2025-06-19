/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Distribution.hpp"
#include "DiracDeltaDistribution.hpp"
#include "GammaDistribution.hpp"
#include "LognormalDistribution.hpp"
#include "taosim/simulation/TimeConfig.hpp"

#include <pugixml.hpp>

#include <memory>

//-------------------------------------------------------------------------

namespace taosim::agent
{

//-------------------------------------------------------------------------

struct DelayDistributionDesc
{
    std::unique_ptr<stats::Distribution> distribution;
    double maxPercentile;
    Timestamp targetMax;
};

//-------------------------------------------------------------------------

class DelayDistribution
{
public:
    DelayDistribution(DelayDistributionDesc desc);

    [[nodiscard]] Timestamp sample(std::mt19937& rng) noexcept;

    [[nodiscard]] static std::unique_ptr<DelayDistribution> fromXML(pugi::xml_node node);

private:
    std::unique_ptr<stats::Distribution> m_distribution;
    Timestamp m_targetMax;
    double m_normalizationFactor{1.0};
};

//-------------------------------------------------------------------------

}  // namespace taosim::agent

//-------------------------------------------------------------------------
