/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "L2Logger.hpp"

#include "Book.hpp"
#include "Simulation.hpp"
#include "common.hpp"

#include <fmt/chrono.h>

//-------------------------------------------------------------------------

L2Logger::L2Logger(
    const fs::path& filepath,
    uint32_t depth,
    std::chrono::system_clock::time_point startTimePoint,
    BookSignals& signals,
    Simulation* simulation) noexcept
    : m_filepath{filepath},
      m_depth{std::max(depth, 1u)},
      m_startTimePoint{startTimePoint},
      m_feed{signals.L2.connect([this](const Book* book) { log(book); })},
      m_simulation{simulation},
      m_currentFilepath{filepath}
{
    m_timeConverter = taosim::simulation::timescaleToConverter(m_simulation->config().time().scale);

    m_logger = std::make_unique<spdlog::logger>("L2Logger", makeFileSink());
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");

    m_logger->trace(s_header);
    m_logger->flush();
}

//-------------------------------------------------------------------------

void L2Logger::log(const Book* book)
{
    updateSink();

    const std::string newLog = createEntryAS(book);
    if (newLog != "" && newLog != m_lastLog) {
        m_logger->trace(newLog);
        m_logger->flush();
    }
    m_lastLog = newLog;
}

//-------------------------------------------------------------------------

void L2Logger::updateSink()
{
    if (!m_simulation->logWindow()) {
        if (m_currentFilepath != m_filepath) [[unlikely]] {
            m_currentWindowBegin = taosim::simulation::kLogWindowMax;
            m_logger->sinks().clear();
            m_logger->sinks().push_back(makeFileSink());
            m_logger->set_pattern("%v");
            m_logger->trace(s_header);
            m_logger->flush();
        }
        return;
    }
    const auto end = std::min(
        m_currentWindowBegin + m_simulation->logWindow(), taosim::simulation::kLogWindowMax);
    const bool withinWindow = m_simulation->time().current < end;
    if (withinWindow) [[likely]] return;
    m_currentWindowBegin += m_simulation->logWindow();
    if (m_currentWindowBegin > taosim::simulation::kLogWindowMax) {
        m_currentWindowBegin = taosim::simulation::kLogWindowMax;
        m_simulation->logWindow() = {};
    }
    m_logger->sinks().clear();
    m_logger->sinks().push_back(makeFileSink());
    m_logger->set_pattern("%v");
    m_logger->trace(s_header);
    m_logger->flush();
}

//-------------------------------------------------------------------------

std::unique_ptr<spdlog::sinks::basic_file_sink_st> L2Logger::makeFileSink()
{
    m_currentFilepath = [this] {
        if (!m_simulation->logWindow()) return m_filepath;
        return fs::path{fmt::format(
            "{}.{}-{}.log",
            (m_filepath.parent_path() / m_filepath.stem()).generic_string(),
            taosim::simulation::logFormatTime(m_timeConverter(m_currentWindowBegin)),
            taosim::simulation::logFormatTime(
                m_timeConverter(m_currentWindowBegin + m_simulation->logWindow())))};
    }();
    return std::make_unique<spdlog::sinks::basic_file_sink_st>(m_currentFilepath);
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
