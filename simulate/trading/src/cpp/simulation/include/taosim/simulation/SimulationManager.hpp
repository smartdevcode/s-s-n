/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Simulation.hpp>
#include "net.hpp"

#include <boost/asio.hpp>
#include <pugixml.hpp>

#include <memory>
#include <vector>

//-------------------------------------------------------------------------

namespace taosim::simulation
{

//-------------------------------------------------------------------------

struct SimulationBlockInfo
{
    uint32_t count;
    uint32_t dimension;
};

//-------------------------------------------------------------------------

struct NetworkingInfo
{
    std::string host, port, bookStateEndpoint, generalMsgEndpoint;
};

//-------------------------------------------------------------------------

class SimulationManager
{
public:
    void runSimulations();
    void publishStartInfo();
    void publishState();

    [[nodiscard]] rapidjson::Document makeStateJson() const;
    [[nodiscard]] rapidjson::Document makeCollectiveBookStateJson() const;
    [[nodiscard]] bool online() const noexcept { return !m_netInfo.host.empty() && !m_netInfo.port.empty(); }

    static std::unique_ptr<SimulationManager> fromConfig(const fs::path& path);

private:
    SimulationBlockInfo m_blockInfo;
    std::unique_ptr<boost::asio::thread_pool> m_threadPool;
    std::vector<std::unique_ptr<Simulation>> m_simulations;
    fs::path m_logDir;
    Timestamp m_gracePeriod;
    NetworkingInfo m_netInfo;
    UnsyncSignal<void()> m_stepSignal;

    void setupLogDir(pugi::xml_node node);

    net::awaitable<void> asyncSendOverNetwork(
        const rapidjson::Value& reqBody, const std::string& endpoint, rapidjson::Document& resJson);
    http::request<http::string_body> makeHttpRequest(
        const std::string& target, const std::string& body);
};

//-------------------------------------------------------------------------

}  // namespace taosim::simulation

//-------------------------------------------------------------------------