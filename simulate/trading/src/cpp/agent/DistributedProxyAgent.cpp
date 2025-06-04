/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "DistributedProxyAgent.hpp"

#include "ExchangeAgentMessagePayloads.hpp"
#include "Simulation.hpp"
#include "json_util.hpp"
#include "util.hpp"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <source_location>
#include <chrono>
#include <thread>

//-------------------------------------------------------------------------

DistributedProxyAgent::DistributedProxyAgent(Simulation* simulation)
    : Agent{simulation, "DISTRIBUTED_PROXY_AGENT"}
{}

//-------------------------------------------------------------------------

void DistributedProxyAgent::receiveMessage(Message::Ptr msg)
{
    if (msg->type == "MULTIBOOK_STATE_PUBLISH") {
        return handleBookStatePublish(msg);
    } else if (msg->type == "EVENT_SIMULATION_START") {
        rapidjson::Document json{rapidjson::kObjectType};
        taosim::json::serializeHelper(
            json,
            "messages",
            [&msg](rapidjson::Document& json) {
                json.SetArray();
                auto& allocator = json.GetAllocator();
                rapidjson::Document msgJson{&allocator};
                msg->jsonSerialize(msgJson);
                json.PushBack(msgJson, allocator);
            });
        rapidjson::Document res;
        net::io_context ctx;
        net::co_spawn(
            ctx, asyncSendOverNetwork(json, m_generalMsgEndpoint, res), net::detached);
        ctx.run();
        if (m_testMode) {
            const Timestamp now = simulation()->currentTimestamp();
            for (const rapidjson::Value& response : res["responses"].GetArray()) {
                simulation()->queueMessage(Message::fromJsonResponse(response, now, name()));
            }
        }
    }
    m_messages.push_back(msg);
}

//-------------------------------------------------------------------------

void DistributedProxyAgent::configure(const pugi::xml_node& node)
{
    Agent::configure(node);

    pugi::xml_attribute att;
    if (!(att = node.attribute("host")).empty()) {
        m_host = simulation()->parameters().processString(att.as_string());
    }
    if (!(att = node.attribute("port")).empty()) {
        m_port = simulation()->parameters().processString(att.as_string());
    }
    if (!(att = node.attribute("bookStateEndpoint")).empty()) {
        m_bookStateEndpoint = simulation()->parameters().processString(att.as_string());
    }
    if (!(att = node.attribute("generalMsgEndpoint")).empty()) {
        m_generalMsgEndpoint = simulation()->parameters().processString(att.as_string());
    }
}

//-------------------------------------------------------------------------

void DistributedProxyAgent::handleBookStatePublish(Message::Ptr msg)
{
    rapidjson::Document msgJson;
    auto& allocator = msgJson.GetAllocator();
    msg->jsonSerialize(msgJson);
    msgJson["payload"].AddMember(
        "notices",
        [this, &allocator] -> rapidjson::Document {
            rapidjson::Document messagesJson{&allocator};
            auto serializeMessages = [this](rapidjson::Document& json) {
                json.SetArray();
                auto& allocator = json.GetAllocator();
                for (Message::Ptr message : m_messages) {
                    rapidjson::Document messageJson{&allocator};
                    message->jsonSerialize(messageJson);
                    json.PushBack(messageJson, allocator);
                }
            };
            serializeMessages(messagesJson);
            return messagesJson;
        }(),
        allocator);

    rapidjson::Document res;

    net::io_context ctx;
    net::co_spawn(ctx, asyncSendOverNetwork(msgJson, m_bookStateEndpoint, res), net::detached);
    ctx.run();

    const Timestamp now = simulation()->currentTimestamp();
    for (const rapidjson::Value& response : res["responses"].GetArray()) {
        simulation()->queueMessage(Message::fromJsonResponse(response, now, name()));
    }

    if (!m_messages.empty() && !m_testMode) {
        m_messages.clear();
    }
}

//-------------------------------------------------------------------------

net::awaitable<void> DistributedProxyAgent::asyncSendOverNetwork(
    const rapidjson::Value& reqBody, const std::string& endpoint, rapidjson::Document& resJson)
{
    auto resolver =
        use_nothrow_awaitable.as_default_on(tcp::resolver{co_await this_coro::executor});
    auto tcp_stream =
        use_nothrow_awaitable.as_default_on(beast::tcp_stream{co_await this_coro::executor});

    int attempts = 0;
    // Resolve.
    auto endpointsVariant = co_await (resolver.async_resolve(m_host, m_port) || timeout(1s));
    while (endpointsVariant.index() == 1) {
        fmt::println("tcp::resolver timed out on {}:{}", m_host, m_port);
        std::this_thread::sleep_for(10s);
        endpointsVariant = co_await (resolver.async_resolve(m_host, m_port) || timeout(1s));
    }
    auto [e1, endpoints] = std::get<0>(endpointsVariant);
    while (e1) {
        const auto loc = std::source_location::current();
        simulation()->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_host, m_port, e1.what());
        attempts++;
        fmt::println("Unable to resolve connection to validator at {}:{}{} - Retrying (Attempt {})", m_host, m_port, endpoint, attempts);
        std::this_thread::sleep_for(10s);
        endpointsVariant = co_await (resolver.async_resolve(m_host, m_port) || timeout(1s));
        auto [e11, endpoints1] = std::get<0>(endpointsVariant);
        e1 = e11;
        endpoints = endpoints1;
    }

    // Connect.
    attempts = 0;
    auto connectVariant = co_await (tcp_stream.async_connect(endpoints) || timeout(3s));
    while (connectVariant.index() == 1) {
        fmt::println("tcp_stream::async_connect timed out on {}:{}", m_host, m_port);
        std::this_thread::sleep_for(10s);
        connectVariant = co_await (tcp_stream.async_connect(endpoints) || timeout(3s));
    }
    auto [e2, _2] = std::get<0>(connectVariant);
    while (e2) {
        const auto loc = std::source_location::current();
        simulation()->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_host, m_port, e2.what());
        attempts++;
        fmt::println("Unable to connect to validator at {}:{}{} - Retrying (Attempt {})", m_host, m_port, endpoint, attempts);
        std::this_thread::sleep_for(10s);
        connectVariant = co_await (tcp_stream.async_connect(endpoints) || timeout(3s));
        auto [e21, _21] = std::get<0>(connectVariant);
        e2 = e21;
        _2 = _21;
    }

    // Create the request.
    const auto req = makeHttpRequest(endpoint, taosim::json::json2str(reqBody));

    // Send the request.
    attempts = 0;
    auto writeVariant = co_await (http::async_write(tcp_stream, req) || timeout(10s));
    while (writeVariant.index() == 1) {
        fmt::println("http::async_write timed out on {}:{}", m_host, m_port);
        std::this_thread::sleep_for(10s);
        writeVariant = co_await (http::async_write(tcp_stream, req) || timeout(10s));
    }
    auto [e3, _3] = std::get<0>(writeVariant);
    while (e3) {
        const auto loc = std::source_location::current();
        simulation()->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_host, m_port, e3.what());
        attempts++;
        fmt::println("Unable to send request to validator at {}:{}{} - Retrying (Attempt {})", m_host, m_port, endpoint, attempts);
        std::this_thread::sleep_for(10s);
        writeVariant = co_await (http::async_write(tcp_stream, req) || timeout(10s));
        auto [e31, _31] = std::get<0>(writeVariant);
        e3 = e31;
        _3 = _31;
    }

    // Receive the response.
    attempts = 0;
    beast::flat_buffer buf;
    http::response<http::string_body> res;
    auto readVariant = co_await (http::async_read(tcp_stream, buf, res) || timeout(30s));
    while (readVariant.index() == 1) {
        fmt::println("http::async_read timed out on {}:{}", m_host, m_port);
        std::this_thread::sleep_for(10s);
        readVariant = co_await (http::async_read(tcp_stream, buf, res) || timeout(30s));
    }
    auto [e4, _4] = std::get<0>(readVariant);
    while (e4) {
        const auto loc = std::source_location::current();
        simulation()->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_host, m_port, e4.what());
        attempts++;
        fmt::println("Unable to read response from validator at {}:{}{} - Retrying (Attempt {})", m_host, m_port, endpoint, attempts);
        std::this_thread::sleep_for(10s);
        readVariant = co_await (http::async_read(tcp_stream, buf, res) || timeout(30s));
        auto [e41, _41] = std::get<0>(readVariant);
        e4 = e41;
        _4 = _41;
    }

    resJson.Parse(res.body().c_str());
}

//-------------------------------------------------------------------------

http::request<http::string_body> DistributedProxyAgent::makeHttpRequest(
    const std::string& target, const std::string& body)
{
    http::request<http::string_body> req;
    req.method(http::verb::get);
    req.target(target);
    req.version(11);
    req.set(http::field::host, m_host);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    return req;
}

//-------------------------------------------------------------------------
