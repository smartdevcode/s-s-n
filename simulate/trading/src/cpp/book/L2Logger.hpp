/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "BookSignals.hpp"
#include "TimeConfig.hpp"
#include "util.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class L2Logger
{
public:
    using Entry = decltype(BookSignals::L2)::argument_type;

    L2Logger(
        const fs::path& filepath,
        uint32_t depth,
        std::chrono::system_clock::time_point startTimePoint,
        BookSignals& signals,
        const Simulation* simulation) noexcept;

    [[nodiscard]] const fs::path& filepath() const noexcept;

private:
    void log(const Book* book);
    [[nodiscard]] std::string createEntryAS(const Book* book) const noexcept;

    std::unique_ptr<spdlog::logger> m_logger;
    fs::path m_filepath;
    std::chrono::system_clock::time_point m_startTimePoint;
    boost::signals2::scoped_connection m_feed;
    uint32_t m_depth;
    std::string m_lastLog;
    const Simulation* m_simulation;
    taosim::simulation::TimestampConversionFn m_timeConverter{};
};

//-------------------------------------------------------------------------
