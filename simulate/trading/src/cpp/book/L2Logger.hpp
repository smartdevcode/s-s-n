/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "BookSignals.hpp"
#include "taosim/simulation/TimeConfig.hpp"
#include "util.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <chrono>
#include <memory>

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class L2Logger
{
public:
    L2Logger(
        const fs::path& filepath,
        uint32_t depth,
        std::chrono::system_clock::time_point startTimePoint,
        BookSignals& signals,
        Simulation* simulation) noexcept;

    [[nodiscard]] const fs::path& filepath() const noexcept { return m_filepath; }

    static constexpr std::string_view s_header =
        "Date,Time,Symbol,Market,BidVol,BidPrice,AskVol,AskPrice,"
        "QuoteCondition,Time,EndTime,BidLevels,AskLevels";

private:
    void log(const Book* book);
    void updateSink();

    [[nodiscard]] std::unique_ptr<spdlog::sinks::basic_file_sink_st> makeFileSink();
    [[nodiscard]] std::string createEntryAS(const Book* book) const noexcept;

    std::unique_ptr<spdlog::logger> m_logger;
    fs::path m_filepath;
    std::chrono::system_clock::time_point m_startTimePoint;
    boost::signals2::scoped_connection m_feed;
    uint32_t m_depth;
    std::string m_lastLog;
    Simulation* m_simulation;
    taosim::simulation::TimestampConversionFn m_timeConverter{};
    Timestamp m_currentWindowBegin{};
    fs::path m_currentFilepath;
};

//-------------------------------------------------------------------------
