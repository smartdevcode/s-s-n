/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "FundamentalPrice.hpp"

#include "Simulation.hpp"

#include <cmath>
#include <source_location>

//-------------------------------------------------------------------------

FundamentalPrice::FundamentalPrice(
    taosim::simulation::ISimulation* simulation,
    uint64_t bookId,
    uint64_t seedInterval,
    double mu,
    double sigma,
    double dt,
    double X0,
    double lambda, 
    double sigmaJump, 
    double muJump) noexcept
    : m_simulation{simulation},
      m_bookId{bookId},
      m_seedInterval{seedInterval},
      m_mu{mu},
      m_sigma{sigma},
      m_dt{dt},
      m_gaussian{0.0, std::sqrt(dt)},
      m_X0{X0},
      m_poisson{lambda},
      m_jump{muJump,sigmaJump},
      m_dJ{0}
{
    m_value = m_X0;
    m_seedfile = (simulation->logDir().parent_path() / "fundamental_seed.csv").generic_string();
}

//-------------------------------------------------------------------------

void FundamentalPrice::update(Timestamp timestamp)
{
    if (timestamp - m_last_seed_time >= m_seedInterval) {
        int count = m_last_count;
        uint64_t seed = 0;
        if ( fs::exists( m_seedfile ) ) {
            try {
                std::vector<std::string> lines = taosim::util::getLastLines(m_seedfile, 2);
                if (lines.size() >= 2) {
                    std::vector<std::string> line = taosim::util::split(lines[lines.size() - 2],',');
                    if (line.size()== 2) {
                        count = std::stoi(line[0]);
                        seed = static_cast<uint64_t>(round(std::stof(line[1])*100)) + m_bookId*10;
                    } else {
                        fmt::println("FundamentalPrice::update : FAILED TO GET SEED FROM LINE - {}", lines[lines.size() - 2]);
                    }
                } else {
                    fmt::println("FundamentalPrice::update : FAILED TO GET SEED FROM FILE - NO DATA ({} LINES READ)", lines.size());
                }
            } catch (const std::exception &exc) {
                fmt::println("FundamentalPrice::update : ERROR GETTING SEED FROM FILE - {}", exc.what());
            }
            if (count == m_last_count) {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> distr(-50, 50);
                seed = m_last_seed + distr(gen);
                fmt::println("WARNING : Fundamental price seed not updated - using random seed.  Last Count {} | Count {} | Last Seed {} | Seed {}", m_last_count, count, seed, m_last_seed);
            }
        } else {
            fmt::println("FundamentalPrice::update : NO SEED FILE PRESENT AT {}.  Using random seed.", m_seedfile);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> distr(10800000,11200000);
            seed = distr(gen);
        }
        m_rng = RNG{seed}; 
        m_last_count = count;
        m_last_seed = seed;
        m_last_seed_time = timestamp;
        
        m_t += m_dt;
        m_W += m_gaussian(m_rng);
        m_dJ += m_poisson(m_rng)* m_jump(m_rng);
        m_value = m_X0 * std::exp((m_mu - 0.5 * m_sigma * m_sigma) * m_t + m_sigma * m_W + m_dJ);
        m_valueSignal(m_value);
    }
}

//-------------------------------------------------------------------------

double FundamentalPrice::value() const
{
    return m_value;
}

//-------------------------------------------------------------------------

void FundamentalPrice::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("name", rapidjson::Value{"FundamentalPrice", allocator}, allocator);
        json.AddMember("bookId", rapidjson::Value{m_bookId}, allocator);
        json.AddMember("seedInterval", rapidjson::Value{m_seedInterval}, allocator);
        m_rng.checkpointSerialize(json, "rng");
        json.AddMember("X0", rapidjson::Value{m_X0}, allocator);
        json.AddMember("mu", rapidjson::Value{m_mu}, allocator);
        json.AddMember("sigma", rapidjson::Value{m_sigma}, allocator);
        json.AddMember("dt", rapidjson::Value{m_dt}, allocator);
        json.AddMember("t", rapidjson::Value{m_t}, allocator);
        json.AddMember("W", rapidjson::Value{m_W}, allocator);
        json.AddMember("value", rapidjson::Value{m_value}, allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

std::unique_ptr<FundamentalPrice> FundamentalPrice::fromXML(
    taosim::simulation::ISimulation* simulation, pugi::xml_node node, uint64_t bookId, double X0, uint64_t updatePeriod)
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

    const float dt = updatePeriod/86'400'000'000'000.0;
    auto getNonNegativeUint64Attribute = [&](pugi::xml_node node, const char* name) {
        pugi::xml_attribute attr = node.attribute(name);
        if (uint64_t value = attr.as_ullong(); attr.empty() || value < 0.0) {
            throw std::invalid_argument(fmt::format(
                "{}: Attribute '{}' must be non-negative", ctx, name));
        } else {
            return value;
        }
    };

    return std::make_unique<FundamentalPrice>(
        simulation,
        bookId,
        getNonNegativeUint64Attribute(node, "seedInterval"),
        getNonNegativeFloatAttribute(node, "mu"),
        getNonNegativeFloatAttribute(node, "sigma"),
        dt,
        X0,       
        getNonNegativeFloatAttribute(node, "lambda"),
        getNonNegativeFloatAttribute(node, "sigmaJump"),
        getNonNegativeFloatAttribute(node, "muJump"));
}

//-------------------------------------------------------------------------

std::unique_ptr<FundamentalPrice> FundamentalPrice::fromCheckpoint(
    taosim::simulation::ISimulation* simulation, const rapidjson::Value& json, double X0)
{
    auto fp = std::make_unique<FundamentalPrice>(
        simulation,
        json["bookId"].GetUint64(),
        json["seedInterval"].GetUint64(),
        json["mu"].GetDouble(),
        json["sigma"].GetDouble(),
        json["dt"].GetDouble(),
        X0,
        json["lambda"].GetDouble(),
        json["muJump"].GetDouble(),
        json["sigmaJump"].GetDouble());
    fp->m_t = json["t"].GetDouble();
    fp->m_W = json["W"].GetDouble();
    fp->m_value = json["value"].GetDouble();
    fp->m_rng = RNG::fromCheckpoint(json["rng"]);
    return fp;
}

//-------------------------------------------------------------------------
