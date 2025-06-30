/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "L3EventLogger.hpp"

#include "Simulation.hpp"
#include "util.hpp"

#include <fmt/chrono.h>

//-------------------------------------------------------------------------

L3EventLogger::L3EventLogger(
    const fs::path& filepath,
    std::chrono::system_clock::time_point startTimePoint,
    decltype(ExchangeSignals::L3)& signal,
    Simulation* simulation) noexcept
    : m_filepath{filepath},
      m_startTimePoint{startTimePoint},
      m_simulation{simulation},
      m_currentFilepath{filepath}
{
    m_timeConverter = taosim::simulation::timescaleToConverter(m_simulation->config().time().scale);

    m_logger = std::make_unique<spdlog::logger>("L3Logger", makeFileSink());
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");
    m_logger->trace(s_header);
    m_logger->flush();

    m_feed = signal.connect([this](taosim::L3LogEvent event) { log(event); });
}

//-------------------------------------------------------------------------

void L3EventLogger::log(taosim::L3LogEvent event)
{
    updateSink();

    const auto time = m_startTimePoint + m_timeConverter(m_simulation->currentTimestamp());

    rapidjson::Document json = std::visit(
        [&](auto&& item) {
            rapidjson::Document json;
            item.jsonSerialize(json);
            json.AddMember("eventId", rapidjson::Value{event.id}, json.GetAllocator());
            return json;
        },
        event.item);

    m_logger->trace(fmt::format("{:%Y-%m-%d,%H:%M:%S},{}", time, taosim::json::json2str(json)));
    m_logger->flush();
}

//-------------------------------------------------------------------------

void L3EventLogger::updateSink()
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

std::unique_ptr<spdlog::sinks::basic_file_sink_st> L3EventLogger::makeFileSink()
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