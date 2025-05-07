/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Agent.hpp"
#include "MultiBookMessagePayloads.hpp"
#include "net.hpp"

#include "rapidjson/document.h"

#include <vector>

//-------------------------------------------------------------------------

class DistributedProxyAgent : public Agent
{
public:
    DistributedProxyAgent(Simulation* simulation);

    virtual void receiveMessage(Message::Ptr msg) override;
    virtual void configure(const pugi::xml_node& node) override;

    void setTestMode(bool flag) noexcept { m_testMode = flag; }

private:
    net::awaitable<void> asyncSendOverNetwork(
        const rapidjson::Value& reqBody, const std::string& endpoint, rapidjson::Document& resJson);
    http::request<http::string_body>
        makeHttpRequest(const std::string& target, const std::string& body);

    void handleBookStatePublish(Message::Ptr msg);

    std::string m_host;
    std::string m_port;
    std::string m_bookStateEndpoint;
    std::string m_generalMsgEndpoint;
    std::vector<Message::Ptr> m_messages;
    bool m_testMode{};
};

//-------------------------------------------------------------------------
