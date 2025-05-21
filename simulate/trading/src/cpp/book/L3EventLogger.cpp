/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "L3EventLogger.hpp"

#include "Simulation.hpp"
#include "util.hpp"

#include <fmt/chrono.h>
#include <spdlog/sinks/basic_file_sink.h>

//-------------------------------------------------------------------------

L3EventLogger::L3EventLogger(
    const fs::path& filepath,
    std::chrono::system_clock::time_point startTimePoint,
    decltype(ExchangeSignals::L3)& signal,
    const Simulation* simulation) noexcept
    : m_filepath{filepath},
      m_startTimePoint{startTimePoint},
      m_simulation{simulation}
{
    m_logger = std::make_unique<spdlog::logger>(
        "L3Logger", std::make_unique<spdlog::sinks::basic_file_sink_st>(filepath));
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");

    m_timeConverter = taosim::simulation::timescaleToConverter(m_simulation->config().time().scale);
    m_feed = signal.connect([this](taosim::L3LogEvent event) { log(event); });

    m_logger->trace("date,time,event");
    m_logger->flush();
}

//-------------------------------------------------------------------------

void L3EventLogger::log(taosim::L3LogEvent event)
{   
    const auto time = m_startTimePoint + m_timeConverter(m_simulation->currentTimestamp());

    rapidjson::Document json = std::visit(
        [&](auto&& item) {
            rapidjson::Document json;
            item.jsonSerialize(json);
            json.AddMember("eventId", rapidjson::Value{event.id}, json.GetAllocator());
            return json;
        },
        event.item);

    std::ostringstream oss;
    oss << fmt::format("{:%Y-%m-%d,%H:%M:%S},", time)
        << taosim::json::json2str(json);

    m_logger->trace(oss.str());
    m_logger->flush();
}

//-------------------------------------------------------------------------