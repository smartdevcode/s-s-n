/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ExchangeSignals.hpp"
#include "JsonSerializable.hpp"
#include "TimeConfig.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>

using namespace taosim::exchange;

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class L3EventLogger
{
public:
    L3EventLogger(
        const fs::path& filepath,
        std::chrono::system_clock::time_point startTimePoint,
        decltype(ExchangeSignals::L3)& signal,
        const Simulation* simulation) noexcept;

    [[nodiscard]] const fs::path& filepath() const noexcept { return m_filepath; }

private:
    void log(taosim::L3LogEvent event);

    std::unique_ptr<spdlog::logger> m_logger;
    fs::path m_filepath;
    std::chrono::system_clock::time_point m_startTimePoint;
    bs2::scoped_connection m_feed;
    const Simulation* m_simulation;
    taosim::simulation::TimestampConversionFn m_timeConverter{};
};

//-------------------------------------------------------------------------
