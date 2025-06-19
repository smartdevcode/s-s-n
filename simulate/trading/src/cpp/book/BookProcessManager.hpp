/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "BookProcessLogger.hpp"
#include "CheckpointSerializable.hpp"
#include "Process.hpp"
#include "ProcessFactory.hpp"
#include "taosim/simulation/SimulationSignals.hpp"
#include "UpdateCounter.hpp"
#include "common.hpp"

#include <pugixml.hpp>

//-------------------------------------------------------------------------

class Simulation;

namespace taosim::exchange
{

class ExchangeConfig;

}  // namespace taosim::exchange

//-------------------------------------------------------------------------

class BookProcessManager : public CheckpointSerializable
{
public:
    using ProcessContainer = std::map<std::string, std::vector<std::unique_ptr<Process>>>;
    using LoggerContainer = std::map<std::string, std::unique_ptr<BookProcessLogger>>;
    using UpdateCounterContainer = std::map<std::string, UpdateCounter>;

    BookProcessManager(
        ProcessContainer container,
        LoggerContainer loggers,
        std::unique_ptr<ProcessFactory> processFactory,
        decltype(taosim::simulation::SimulationSignals::time)& timeSignal,
        Timestamp updatePeriod);

    [[nodiscard]] auto&& operator[](this auto&& self, const std::string& name)
    {
        return self.m_container[name];
    }

    [[nodiscard]] auto&& at(this auto&& self, const std::string& name)
    {
        return self.m_container.at(name);
    }

    void updateProcesses(Timespan timespan);

    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static std::unique_ptr<BookProcessManager> fromXML(
        pugi::xml_node node, Simulation* simulation, taosim::exchange::ExchangeConfig* exchangeConfig);
    [[nodiscard]] static std::unique_ptr<BookProcessManager> fromCheckpoint(
        const rapidjson::Value& json, Simulation* simulation, taosim::exchange::ExchangeConfig* exchangeConfig);

private:
    ProcessContainer m_container;
    LoggerContainer m_loggers;
    std::unique_ptr<ProcessFactory> m_processFactory;
    bs2::scoped_connection m_feed;
    UpdateCounterContainer m_updateCounters;
};

//-------------------------------------------------------------------------