/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "ALGOTrigger.hpp"

#include "Simulation.hpp"

#include <cmath>
#include <source_location>

//-------------------------------------------------------------------------

ALGOTrigger::ALGOTrigger(
    taosim::simulation::ISimulation* simulation,
    uint64_t seedInterval,
    double probability,
    uint64_t seed,
    Timestamp updatePeriod) noexcept
    : m_simulation{simulation},
      m_probability{probability}
{
    m_updatePeriod = updatePeriod;
    m_rng = RNG{seed};
    m_value = 0.0;
    m_last_count = 0;
}

//-------------------------------------------------------------------------

void ALGOTrigger::update(Timestamp timestamp)
{
    bool activate = std::bernoulli_distribution{m_probability}(m_rng);
    if (activate) {
        if (std::bernoulli_distribution{0.5}(m_rng)) {
            m_value = 1.0;
        } else {
            m_value = -1.0;
        } 
    } else {
        m_value = 0.0;
    }
    m_last_count += 1;
}

//-------------------------------------------------------------------------

void ALGOTrigger::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("name", rapidjson::Value{"external", allocator}, allocator);
        json.AddMember("bookId", rapidjson::Value{m_bookId}, allocator);
        m_rng.checkpointSerialize(json, "rng");
        json.AddMember("probability", rapidjson::Value{m_probability}, allocator);
        json.AddMember("value", rapidjson::Value{m_value}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

std::unique_ptr<ALGOTrigger> ALGOTrigger::fromXML(
    taosim::simulation::ISimulation* simulation, pugi::xml_node node, uint64_t bookId)
{
    static constexpr auto ctx = std::source_location::current().function_name();

    auto getNonNegativeFloatAttribute = [&](pugi::xml_node node, const char* name) {
        pugi::xml_attribute attr = node.attribute(name);
        if (double value = attr.as_double(); attr.empty() || value < 0.0) {
            throw std::invalid_argument(fmt::format(
                "{}: Attribute '{}' must be non-negative", ctx, name));
        } else {
            return value;
        }
    };

    auto getNonNegativeUint64Attribute = [&](pugi::xml_node node, const char* name) {
        pugi::xml_attribute attr = node.attribute(name);
        if (uint64_t value = attr.as_ullong(); attr.empty() || value < 0.0) {
            throw std::invalid_argument(fmt::format(
                "{}: Attribute '{}' must be non-negative", ctx, name));
        } else {
            return value;
        }
    };

    return std::make_unique<ALGOTrigger>(
        simulation,
        bookId,
        getNonNegativeFloatAttribute(node,"probability"),
        getNonNegativeUint64Attribute(node,"seed"),
        node.attribute("updatePeriod").as_ullong(1));
}

//-------------------------------------------------------------------------

std::unique_ptr<ALGOTrigger> ALGOTrigger::fromCheckpoint(
    taosim::simulation::ISimulation* simulation, const rapidjson::Value& json, double probability)
{
    auto fp = std::make_unique<ALGOTrigger>(
        simulation,
        json["bookId"].GetUint64(),
        probability,
        42,
        1);
    fp->m_value = json["value"].GetDouble();
    fp->m_rng = RNG::fromCheckpoint(json["rng"]);
    return fp;
}

//-------------------------------------------------------------------------
