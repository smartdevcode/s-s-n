/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "BookProcessManager.hpp"

#include "Simulation.hpp"
#include "taosim/exchange/ExchangeConfig.hpp"

//-------------------------------------------------------------------------

BookProcessManager::BookProcessManager(
    BookProcessManager::ProcessContainer container,
    BookProcessManager::LoggerContainer loggers,
    std::unique_ptr<ProcessFactory> processFactory,
    decltype(taosim::simulation::SimulationSignals::time)& timeSignal)
    : m_container{std::move(container)},
      m_loggers{std::move(loggers)},
      m_processFactory{std::move(processFactory)}
{
    m_feed = timeSignal.connect([this](Timespan timespan) {
        updateProcesses(timespan);
    });
    for (const auto& [name, bookProcesses] : m_container) {
        const auto& representativeProcess = bookProcesses.front();
        m_updateCounters[name] = UpdateCounter{representativeProcess->updatePeriod()};
    }
}

//-------------------------------------------------------------------------

void BookProcessManager::updateProcesses(Timespan timespan)
{
    for (const auto& [name, bookId2Process] : m_container) {
        std::map<BookId, std::vector<double>> bookId2ProcessValues;
        std::vector<Timestamp> timestamps;
        auto& updateCounter = m_updateCounters.at(name);
        const Timestamp stepsUntilUpdate = updateCounter.stepsUntilUpdate();
        if (const auto len = timespan.end - timespan.begin; len < stepsUntilUpdate) {
            updateCounter.setState(updateCounter.state() + len + 1);
            continue;
        }
        const Timestamp begin = timespan.begin + stepsUntilUpdate;
        const Timestamp stride = updateCounter.period();
        for (const auto& [bookId, process] : views::enumerate(bookId2Process)) {
            std::vector<double> processValues;
            for (Timestamp t = begin; t <= timespan.end; t += stride) {
                process->update(t);
                processValues.push_back(process->value());
            }
            bookId2ProcessValues.insert({bookId, std::move(processValues)});
        }
        for (Timestamp t = begin; t <= timespan.end; t += stride) {
            timestamps.push_back(t);
        }
        m_loggers.at(name)->log(bookId2ProcessValues, timestamps);
        updateCounter.setState((timespan.end - begin) % updateCounter.period());
    }
}

//-------------------------------------------------------------------------

void BookProcessManager::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        for (const auto& [name, bookId2Process] : m_container) {
            rapidjson::Document subJson{rapidjson::kObjectType, &allocator};
            taosim::json::serializeHelper(
                subJson,
                "processes",
                [&](rapidjson::Document& json) {
                    json.SetArray();
                    auto& allocator = json.GetAllocator();
                    for (const auto& process : bookId2Process) {
                        rapidjson::Document processJson{&allocator};
                        process->checkpointSerialize(processJson);
                        json.PushBack(processJson, allocator);
                    }
                });
            m_loggers.at(name)->checkpointSerialize(subJson, "logger");
            json.AddMember(rapidjson::Value{name.c_str(), allocator}, subJson, allocator);
        }
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

std::unique_ptr<BookProcessManager> BookProcessManager::fromXML(
    pugi::xml_node node, Simulation* simulation, taosim::exchange::ExchangeConfig* exchangeConfig)
{
    static constexpr auto ctx = std::source_location::current().function_name();

    if (std::string_view name = node.name(); name != "Books") {
        throw std::invalid_argument(fmt::format(
            "{}: Instantiation node should be 'Books', was '{}'", ctx, name));
    }

    const uint32_t bookCount = node.attribute("instanceCount").as_uint(1);

    auto processFactory = std::make_unique<ProcessFactory>(simulation, exchangeConfig);

    ProcessContainer container;
    LoggerContainer loggers;
    for (pugi::xml_node processNode : node.child("Processes")) {
        ProcessContainer::mapped_type bookId2Process(bookCount);
        for (BookId bookId = 0; bookId < bookCount; ++bookId) {
            bookId2Process[bookId] = processFactory->createFromXML(processNode, simulation->blockIdx()* bookCount + bookId);
        }
        pugi::xml_attribute attr;
        if (attr = processNode.attribute("name"); attr.empty()) {
            throw std::invalid_argument(fmt::format(
                "{}: Node '{}' missing required attribute '{}'",
                ctx,
                processNode.name(),
                "name"));
        }
        const std::string name = attr.as_string();
        container[name] = std::move(bookId2Process);
        loggers[name] = std::make_unique<BookProcessLogger>(
            simulation->logDir() /
                fmt::format("{}.{}-{}.csv",
                    name,
                    simulation->blockIdx() * bookCount,
                    simulation->blockIdx() * bookCount + bookCount - 1),
            container.at(name)
                | views::transform([](const auto& p) { return p->value(); })
                | ranges::to<std::vector>,
            simulation);
    }

    return std::make_unique<BookProcessManager>(
        std::move(container),
        std::move(loggers),
        std::move(processFactory),
        simulation->signals().time);
}

//-------------------------------------------------------------------------

std::unique_ptr<BookProcessManager> BookProcessManager::fromCheckpoint(
    const rapidjson::Value& json, Simulation* simulation, taosim::exchange::ExchangeConfig* exchangeConfig)
{
    auto processFactory = std::make_unique<ProcessFactory>(simulation, exchangeConfig);

    BookProcessManager::ProcessContainer container;
    BookProcessManager::LoggerContainer loggers;
    for (const auto& member : json.GetObject()) {
        const char* name = member.name.GetString();
        const rapidjson::Value& value = member.value;
        auto& processes = container[name];
        const auto& processesJson = value["processes"].GetArray();
        for (const auto& [bookId, processJson] : views::enumerate(processesJson)) {
            processes[bookId] = processFactory->createFromCheckpoint(processJson);
        }
        loggers[name] = BookProcessLogger::fromCheckpoint(value["logger"], simulation);
    }

    return std::make_unique<BookProcessManager>(
        std::move(container),
        std::move(loggers),
        std::move(processFactory),
        simulation->signals().time);
}

//-------------------------------------------------------------------------