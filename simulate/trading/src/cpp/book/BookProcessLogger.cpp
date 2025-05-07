/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "BookProcessLogger.hpp"

#include "Simulation.hpp"

#include <boost/algorithm/string.hpp>
#include <spdlog/sinks/basic_file_sink.h>

//-------------------------------------------------------------------------

BookProcessLogger::BookProcessLogger(const fs::path& filepath, const std::vector<double>& X0s)
    : m_filepath{filepath}
{
    fs::remove(filepath);
    m_logger = std::make_unique<spdlog::logger>(
        fmt::format("BookProcessLogger-{}", filepath.stem().c_str()),
        std::make_unique<spdlog::sinks::basic_file_sink_st>(filepath));
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");
    m_logger->trace(fmt::format(
        "{},Timestamp\n"
        "{},0",
        fmt::join(views::iota(0u, X0s.size()), ","),
        fmt::join(X0s, ",")));
    m_logger->flush();
}

//-------------------------------------------------------------------------

void BookProcessLogger::log(
    const std::map<BookId, std::vector<double>>& entries, std::span<Timestamp> timestamps)
{
    for (auto [i, t] : views::enumerate(timestamps)) {
        std::vector<double> values;
        for (BookId bookId : views::keys(entries)) {
            values.push_back(entries.at(bookId)[i]);
        }
        m_logger->trace(fmt::format("{},{}", fmt::join(values, ","), t));
        m_logger->flush();
    }
}

//-------------------------------------------------------------------------

void BookProcessLogger::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember(
            "filename", rapidjson::Value{m_filepath.filename().c_str(), allocator}, allocator);
        json.AddMember(
            "log",
            rapidjson::Value{
                [this] {
                    std::ifstream logFile{m_filepath};
                    std::ostringstream oss;
                    oss << logFile.rdbuf();
                    return oss.str();
                }().c_str(),
                allocator},
            allocator);
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

std::unique_ptr<BookProcessLogger> BookProcessLogger::fromCheckpoint(
    const rapidjson::Value& json, Simulation* simulation)
{
    auto logger = std::unique_ptr<BookProcessLogger>{new BookProcessLogger{
        simulation->logDir() / json["filename"].GetString()}};
    logger->m_logger->trace([&] {
        std::string log = json["log"].GetString();
        boost::erase_last(log, "\n");
        return log;
    }());
    logger->m_logger->flush();
    return logger;
}

//-------------------------------------------------------------------------

BookProcessLogger::BookProcessLogger(const fs::path& filepath)
    : m_filepath{filepath}
{
    fs::remove(filepath);
    m_logger = std::make_unique<spdlog::logger>(
        fmt::format("BookProcessLogger-{}", filepath.stem().c_str()),
        std::make_unique<spdlog::sinks::basic_file_sink_st>(filepath));
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");
}

//-------------------------------------------------------------------------
