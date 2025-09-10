/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "taosim/simulation/SimulationManager.hpp"
#include "taosim/simulation/util.hpp"
#include "MultiBookMessagePayloads.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <date/date.h>
#include <date/tz.h>
#include <fmt/format.h>

#include <barrier>
#include <latch>
#include <ranges>
#include <source_location>
#include <thread>

//-------------------------------------------------------------------------

namespace taosim::simulation
{

//-------------------------------------------------------------------------

void SimulationManager::runSimulations()
{
    std::barrier barrier{
        m_blockInfo.count,
        [&] {
            publishState();
            m_stepSignal();
        }};
    std::latch latch{m_blockInfo.count};

    publishStartInfo();

    for (auto& simulation : m_simulations) {
        boost::asio::post(
            *m_threadPool,
            [&] {
                simulation->simulate(barrier);
                latch.count_down();
            });
    }
    latch.wait();

    publishEndInfo();
}

//-------------------------------------------------------------------------

void SimulationManager::runReplay(const fs::path& replayDir, BookId bookId)
{
    static constexpr auto ctx = std::source_location::current().function_name();

    auto it = ranges::find_if(
        m_simulations,
        [&](const auto& sim) { return sim->blockIdx() == bookId / m_blockInfo.dimension; });
    if (it == m_simulations.end()) {
        throw std::runtime_error{fmt::format(
            "{}: Could not find simulation matching bookId {} within {}; "
            "blockInfo was {{.count = {}, .dimension = {}}}",
            ctx, bookId, replayDir.c_str(), m_blockInfo.count, m_blockInfo.dimension)};
    }
    const auto& simulation = *it;

    auto replayLogFiles = [&] {
        std::vector<fs::path> replayLogFiles;
        const std::regex pat{fmt::format("^Replay-{}{}", bookId, R"(\.\d{8}-\d{8}.log$)")};
        for (const auto& entry : fs::directory_iterator(replayDir)) {
            const auto path = entry.path();
            if (entry.is_regular_file() && std::regex_match(path.filename().string(), pat)) {
                replayLogFiles.push_back(path);
            }
        }
        ranges::sort(replayLogFiles);
        return replayLogFiles;
    }();

    [&] {
        std::vector<fs::path> replayBalancesPaths;
        const std::regex pat{R"(^Replay-Balances-\d+-\d+\.json$)"};
        for (const auto& entry : fs::directory_iterator(replayDir)) {
            const auto path = entry.path();
            if (entry.is_regular_file() && std::regex_match(path.filename().string(), pat)) {
                replayBalancesPaths.push_back(path);
            }
        }
        ranges::sort(replayBalancesPaths);
        for (const auto& [simulation, path] : views::zip(m_simulations, replayBalancesPaths)) {
            rapidjson::Document balancesJson = json::loadJson(path);
            for (const auto& member : balancesJson.GetObject()) {
                const auto name = member.name.GetString();
                const AgentId agentId = std::stoi(name);
                BookId bookId{};
                for (const auto& balsJson : balancesJson[name].GetArray()) {
                    auto& bals = simulation->exchange()->accounts().at(agentId).at(bookId);
                    bals.base = taosim::accounting::Balance(
                        taosim::json::getDecimal(balsJson["base"]),
                        "",
                        bals.m_roundParams.baseDecimals);
                    bals.quote = taosim::accounting::Balance(
                        taosim::json::getDecimal(balsJson["quote"]),
                        "",
                        bals.m_roundParams.quoteDecimals);
                    ++bookId;
                }
            }
        }
    }();

    auto makePayload = [&](const rapidjson::Value& json) -> MessagePayload::Ptr {
        const std::string msgType{json["p"].GetString()};
        const auto& payloadJson =
            msgType.starts_with("DISTRIBUTED") ? json["pld"]["pld"] : json["pld"];
        if (msgType.ends_with("PLACE_ORDER_MARKET")) {
            return MessagePayload::create<PlaceOrderMarketPayload>(
                OrderDirection{payloadJson["d"].GetUint()},
                json::getDecimal(payloadJson["v"]),
                json::getDecimal(payloadJson["l"]),
                bookId,
                Currency{payloadJson["n"].GetUint()},
                payloadJson["ci"].IsNull()
                    ? std::nullopt : std::make_optional(payloadJson["ci"].GetUint()),
                magic_enum::enum_cast<taosim::STPFlag>(
                    std::string{payloadJson["s"].GetString()}).value(),
                [&] -> taosim::SettleFlag {
                    if (payloadJson["f"].IsUint()) {
                        return payloadJson["f"].GetUint();
                    } else if (payloadJson["f"].IsString()) {
                        return magic_enum::enum_cast<taosim::SettleType>(
                            std::string{payloadJson["f"].GetString()}).value();
                    } else {
                        throw std::runtime_error{fmt::format(
                            "{}: Unrecogized 'settleFlag': {}", ctx, json::json2str(payloadJson["f"]))};
                    }
                }());
        }
        else if (msgType.ends_with("PLACE_ORDER_LIMIT")) {
            return MessagePayload::create<PlaceOrderLimitPayload>(
                OrderDirection{payloadJson["d"].GetUint()},
                json::getDecimal(payloadJson["v"]),
                json::getDecimal(payloadJson["p"]),
                json::getDecimal(payloadJson["l"]),
                bookId,
                Currency{payloadJson["n"].GetUint()},
                payloadJson["ci"].IsNull()
                    ? std::nullopt : std::make_optional(payloadJson["ci"].GetUint()),
                payloadJson["y"].GetBool(),
                magic_enum::enum_cast<taosim::TimeInForce>(
                    std::string{payloadJson["r"].GetString()}).value(),
                payloadJson["x"].IsNull()
                    ? std::nullopt : std::make_optional<Timestamp>(payloadJson["x"].GetUint64()),
                magic_enum::enum_cast<taosim::STPFlag>(
                    std::string{payloadJson["s"].GetString()}).value(),
                [&] -> taosim::SettleFlag {
                    if (payloadJson["f"].IsUint()) {
                        return payloadJson["f"].GetUint();
                    } else if (payloadJson["f"].IsString()) {
                        return magic_enum::enum_cast<taosim::SettleType>(
                            std::string{payloadJson["f"].GetString()}).value();
                    } else {
                        throw std::runtime_error{fmt::format(
                            "{}: Unrecogized 'settleFlag': {}", ctx, json::json2str(payloadJson["f"]))};
                    }
                }());
        }
        else if (msgType.ends_with("CANCEL_ORDERS")) {
            return MessagePayload::create<CancelOrdersPayload>(
                [&] {
                    std::vector<Cancellation> cancellations;
                    for (const auto& cancellationJson : payloadJson["cs"].GetArray()) {
                        cancellations.emplace_back(
                            cancellationJson["i"].GetUint(),
                            cancellationJson["v"].IsNull()
                                ? std::nullopt : std::make_optional(json::getDecimal(cancellationJson["v"])));
                    }
                    return cancellations;
                }(),
                bookId);
        }
        else if (msgType.ends_with("CLOSE_POSITIONS")) {
            return MessagePayload::create<ClosePositionsPayload>(
                [&] {
                    std::vector<ClosePosition> closePositions;
                    for (const auto& closePositionJson : payloadJson["cps"].GetArray()) {
                        closePositions.emplace_back(
                            closePositionJson["i"].GetUint(),
                            closePositionJson["v"].IsNull()
                                ? std::nullopt : std::make_optional(json::getDecimal(closePositionJson["v"])));
                    }
                    return closePositions;
                }(),
                bookId);
        }
        else if (msgType.ends_with("RESET_AGENT")) {
            return MessagePayload::create<ResetAgentsPayload>(
                [&] {
                    std::vector<AgentId> agentIds;
                    for (const auto& agentIdJson : payloadJson["as"].GetArray()) {
                        agentIds.push_back(agentIdJson.GetInt());
                    }
                    return agentIds;
                }());
        }
        throw std::runtime_error{fmt::format(
            "{}: Unexpected message type encountered during replay: {}", ctx, msgType)};
    };

    auto createMessageFromLogFileEntry = [&](const std::string& entry, size_t lineCounter) {
        const auto jsonEntryStr = entry.substr(
            boost::algorithm::find_nth(entry, ",", 1).begin() - entry.begin() + 1);
        rapidjson::Document json;
        if (json.Parse(jsonEntryStr.c_str()).HasParseError()) {
            throw std::runtime_error{fmt::format(
                "{}: Error parsing log file entry at line {}: {}", ctx, lineCounter, entry)};
        }
        const std::string msgType{json["p"].GetString()};
        return Message::create(
            json["o"].GetUint64(),
            json["o"].GetUint64() + json["d"].GetUint64(),
            json["s"].GetString(),
            json["t"].GetString(),
            msgType,
            msgType.starts_with("DISTRIBUTED")
                ? MessagePayload::create<DistributedAgentResponsePayload>(
                    json["pld"]["a"].GetInt(),
                    makePayload(json))
                : makePayload(json));
    };

    for (const auto& replayLogFile : replayLogFiles) {
        std::ifstream ifs{replayLogFile, std::ios::in};
        std::vector<std::string> lines;
        std::string buf;
        std::getline(ifs, buf);
        size_t lineCounter{1};
        while (true) {
            lines.clear();
            while (std::getline(ifs, buf)) {
                lines.push_back(buf);
                ++lineCounter;
            }
            if (lines.empty()) break;
            for (const auto& line : lines) {
                Message::Ptr msg = createMessageFromLogFileEntry(line, lineCounter);
                simulation->queueMessage(msg);
                simulation->time().duration = msg->arrival;
            }
        }
    }

    simulation->simulate();
}

//-------------------------------------------------------------------------

void SimulationManager::publishStartInfo()
{
    if (!online()) return;

    rapidjson::Document json = [this] {
        const auto& representativeSimulation = m_simulations.front();
        const auto msg = Message::create(
            representativeSimulation->time().start,
            0,
            "SIMULATION",
            "*",
            "EVENT_SIMULATION_START",
            MessagePayload::create<StartSimulationPayload>(m_logDir.generic_string()));
        rapidjson::Document json{rapidjson::kObjectType};
        auto& allocator = json.GetAllocator();
        json.AddMember(
            "messages",
            [&] {
                rapidjson::Document messagesJson{rapidjson::kArrayType, &allocator};
                rapidjson::Document msgJson{&allocator};
                msg->jsonSerialize(msgJson);
                messagesJson.PushBack(msgJson, allocator);
                return messagesJson;
            }().Move(),
            allocator);
        return json;
    }();
    rapidjson::Document res;

    net::io_context ctx;
    net::co_spawn(
        ctx, asyncSendOverNetwork(json, m_netInfo.generalMsgEndpoint, res), net::detached);
    ctx.run();
}

//-------------------------------------------------------------------------

void SimulationManager::publishEndInfo()
{
    if (!online()) return;

    rapidjson::Document json = [this] {
        const auto& representativeSimulation = m_simulations.front();
        const auto msg = Message::create(
            representativeSimulation->time().start,
            0,
            "SIMULATION",
            "*",
            "EVENT_SIMULATION_END",
            MessagePayload::create<EmptyPayload>());
        rapidjson::Document json{rapidjson::kObjectType};
        auto& allocator = json.GetAllocator();
        json.AddMember(
            "messages",
            [&] {
                rapidjson::Document messagesJson{rapidjson::kArrayType, &allocator};
                rapidjson::Document msgJson{&allocator};
                msg->jsonSerialize(msgJson);
                messagesJson.PushBack(msgJson, allocator);
                return messagesJson;
            }().Move(),
            allocator);
        return json;
    }();
    rapidjson::Document res;

    net::io_context ctx;
    net::co_spawn(
        ctx, asyncSendOverNetwork(json, m_netInfo.generalMsgEndpoint, res), net::detached);
    ctx.run();
}

//-------------------------------------------------------------------------

void SimulationManager::publishState()
{
    const auto& representativeSimulation = m_simulations.front();

    if (representativeSimulation->currentTimestamp() < m_gracePeriod || !online()) return;
    
    rapidjson::Document stateJson = makeStateJson();
    rapidjson::Document resJson;

    net::io_context ctx;
    net::co_spawn(
        ctx, asyncSendOverNetwork(stateJson, m_netInfo.bookStateEndpoint, resJson), net::detached);
    ctx.run();

    const Timestamp now = representativeSimulation->currentTimestamp();

    for (const auto& response : resJson["responses"].GetArray()) {
        const auto [msg, blockIdx] = decanonize(
            Message::fromJsonResponse(response, now, representativeSimulation->proxy()->name()),
            m_blockInfo.dimension);
        if (!blockIdx) {
            for (const auto& simulation : m_simulations) {
                simulation->queueMessage(msg);
            }
            continue;
        }
        m_simulations.at(*blockIdx)->queueMessage(msg);
    }
}

//-------------------------------------------------------------------------

rapidjson::Document SimulationManager::makeStateJson() const
{
    const auto& representativeSimulation = m_simulations.front();

    const auto bookStatePublishMsg = Message::create(
        representativeSimulation->currentTimestamp(),
        0,
        representativeSimulation->exchange()->name(),
        representativeSimulation->proxy()->name(),
        "MULTIBOOK_STATE_PUBLISH",
        MessagePayload::create<BookStateMessagePayload>(makeCollectiveBookStateJson()));

    rapidjson::Document json;
    auto& allocator = json.GetAllocator();
    bookStatePublishMsg->jsonSerialize(json);
    json["payload"].AddMember(
        "notices",
        [&] {
            rapidjson::Document noticesJson{rapidjson::kArrayType, &allocator};
            std::unordered_map<decltype(Message::type), uint32_t> msgTypeToCount{
                { "RESPONSE_DISTRIBUTED_RESET_AGENT", 0 },
                { "ERROR_RESPONSE_DISTRIBUTED_RESET_AGENT", 0 }
            };
            auto checkGlobalDuplicate = [&](Message::Ptr msg) -> bool {
                const auto payload = std::dynamic_pointer_cast<DistributedAgentResponsePayload>(msg->payload);
                if (payload == nullptr) return false;
                auto relevantPayload = [&] {
                    const auto pld = payload->payload;
                    return std::dynamic_pointer_cast<ResetAgentsResponsePayload>(pld) != nullptr
                        || std::dynamic_pointer_cast<ResetAgentsErrorResponsePayload>(pld) != nullptr;
                };
                if (!relevantPayload()) return true;
                auto it = msgTypeToCount.find(msg->type);
                if (it == msgTypeToCount.end()) return true;
                if (it->second > 0) return false;
                it->second++;
                return true;
            };
            for (const auto& [blockIdx, simulation] : views::enumerate(m_simulations)) {
                for (const auto msg : simulation->proxy()->messages()) {
                    if (!checkGlobalDuplicate(msg)) continue;
                    canonize(msg, blockIdx, m_blockInfo.dimension);
                    rapidjson::Document msgJson{&allocator};
                    msg->jsonSerialize(msgJson);
                    noticesJson.PushBack(msgJson, allocator);
                }
                simulation->proxy()->clearMessages();
            }
            return noticesJson;
        }().Move(),
        allocator);
    
    return json;
}

//-------------------------------------------------------------------------

rapidjson::Document SimulationManager::makeCollectiveBookStateJson() const
{
    auto serialize = [this](rapidjson::Document& json) {
        auto& allocator = json.GetAllocator();
        // Log directory.
        json.AddMember("logDir", rapidjson::Value{m_logDir.c_str(), allocator}, allocator);
        // Books.
        auto serializeBooks = [this](rapidjson::Document& json) {
            json.SetArray();
            auto& allocator = json.GetAllocator();
            for (const auto& [blockIdx, simulation] : views::enumerate(m_simulations)) {
                const auto exchange = simulation->exchange();
                for (const auto book : exchange->books()) {
                    json.PushBack(
                        [&] {
                            rapidjson::Document bookJson{rapidjson::kObjectType, &allocator};
                            const BookId bookIdCanon = blockIdx * m_blockInfo.dimension + book->id();
                            bookJson.AddMember("bookId", rapidjson::Value{bookIdCanon}, allocator);
                            exchange->L3Record().at(book->id()).jsonSerialize(bookJson, "record");
                            rapidjson::Document bidAskJson{&allocator};
                            book->jsonSerialize(bidAskJson);
                            bookJson.AddMember("bid", bidAskJson["bid"], allocator);
                            bookJson.AddMember("ask", bidAskJson["ask"], allocator);
                            return bookJson;
                        }().Move(),
                        allocator);
                }
            }
        };
        json::serializeHelper(json, "books", serializeBooks);
        // Accounts.
        auto serializeAccounts = [this](rapidjson::Document& json) {
            json.SetObject();
            auto& allocator = json.GetAllocator();
            const auto& representativeSimulation = m_simulations.front();
            for (AgentId agentId : views::keys(representativeSimulation->exchange()->accounts())) {
                if (agentId < 0) continue;
                const auto agentIdStr = std::to_string(agentId);
                const char* agentIdCStr = agentIdStr.c_str();
                json.AddMember(
                    rapidjson::Value{agentIdCStr, allocator},
                    rapidjson::Document{rapidjson::kObjectType, &allocator}.Move(),
                    allocator);
                json[agentIdCStr].AddMember("agentId", rapidjson::Value{agentId}, allocator);
                json[agentIdCStr].AddMember(
                    "agentName",
                    agentId < 0
                        ? rapidjson::Value{
                            representativeSimulation->exchange()->accounts().idBimap().right.at(agentId).c_str(), allocator}.Move()
                        : rapidjson::Value{}.SetNull(),
                    allocator);
                json[agentIdCStr].AddMember("holdings", rapidjson::Document{rapidjson::kArrayType, &allocator}, allocator);
                json[agentIdCStr].AddMember("orders", rapidjson::Document{rapidjson::kArrayType, &allocator}, allocator);
                json[agentIdCStr].AddMember("loans", rapidjson::Document{rapidjson::kArrayType, &allocator}, allocator);
                rapidjson::Document feesJson{rapidjson::kObjectType, &allocator};
                for (const auto& [blockIdx, simulation] : views::enumerate(m_simulations)) {
                    const auto exchange = simulation->exchange();
                    const auto books = exchange->books();
                    const auto& account = exchange->accounts().at(agentId);
                    const auto feePolicy = exchange->clearingManager().feePolicy();
                    for (const auto book : books) {
                        const BookId bookIdCanon = blockIdx * m_blockInfo.dimension + book->id();
                        json[agentIdCStr]["orders"].PushBack(
                            rapidjson::Document{rapidjson::kArrayType, &allocator}.Move(), allocator);
                        json[agentIdCStr]["holdings"].PushBack(
                            [&] {
                                rapidjson::Document holdingsJson{&allocator};
                                account.at(book->id()).jsonSerialize(holdingsJson);
                                return holdingsJson;
                            }().Move(),
                            allocator);
                        json[agentIdCStr]["loans"].PushBack(
                            [&] {
                                rapidjson::Document loansObjectJson{rapidjson::kObjectType, &allocator};
                                for (const auto& [id, loan] : account.at(book->id()).m_loans) {
                                    rapidjson::Document loanJson{rapidjson::kObjectType, &allocator};
                                    loanJson.AddMember("id", rapidjson::Value{id}, allocator);
                                    loanJson.AddMember("amount", rapidjson::Value{taosim::util::decimal2double(loan.amount())}, allocator);
                                    loanJson.AddMember("currency", rapidjson::Value{
                                        std::to_underlying(loan.direction() == OrderDirection::BUY ? Currency::QUOTE : Currency::BASE)
                                    }, allocator);
                                    loanJson.AddMember("baseCollateral", rapidjson::Value{taosim::util::decimal2double(loan.collateral().base())}, allocator);
                                    loanJson.AddMember("quoteCollateral", rapidjson::Value{taosim::util::decimal2double(loan.collateral().quote())}, allocator);
                                    const auto idStr = std::to_string(id);
                                    const char* idCStr = idStr.c_str();
                                    loansObjectJson.AddMember(rapidjson::Value{idCStr, allocator}, loanJson, allocator);
                                }
                                return loansObjectJson;
                            }().Move(),
                            allocator);
                        json::serializeHelper(
                            feesJson,
                            std::to_string(bookIdCanon).c_str(),
                            [&](rapidjson::Document& feeJson) {
                                feeJson.SetObject();
                                auto& allocator = feeJson.GetAllocator();
                                feeJson.AddMember(
                                    "volume",
                                    rapidjson::Value{util::decimal2double(
                                        feePolicy->agentVolume(book->id(), agentId))},
                                    allocator);
                                const auto rates = feePolicy->getRates(book->id(), agentId);
                                feeJson.AddMember(
                                    "makerFeeRate",
                                    rapidjson::Value{util::decimal2double(rates.maker)},
                                    allocator);
                                feeJson.AddMember(
                                    "takerFeeRate",
                                    rapidjson::Value{util::decimal2double(rates.taker)},
                                    allocator);
                            });
                    }
                }
                json[agentIdCStr].AddMember("fees", feesJson, allocator);
            }
            for (const auto& [blockIdx, simulation] : views::enumerate(m_simulations)) {
                const auto books = simulation->exchange()->books();
                for (const auto book : books) {
                    const BookId bookIdCanon = blockIdx * m_blockInfo.dimension + book->id();
                    auto serializeSide = [&](OrderDirection side) {
                        const auto& levels =
                            side == OrderDirection::BUY ? book->buyQueue() : book->sellQueue();
                        for (const auto& level : levels) {
                            for (const auto tick : level) {
                                const auto [agentId, clientOrderId] =
                                    books[book->id()]->orderClientContext(tick->id());
                                if (agentId < 0) continue;
                                const auto agentIdStr = std::to_string(agentId);
                                const char* agentIdCStr = agentIdStr.c_str();
                                rapidjson::Document orderJson{&allocator};
                                tick->jsonSerialize(orderJson);
                                json::setOptionalMember(orderJson, "clientOrderId", clientOrderId);
                                json[agentIdCStr]["orders"][bookIdCanon].PushBack(orderJson, allocator);
                            }
                        }
                    };
                    serializeSide(OrderDirection::BUY);
                    serializeSide(OrderDirection::SELL);
                }
            }
        };
        json::serializeHelper(json, "accounts", serializeAccounts);
    };

    rapidjson::Document json{rapidjson::kObjectType};
    serialize(json);
    return json;
}

//-------------------------------------------------------------------------

std::unique_ptr<SimulationManager> SimulationManager::fromConfig(const fs::path& path)
{
    static constexpr auto ctx = std::source_location::current().function_name();

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(path.c_str());
    fmt::println(" - '{}' loaded successfully", path.c_str());
    pugi::xml_node node = doc.child("Simulation");

    auto mngr = std::make_unique<SimulationManager>();

    mngr->m_blockInfo = [&] -> SimulationBlockInfo {
        static constexpr const char* attrName = "blockCount";
        pugi::xml_attribute attr = node.attribute(attrName);
        const auto threadCount = [&] {
            const auto threadCount = attr.as_uint(1);
            if (threadCount > std::thread::hardware_concurrency()) {
                throw std::runtime_error{fmt::format(
                    "{}: requested thread count ({}) exceeds count available ({})",
                    ctx, threadCount, std::thread::hardware_concurrency()
                )};
            }
            return threadCount;
        }();
        const auto booksNode = node.child("Agents").child("MultiBookExchangeAgent").child("Books");
        if (!booksNode) {
            throw std::runtime_error{fmt::format(
                "{}: missing node 'Agents/MultiBookExchangeAgent/Books'",
                ctx
            )};
        }
        return {
            .count = threadCount,
            .dimension = booksNode.attribute("instanceCount").as_uint(1)
        };
    }();
    mngr->m_threadPool = std::make_unique<boost::asio::thread_pool>(mngr->m_blockInfo.count);
    boost::asio::signal_set{mngr->m_io, SIGINT, SIGTERM}.async_wait(
        [&](boost::system::error_code, int) {
            mngr->m_threadPool->stop();
            mngr->m_io.stop();
        });

    mngr->setupLogDir(node);
    mngr->m_simulations =
        views::iota(0u, mngr->m_blockInfo.count)
        | views::transform([&](auto blockIdx) {
            auto simulation = std::make_unique<Simulation>(blockIdx, mngr->m_logDir);
            simulation->configure(node);
            return simulation;
        })
        | ranges::to<std::vector>;

    mngr->m_gracePeriod = node.child("Agents")
        .child("MultiBookExchangeAgent")
        .attribute("gracePeriod")
        .as_ullong();

    mngr->m_netInfo = {
        .host = node.attribute("host").as_string(),
        .port = node.attribute("port").as_string(),
        .bookStateEndpoint = node.attribute("bookStateEndpoint").as_string("/"),
        .generalMsgEndpoint = node.attribute("generalMsgEndpoint").as_string("/"),
        .resolveTimeout = node.attribute("resolveTimeout").as_llong(1),
        .connectTimeout = node.attribute("connectTimeout").as_llong(3),
        .writeTimeout = node.attribute("writeTimeout").as_llong(15),
        .readTimeout = node.attribute("readTimeout").as_llong(60)
    };

    mngr->m_stepSignal.connect([&] {
        for (auto& simulation : mngr->m_simulations) {
            simulation->exchange()->L3Record().clear();
        }
    });

    if (node.attribute("traceTime").as_bool()) {
        mngr->m_stepSignal.connect([&] {
            const auto& representativeSimulation = mngr->m_simulations.front();
            uint64_t total, seconds, hours, minutes, nanos;
            total = representativeSimulation->time().current / 1'000'000'000;
            minutes = total / 60;
            seconds = total % 60;
            hours = minutes / 60;
            minutes = minutes % 60;
            nanos = representativeSimulation->time().current % 1'000'000'000;
            fmt::println("TIME : {:02d}:{:02d}:{:02d}.{:09d}", hours, minutes, seconds, nanos); 
        });
    }

    return mngr;
}

//-------------------------------------------------------------------------

std::unique_ptr<SimulationManager> SimulationManager::fromReplay(
    const fs::path& replayDir, BookId bookId)
{
    static constexpr auto ctx = std::source_location::current().function_name();

    pugi::xml_document doc;
    const fs::path configPath = replayDir / "config.xml";;
    pugi::xml_parse_result result = doc.load_file(configPath.c_str());
    fmt::println(" - '{}' loaded successfully", configPath.c_str());
    pugi::xml_node node = doc.child("Simulation");
    node.attribute("id").set_value(
        fmt::format("{}-replay", replayDir.filename().c_str()).c_str());
    
    static constexpr const char* replayNodeName = "Replay";
    auto replayLogNode = node.child("Agents")
        .child("MultiBookExchangeAgent")
        .child("Logging")
        .child(replayNodeName);
    if (replayLogNode) {
        replayLogNode.parent().remove_child(replayNodeName);
    }

    auto mngr = std::make_unique<SimulationManager>();

    mngr->m_blockInfo = [&] -> SimulationBlockInfo {
        static constexpr const char* attrName = "blockCount";
        pugi::xml_attribute attr = node.attribute(attrName);
        const auto threadCount = [&] {
            const auto threadCount = attr.as_uint(1);
            if (threadCount > std::thread::hardware_concurrency()) {
                throw std::runtime_error{fmt::format(
                    "{}: requested thread count ({}) exceeds count available ({})",
                    ctx, threadCount, std::thread::hardware_concurrency()
                )};
            }
            return threadCount;
        }();
        const auto booksNode = node.child("Agents").child("MultiBookExchangeAgent").child("Books");
        if (!booksNode) {
            throw std::runtime_error{fmt::format(
                "{}: missing node 'Agents/MultiBookExchangeAgent/Books'",
                ctx
            )};
        }
        return {
            .count = threadCount,
            .dimension = booksNode.attribute("instanceCount").as_uint(1)
        };
    }();
    mngr->m_threadPool = std::make_unique<boost::asio::thread_pool>(mngr->m_blockInfo.count);
    boost::asio::signal_set{mngr->m_io, SIGINT, SIGTERM}.async_wait(
        [&](boost::system::error_code, int) {
            mngr->m_threadPool->stop();
            mngr->m_io.stop();
        });

    mngr->setupLogDir(node);
    mngr->m_simulations =
        views::iota(0u, mngr->m_blockInfo.count)
        | views::transform([&](auto blockIdx) {
            auto simulation = std::make_unique<Simulation>(blockIdx, mngr->m_logDir);
            simulation->configure(node);
            simulation->exchange()->replayMode() = true;
            return simulation;
        })
        | ranges::to<std::vector>;

    mngr->m_gracePeriod = node.child("Agents")
        .child("MultiBookExchangeAgent")
        .attribute("gracePeriod")
        .as_ullong();

    if (node.attribute("traceTime").as_bool()) {
        mngr->m_stepSignal.connect([&] {
            const auto& representativeSimulation = mngr->m_simulations.front();
            uint64_t total, seconds, hours, minutes, nanos;
            total = representativeSimulation->time().current / 1'000'000'000;
            minutes = total / 60;
            seconds = total % 60;
            hours = minutes / 60;
            minutes = minutes % 60;
            nanos = representativeSimulation->time().current % 1'000'000'000;
            fmt::println("TIME : {:02d}:{:02d}:{:02d}.{:09d}", hours, minutes, seconds, nanos); 
        });
    }

    return mngr;
}

//-------------------------------------------------------------------------

void SimulationManager::setupLogDir(pugi::xml_node node)
{
    struct ChildAttributeGetter
    {
        std::vector<std::string> searchContext;

        std::string operator()(
            pugi::xml_node node,
            const std::string& searchPath,
            const std::string& attrName,
            std::function<bool(pugi::xml_node)> criterion = [](auto) { return true; })
        {
            const auto [current, rest] = [&searchPath] -> std::pair<std::string, std::string> {
                auto splitPos = searchPath.find_first_of("/");
                return {
                    searchPath.substr(0, splitPos),
                    splitPos != std::string::npos
                        ? searchPath.substr(splitPos + 1, searchPath.size())
                        : ""
                };
            }();
            searchContext.push_back(current);

            if (!rest.empty()) {
                pugi::xml_node child = node.find_child([&current](pugi::xml_node child) {
                    return std::string_view{child.name()} == current;
                });
                if (!child) {
                    const auto searchContextCopy = searchContext;
                    searchContext.clear();
                    throw std::runtime_error(fmt::format(
                        "{}: cannot find node '{}'",
                        std::source_location::current().function_name(),
                        fmt::join(searchContextCopy, "/")));
                }
                return operator()(child, rest, attrName);
            }
    
            pugi::xml_attribute attr;
            for (pugi::xml_node child : node.children()) {
                if (std::string_view{child.name()} == current && criterion(child)) {
                    attr = child.attribute(attrName.c_str());
                    break;
                }
            }
            if (!attr) {
                const auto searchContextCopy = searchContext;
                searchContext.clear();
                throw std::runtime_error(fmt::format(
                    "{}: node '{}' has no attribute '{}'",
                    std::source_location::current().function_name(),
                    fmt::join(searchContextCopy, "/"),
                    attrName));
            }
            searchContext.clear();
            return attr.as_string();
        }
    };

    auto createLogDir = [&] {
        m_logDir = fs::current_path() / "logs" / m_logDir;
        fs::create_directories(m_logDir);
        pugi::xml_document doc;
        doc.append_copy(node);
        doc.save_file((m_logDir / "config.xml").c_str());
    };

    m_logDir = node.attribute("id").as_string();
    if (m_logDir != "{{BG_CONFIG}}") {
        if (m_logDir.empty()) {
            m_logDir = boost::uuids::to_string(boost::uuids::random_generator{}());
        }
        createLogDir();
        return;
    }
    
    pugi::xml_node agentsNode;
    if (agentsNode = node.child("Agents"); !agentsNode) {
        throw std::invalid_argument(fmt::format(
            "{}: missing required child 'Agents'",
            std::source_location::current().function_name()));
    }

    auto getAttr = ChildAttributeGetter{};

    const std::string dt = date::format(
        "%Y%m%d_%H%M",
        date::make_zoned(date::current_zone(), std::chrono::system_clock::now()));
    const std::string duration = node.attribute("duration").as_string();
    const std::string books = std::to_string(m_blockInfo.count * m_blockInfo.dimension);

    const auto balances = [&] -> std::string {
        try {
            return fmt::format(
                "{}_{}",
                getAttr(agentsNode, "MultiBookExchangeAgent/Balances/Base", "total"),
                getAttr(agentsNode, "MultiBookExchangeAgent/Balances/Quote", "total"));
        }
        catch (...) {
            return fmt::format(
                "{}_{}",
                getAttr(agentsNode, "MultiBookExchangeAgent/Balances", "type"),
                getAttr(agentsNode, "MultiBookExchangeAgent/Balances", "wealth"));
        }
    }();

    const std::string priceDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "priceDecimals");
    const std::string volumeDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "volumeDecimals");
    const std::string baseDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "baseDecimals");
    const std::string quoteDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "quoteDecimals");
    const std::string iCount = getAttr(agentsNode, "InitializationAgent", "instanceCount");
    const std::string iPrice = getAttr(agentsNode, "MultiBookExchangeAgent", "initialPrice");
    const std::string fWeight = getAttr(agentsNode, "StylizedTraderAgent", "sigmaF");
    const std::string cWeight = getAttr(agentsNode, "StylizedTraderAgent", "sigmaC");
    const std::string nWeight = getAttr(agentsNode, "StylizedTraderAgent", "sigmaN");
    const std::string tau = getAttr(agentsNode, "StylizedTraderAgent", "tau");
    const std::string sigmaEps = getAttr(agentsNode, "StylizedTraderAgent", "sigmaEps");
    const std::string riskAversion = getAttr(agentsNode, "StylizedTraderAgent", "r_aversion");

    m_logDir = fmt::format(
        "{}-{}-{}-{}-i{}_p{}-f{}_c{}_n{}_t{}_s{}_r{}_d{}_v{}_b{}_q{}",
        dt, duration, books, balances, iCount, iPrice, fWeight, cWeight, nWeight,
        tau, sigmaEps, riskAversion, priceDecimals, volumeDecimals, baseDecimals, quoteDecimals);

    createLogDir();
}

//-------------------------------------------------------------------------

net::awaitable<void> SimulationManager::asyncSendOverNetwork(
    const rapidjson::Value& reqBody, const std::string& endpoint, rapidjson::Document& resJson)
{
    const auto& representativeSimulation = m_simulations.front();

retry:
    auto resolver =
        use_nothrow_awaitable.as_default_on(tcp::resolver{co_await this_coro::executor});
    auto tcp_stream =
        use_nothrow_awaitable.as_default_on(beast::tcp_stream{co_await this_coro::executor});

    int attempts = 0;
    // Resolve.
    auto endpointsVariant = co_await (
        resolver.async_resolve(m_netInfo.host, m_netInfo.port) || timeout(m_netInfo.resolveTimeout));
    while (endpointsVariant.index() == 1) {
        fmt::println("tcp::resolver timed out on {}:{}", m_netInfo.host, m_netInfo.port);
        std::this_thread::sleep_for(10s);
        endpointsVariant = co_await (
            resolver.async_resolve(m_netInfo.host, m_netInfo.port) || timeout(m_netInfo.resolveTimeout));
    }
    auto [e1, endpoints] = std::get<0>(endpointsVariant);
    while (e1) {
        const auto loc = std::source_location::current();
        representativeSimulation->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_netInfo.host, m_netInfo.port, e1.what());
        attempts++;
        fmt::println("Unable to resolve connection to validator at {}:{}{} - Retrying (Attempt {})", m_netInfo.host, m_netInfo.port, endpoint, attempts);
        std::this_thread::sleep_for(10s);
        endpointsVariant = co_await (resolver.async_resolve(m_netInfo.host, m_netInfo.port) || timeout(m_netInfo.resolveTimeout));
        auto [e11, endpoints1] = std::get<0>(endpointsVariant);
        e1 = e11;
        endpoints = endpoints1;
    }

    // Connect.
    attempts = 0;
    auto connectVariant = co_await (tcp_stream.async_connect(endpoints) || timeout(m_netInfo.connectTimeout));
    while (connectVariant.index() == 1) {
        fmt::println("tcp_stream::async_connect timed out on {}:{}", m_netInfo.host, m_netInfo.port);
        std::this_thread::sleep_for(10s);
        connectVariant = co_await (tcp_stream.async_connect(endpoints) || timeout(m_netInfo.connectTimeout));
    }
    auto [e2, _2] = std::get<0>(connectVariant);
    while (e2) {
        const auto loc = std::source_location::current();
        representativeSimulation->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_netInfo.host, m_netInfo.port, e2.what());
        attempts++;
        fmt::println("Unable to connect to validator at {}:{}{} - Retrying (Attempt {})", m_netInfo.host, m_netInfo.port, endpoint, attempts);
        std::this_thread::sleep_for(10s);
        connectVariant = co_await (tcp_stream.async_connect(endpoints) || timeout(m_netInfo.connectTimeout));
        auto [e21, _21] = std::get<0>(connectVariant);
        e2 = e21;
        _2 = _21;
    }

    // Create the request.
    const auto req = makeHttpRequest(endpoint, taosim::json::json2str(reqBody));

    // Send the request.
    attempts = 0;
    auto writeVariant = co_await (http::async_write(tcp_stream, req) || timeout(m_netInfo.writeTimeout));
    while (writeVariant.index() == 1) {
        fmt::println("http::async_write timed out on {}:{}", m_netInfo.host, m_netInfo.port);
        std::this_thread::sleep_for(5s);
        writeVariant = co_await (http::async_write(tcp_stream, req) || timeout(m_netInfo.writeTimeout));
    }
    auto [e3, _3] = std::get<0>(writeVariant);
    while (e3) {
        const auto loc = std::source_location::current();
        representativeSimulation->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_netInfo.host, m_netInfo.port, e3.what());
        attempts++;
        fmt::println("Unable to send request to validator at {}:{}{} - Retrying (Attempt {})", m_netInfo.host, m_netInfo.port, endpoint, attempts);
        goto retry;
    }

    // Receive the response.
    attempts = 0;
    beast::flat_buffer buf;
    http::response_parser<http::string_body> parser{http::response<http::string_body>{}};
    parser.eager(true);
    parser.body_limit(std::numeric_limits<size_t>::max());
    auto readVariant = co_await (http::async_read(tcp_stream, buf, parser) || timeout(m_netInfo.readTimeout));
    if (readVariant.index() == 1) {
        fmt::println("http::async_read timed out on {}:{}", m_netInfo.host, m_netInfo.port);
        goto retry;
    }
    auto [e4, _4] = std::get<0>(readVariant);
    while (e4) {
        const auto loc = std::source_location::current();
        representativeSimulation->logDebug("{}#L{}: {}:{}: {}", loc.file_name(), loc.line(), m_netInfo.host, m_netInfo.port, e4.what());
        attempts++;          
        fmt::println("Unable to read response from validator at {}:{}{} : {} - re-sending request.", m_netInfo.host, m_netInfo.port, endpoint, e4.what(), attempts);
        goto retry;
    }

    http::response<http::string_body> res = parser.release();
    resJson.Parse(res.body().c_str());
}

//-------------------------------------------------------------------------

http::request<http::string_body> SimulationManager::makeHttpRequest(
    const std::string& target, const std::string& body)
{
    http::request<http::string_body> req;
    req.method(http::verb::get);
    req.target(target);
    req.version(11);
    req.set(http::field::host, m_netInfo.host);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    return req;
}

//-------------------------------------------------------------------------

}  // namespace taosim::simulation

//-------------------------------------------------------------------------