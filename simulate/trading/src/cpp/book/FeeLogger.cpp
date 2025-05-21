/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */

#include "Simulation.hpp"
#include "util.hpp"
#include "taosim/book/FeeLogger.hpp"

#include <fmt/chrono.h>
#include <spdlog/sinks/basic_file_sink.h>

//-------------------------------------------------------------------------

FeeLogger::FeeLogger(const fs::path &filepath, 
    std::chrono::system_clock::time_point startTimePoint,
    decltype(ExchangeSignals::feeLog) &signal,
    const Simulation *simulation) noexcept
    : m_filepath{filepath},
      m_startTimePoint{startTimePoint},
      m_simulation{simulation}
{
    m_logger = std::make_unique<spdlog::logger>(
        "FeeLogger", std::make_unique<spdlog::sinks::basic_file_sink_st>(filepath));
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");
    std::string columns = "Date,Time,AgentId,Role,Fee,FeeRate,Price,Volume";
    m_logger->trace(columns);
    m_logger->flush();
    m_timeConverter = taosim::simulation::timescaleToConverter(m_simulation->config().time().scale);
    m_feed = signal.connect([this](taosim::FeeLogEvent event) { log(event); });
}

//-------------------------------------------------------------------------

void FeeLogger::log(taosim::FeeLogEvent event)
{
    const auto time = m_startTimePoint + m_timeConverter(m_simulation->currentTimestamp());

    std::string entry = fmt::format(
        "{:%Y-%m-%d,%H:%M:%S},{},{},{},{},{},{}",
        time,
        event.agentId,
        event.isMaker ? "Maker" : "Taker",
        event.fee,
        event.feeRate,
        event.price,
        event.volume
    );

    m_logger->trace(entry);
    m_logger->flush();
}

//-------------------------------------------------------------------------