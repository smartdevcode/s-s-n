/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ExchangeSignals.hpp"
#include "JsonSerializable.hpp"
#include "taosim/message/Message.hpp"
#include "taosim/simulation/TimeConfig.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <chrono>
#include <memory>

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

class ReplayEventLogger
{
public:
    ReplayEventLogger(
        const fs::path& filepath,
        std::chrono::system_clock::time_point startTimePoint,
        Simulation* simulation) noexcept;

    [[nodiscard]] const fs::path& filepath() const noexcept { return m_filepath; }

    void log(Message::Ptr event);

    static constexpr std::string_view s_header = "date,time,message";

private:
    void updateSink();

    [[nodiscard]] std::unique_ptr<spdlog::sinks::basic_file_sink_st> makeFileSink();
    [[nodiscard]] rapidjson::Document makeLogEntryJson(Message::Ptr msg);

    std::unique_ptr<spdlog::logger> m_logger;
    fs::path m_filepath;
    std::chrono::system_clock::time_point m_startTimePoint;
    Simulation* m_simulation;
    taosim::simulation::TimestampConversionFn m_timeConverter{};
    Timestamp m_currentWindowBegin{};
    fs::path m_currentFilepath;
};

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
