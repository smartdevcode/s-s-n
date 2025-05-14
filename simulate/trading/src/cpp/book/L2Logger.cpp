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

    const auto time = m_startTimePoint + m_timeConverter(m_simulation->currentTimestamp());

    std::stringstream sstrm;

    // Date,Time
    sstrm << fmt::format("{:%Y-%m-%d,%H:%M:%S},", time);
    // Symbol,Market
    sstrm << fmt::format("S{:0{}}-SIMU,RAYX,", book->id(), 3);
    // BidVol,BidPrice,AskVol,AskPrice
    sstrm << fmt::format(
        "{},{},",
        book->buyQueue().back().volume(),
        book->buyQueue().back().price());
    sstrm << fmt::format(
        "{},{},",
        book->sellQueue().front().volume(),
        book->sellQueue().front().price());
    // QuoteCondition,Time,EndTime (legacy)
    sstrm << ",,,";
    // BidLevels,AskLevels
    auto serializeLevels = [&]<std::same_as<OrderDirection> auto Side> -> std::string {
        auto levelFormatter = [](const auto& level) {
            return fmt::format("({}@{})", level.volume(), level.price());
        };
        if constexpr (Side == OrderDirection::BUY) {
            auto levels = book->buyQueue() | views::reverse | views::take(m_depth) | views::reverse;
            return fmt::format(
                "{},", fmt::join(levels | views::transform(levelFormatter), " "));
        }
        else {
            auto levels = book->sellQueue() | views::take(m_depth);
            return fmt::format(
                "{},", fmt::join(levels | views::transform(levelFormatter), " "));
        }
    };
    sstrm << serializeLevels.operator()<OrderDirection::BUY>();
    sstrm << serializeLevels.operator()<OrderDirection::SELL>();

    return sstrm.str();
}

//-------------------------------------------------------------------------
