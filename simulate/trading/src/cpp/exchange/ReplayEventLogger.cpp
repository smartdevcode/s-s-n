/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "taosim/exchange/ReplayEventLogger.hpp"

#include "Simulation.hpp"
#include "util.hpp"

#include <fmt/chrono.h>

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

ReplayEventLogger::ReplayEventLogger(
    const fs::path& filepath,
    std::chrono::system_clock::time_point startTimePoint,
    Simulation* simulation) noexcept
    : m_filepath{filepath},
      m_startTimePoint{startTimePoint},
      m_simulation{simulation},
      m_currentFilepath{filepath}
{
    m_timeConverter = taosim::simulation::timescaleToConverter(m_simulation->config().time().scale);

    m_logger = std::make_unique<spdlog::logger>("ReplayEventLogger", makeFileSink());
    m_logger->set_level(spdlog::level::trace);
    m_logger->set_pattern("%v");
    m_logger->trace(s_header);
    m_logger->flush();
}

//-------------------------------------------------------------------------

void ReplayEventLogger::log(Message::Ptr event)
{
    updateSink();

    const auto time = m_startTimePoint + m_timeConverter(m_simulation->currentTimestamp());

    rapidjson::Document json = makeLogEntryJson(event);

    m_logger->trace(fmt::format("{:%Y-%m-%d,%H:%M:%S},{}", time, json::json2str(json)));
    m_logger->flush();
}

//-------------------------------------------------------------------------

void ReplayEventLogger::updateSink()
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

std::unique_ptr<spdlog::sinks::basic_file_sink_st> ReplayEventLogger::makeFileSink()
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

rapidjson::Document ReplayEventLogger::makeLogEntryJson(Message::Ptr msg)
{
    rapidjson::Document json{rapidjson::kObjectType};
    auto& allocator = json.GetAllocator();

    json.AddMember("o", rapidjson::Value{msg->occurrence}, allocator);
    json.AddMember("d", rapidjson::Value{msg->arrival - msg->occurrence}, allocator);
    json.AddMember("s", rapidjson::Value{msg->source.c_str(), allocator}, allocator);
    json.AddMember(
        "t",
        rapidjson::Value{
            fmt::format("{}", fmt::join(msg->targets, std::string{1, Message::s_targetDelim})).c_str(),
            allocator},
        allocator);
    json.AddMember("p", rapidjson::Value{msg->type.c_str(), allocator}, allocator);

    auto makePayloadJson = [&](MessagePayload::Ptr payload) {
        rapidjson::Document payloadJson{rapidjson::kObjectType, &allocator};
        if (const auto pld = std::dynamic_pointer_cast<PlaceOrderMarketPayload>(payload)) {
            payloadJson.AddMember(
                "d", rapidjson::Value{std::to_underlying(pld->direction)}, allocator);
            payloadJson.AddMember(
                "v", rapidjson::Value{util::packDecimal(pld->volume)}, allocator);
            payloadJson.AddMember(
                "l", rapidjson::Value{util::packDecimal(pld->leverage)}, allocator);
            payloadJson.AddMember(
                "b", rapidjson::Value{pld->bookId}, allocator);
            payloadJson.AddMember(
                "n", rapidjson::Value{std::to_underlying(pld->currency)}, allocator);
            json::setOptionalMember(payloadJson, "ci", pld->clientOrderId);
            payloadJson.AddMember(
                "s",
                rapidjson::Value{magic_enum::enum_name(pld->stpFlag).data(), allocator},
                allocator);
            std::visit(
                [&](auto&& flag) {
                    using T = std::remove_cvref_t<decltype(flag)>;
                    if constexpr (std::same_as<T, SettleType>) {
                        payloadJson.AddMember(
                            "f",
                            rapidjson::Value{magic_enum::enum_name(flag).data(), allocator},
                            allocator);
                    } else if constexpr (std::same_as<T, OrderID>) {
                        payloadJson.AddMember("f", rapidjson::Value{flag}, allocator);
                    } else {
                        static_assert(false, "Non-exchaustive visitor");
                    }
                },
                pld->settleFlag);
        }
        else if (const auto pld = std::dynamic_pointer_cast<PlaceOrderLimitPayload>(payload)) {
            payloadJson.AddMember(
                "d", rapidjson::Value{std::to_underlying(pld->direction)}, allocator);
            payloadJson.AddMember(
                "v", rapidjson::Value{util::packDecimal(pld->volume)}, allocator);
            payloadJson.AddMember(
                "p", rapidjson::Value{util::packDecimal(pld->price)}, allocator);
            payloadJson.AddMember(
                "l", rapidjson::Value{util::packDecimal(pld->leverage)}, allocator);
            payloadJson.AddMember(
                "b", rapidjson::Value{pld->bookId}, allocator);
            payloadJson.AddMember(
                "n", rapidjson::Value{std::to_underlying(pld->currency)}, allocator);
            taosim::json::setOptionalMember(payloadJson, "ci", pld->clientOrderId);
            payloadJson.AddMember("y", rapidjson::Value{pld->postOnly}, allocator);
            payloadJson.AddMember(
                "r",
                rapidjson::Value{magic_enum::enum_name(pld->timeInForce).data(), allocator},
                allocator);
            taosim::json::setOptionalMember(payloadJson, "x", pld->expiryPeriod);
            payloadJson.AddMember(
                "s",
                rapidjson::Value{magic_enum::enum_name(pld->stpFlag).data(), allocator},
                allocator);
            std::visit(
                [&](auto&& flag) {
                    using T = std::remove_cvref_t<decltype(flag)>;
                    if constexpr (std::same_as<T, SettleType>) {
                        payloadJson.AddMember(
                            "f",
                            rapidjson::Value{magic_enum::enum_name(flag).data(), allocator},
                            allocator);
                    } else if constexpr (std::same_as<T, OrderID>) {
                        payloadJson.AddMember("f", rapidjson::Value{flag}, allocator);
                    } else {
                        static_assert(false, "Non-exchaustive visitor");
                    }
                },
                pld->settleFlag);
        }
        else if (const auto pld = std::dynamic_pointer_cast<CancelOrdersPayload>(payload)) {
            payloadJson.AddMember(
                "cs",
                [&] {
                    rapidjson::Document cancellationsJson{rapidjson::kArrayType, &allocator};
                    for (const auto& cancellation : pld->cancellations) {
                        rapidjson::Document cancellationJson{rapidjson::kObjectType, &allocator};
                        cancellationJson.AddMember("i", rapidjson::Value{cancellation.id}, allocator);
                        if (cancellation.volume) {
                            cancellationJson.AddMember(
                                "v", util::packDecimal(cancellation.volume.value()), allocator);
                        } else {
                            cancellationJson.AddMember(
                                "v", rapidjson::Value{}.SetNull(), allocator);
                        }
                        cancellationsJson.PushBack(cancellationJson, allocator);
                    }
                    return cancellationsJson;
                }().Move(),
                allocator);
            payloadJson.AddMember("b", rapidjson::Value{pld->bookId}, allocator);
        }
        else if (const auto pld = std::dynamic_pointer_cast<ClosePositionsPayload>(payload)) {
            payloadJson.AddMember(
                "cps",
                [&] {
                    rapidjson::Document closePositionsJson{rapidjson::kArrayType, &allocator};
                    for (const auto& closePosition : pld->closePositions) {
                        rapidjson::Document closePositionJson{rapidjson::kObjectType, &allocator};
                        closePositionJson.AddMember("i", rapidjson::Value{closePosition.id}, allocator);
                        if (closePosition.volume) {
                            closePositionJson.AddMember(
                                "v", util::packDecimal(closePosition.volume.value()), allocator);
                        } else {
                            closePositionJson.AddMember(
                                "v", rapidjson::Value{}.SetNull(), allocator);
                        }
                        closePositionsJson.PushBack(closePositionJson, allocator);
                    }
                    return closePositionsJson;
                }().Move(),
                allocator);
            payloadJson.AddMember("b", rapidjson::Value{pld->bookId}, allocator);
        }
        else if (const auto pld = std::dynamic_pointer_cast<ResetAgentsPayload>(payload)) {
            payloadJson.AddMember(
                "as",
                [&] {
                    rapidjson::Document agentIdsJson{rapidjson::kObjectType, &allocator};
                    for (AgentId agentId : pld->agentIds) {
                        agentIdsJson.PushBack(rapidjson::Value{agentId}.Move(), allocator);
                    }
                    return agentIdsJson;
                }().Move(),
                allocator);
        }
        return payloadJson;
    };

    json.AddMember(
        "pld",
        [&] {
            if (const auto pld = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload)) {
                rapidjson::Document distributedAgentResponsePayloadJson{rapidjson::kObjectType, &allocator};
                distributedAgentResponsePayloadJson.AddMember(
                    "a", rapidjson::Value{pld->agentId}, allocator);
                distributedAgentResponsePayloadJson.AddMember(
                    "pld",
                    makePayloadJson(pld->payload).Move(),
                    allocator);
                return distributedAgentResponsePayloadJson;
            }
            return makePayloadJson(msg->payload);
        }().Move(),
        allocator);

    return json;
}

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------