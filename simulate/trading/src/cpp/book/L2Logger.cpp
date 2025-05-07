/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "L2Logger.hpp"

#include "Simulation.hpp"

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
      m_feed{signals.L2.connect([this](Entry entry) { log(entry); })},
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

void L2Logger::log(Entry entry)
{
    std::string newLog = formatEntryAS(entry);
    if (newLog != "" && newLog != m_lastLog) {
        m_logger->trace(newLog);
        m_logger->flush();
    }
    m_lastLog = newLog;
}

//-------------------------------------------------------------------------

std::string L2Logger::formatEntryAS(Entry entry) const noexcept
{
    const auto time = m_startTimePoint + m_timeConverter(m_simulation->currentTimestamp());

    //fmt::println("{}", taosim::json::json2str(entry));
    std::stringstream sstrm;
    // Date,Time
    sstrm << fmt::format("{:%Y-%m-%d,%H:%M:%S},", time);
    // Symbol,Market
    const BookId bookId = [this] {
        BookId bookId{};
        const char bookIdChar = m_filepath.stem().string().back();
        std::from_chars(&bookIdChar, &bookIdChar + 1, bookId);
        return bookId;
    }();
    sstrm << fmt::format("S{:0{}}-SIMU,RAYX,", bookId, 3);
    // BidVol,BidPrice,AskVol,AskPrice
    if (!entry["bid"].IsNull() && entry["bid"].GetArray().Size() > 0) {
        sstrm << fmt::format(
            "{},{},",
            entry["bid"][0]["volume"].GetDouble(),
            entry["bid"][0]["price"].GetDouble());
    } else {
        return "";
    }
    if (!entry["ask"].IsNull() && entry["ask"].GetArray().Size() > 0) {
        sstrm << fmt::format(
            "{},{},",
            entry["ask"][0]["volume"].GetDouble(),
            entry["ask"][0]["price"].GetDouble());
    } else {
        return "";
    }
    // QuoteCondition,Time,EndTime (legacy)
    sstrm << ",,,";
    // BidLevels,AskLevels
    auto serializeLevels = [&](const char* side) -> std::string {
        if (entry[side].IsNull()) return {};
        const auto& levels = entry[side].GetArray();
        std::stringstream ss;
        if (std::string_view{side} == "bid") {
            const auto& level = levels[std::min(m_depth,levels.Size()) - 1];
            ss << fmt::format(
                "({}@{})", level["volume"].GetDouble(), level["price"].GetDouble());
            for (int32_t i = static_cast<int32_t>(std::min(m_depth,levels.Size()))-2; i >= 0; --i) {
                const auto& level = levels[i];
                ss << fmt::format(
                    " ({}@{})", level["volume"].GetDouble(), level["price"].GetDouble());
            }
        } else {
            const auto& level = levels[0];
            ss << fmt::format(
                "({}@{})", level["volume"].GetDouble(), level["price"].GetDouble());
            for (int32_t i = 1; i < std::min(levels.Size(), m_depth); ++i) {
                const auto& level = levels[i];
                ss << fmt::format(
                    " ({}@{})", level["volume"].GetDouble(), level["price"].GetDouble());
            }
        }
        ss << ",";
        return ss.str();
    };
    sstrm << serializeLevels("bid");
    sstrm << serializeLevels("ask");
    return sstrm.str();
}

//-------------------------------------------------------------------------
