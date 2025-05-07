/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "DelayDistribution.hpp"

#include "DistributionFactory.hpp"

#include <fmt/format.h>

#include <source_location>

//-------------------------------------------------------------------------

namespace taosim::agent
{

//-------------------------------------------------------------------------

DelayDistribution::DelayDistribution(DelayDistributionDesc desc)
    : m_distribution{std::move(desc.distribution)},
      m_targetMax{desc.targetMax}
{
    static constexpr auto ctx = std::source_location::current().function_name();

    if (desc.maxPercentile < 0.0 || 1.0 < desc.maxPercentile) {
        throw std::invalid_argument{fmt::format(
            "{}: maxPercentile should be in (0,1), was {}", ctx, desc.maxPercentile)};
    }

    auto normalizationConstant = m_distribution->quantile(desc.maxPercentile);
    if (normalizationConstant <= 0.0) {
        normalizationConstant = 1.0;
    }
    m_normalizationFactor = 1.0 / normalizationConstant;
}

//-------------------------------------------------------------------------

Timestamp DelayDistribution::sample(std::mt19937& rng) noexcept
{
    return static_cast<Timestamp>(
        m_distribution->sample(rng) * m_normalizationFactor * m_targetMax);
}

//-------------------------------------------------------------------------

std::unique_ptr<DelayDistribution> DelayDistribution::fromXML(pugi::xml_node node)
{
    return std::make_unique<DelayDistribution>(DelayDistributionDesc{
        .distribution = stats::DistributionFactory::createFromXML(node),
        .maxPercentile = node.attribute("maxPercentile").as_double(),
        .targetMax = node.attribute("targetMax").as_ullong()
    });
}

//-------------------------------------------------------------------------

}  // namespace taosim::agent

//-------------------------------------------------------------------------
