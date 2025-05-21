/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "L2Logger.hpp"

#include "Book.hpp"
#include "Simulation.hpp"
#include "common.hpp"

#include <fmt/chrono.h>
#include <spdlog/sinks/basic_file_sink.h>

//-------------------------------------------------------------------------

L2Logger::L2Logger(
    const fs::path& filepath,
    uint32_t depth,
    std::chrono::system_clock::time_point startTimePoint,
    BookSignals& signals,
    const Simulation* simulation) noexcept
    : m_filepath{filepath},
      m_depth{std::max(depth, 1u)},
      m_startTimePoint{startTimePoint},
      m_feed{signals.L2.connect([this](const Book* book) { log(book); })},
      m_simulation{simulation}
{
    m_logger = std::make_unique<spdlog::logger>(
        "L2Logger", std::make_unique<spdlog::sinks::basic_file_sink_st>(filepath));
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");

    m_timeConverter = taosim::simulation::timescaleToConverter(m_simulation->config().time().scale);

    m_logger->trace(
        "Date,Time,Symbol,Market,BidVol,BidPrice,AskVol,AskPrice,"
        "QuoteCondition,Time,EndTime,BidLevels,AskLevels");
    m_logger->flush();
}

//-------------------------------------------------------------------------

const fs::path& L2Logger::filepath() const noexcept
{
    return m_filepath;
}

//-------------------------------------------------------------------------

void L2Logger::log(const Book* book)
{
    std::string newLog = createEntryAS(book);
    if (newLog != "" && newLog != m_lastLog) {
        m_logger->trace(newLog);
        m_logger->flush();
    }
    m_lastLog = newLog;
}

//-------------------------------------------------------------------------

std::string L2Logger::createEntryAS(const Book* book) const noexcept
{
    if (book->buyQueue().empty() || book->sellQueue().empty()) [[unlikely]] {
        return {};
    }

    auto levelFormatter = [](const auto& level) -> std::string {
        return fmt::format("({}@{})", level.volume(), level.price());
    };

    return fmt::format(
        // Date,Time,
        "{:%Y-%m-%d,%H:%M:%S},"
        // Symbol,Market,
        "S{:0{}}-SIMU,RAYX,"
        // BidVol,BidPrice,
        "{},{},"
        // AskVol,AskPrice,
        "{},{},"
        // QuoteCondition,Time,EndTime, (legacy)
        ",,,"
        // BidLevels,
        "{},"
        // AskLevels,
        "{},",
        m_startTimePoint + m_timeConverter(m_simulation->currentTimestamp()),
        book->id(), 3,
        book->buyQueue().back().volume(), book->buyQueue().back().price(),
        book->sellQueue().front().volume(), book->sellQueue().front().price(),
        fmt::join(
            book->buyQueue()
            | views::reverse
            | views::take(m_depth)
            | views::reverse
            | views::transform(levelFormatter),
            " "),
        fmt::join(
            book->sellQueue()
            | views::take(m_depth)
            | views::transform(levelFormatter),
            " "));
}

//-------------------------------------------------------------------------
