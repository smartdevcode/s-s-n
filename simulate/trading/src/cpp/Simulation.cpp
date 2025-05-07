/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "Simulation.hpp"

#include "SimulationException.hpp"
#include "util.hpp"

#include <boost/algorithm/string/replace.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <date/date.h>
#include <date/tz.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <source_location>


#include <stdexcept>

//-------------------------------------------------------------------------

Simulation::Simulation(ParameterStorage::Ptr parameters) : Simulation(parameters, 0, 0, ".")
{}

//-------------------------------------------------------------------------

Simulation::Simulation(
    ParameterStorage::Ptr parameters,
    Timestamp startTimestamp,
    Timestamp duration,
    const std::string& directory)
    : IMessageable{this, "SIMULATION"},
      m_parameters{parameters},
      m_time{.start = startTimestamp, .duration = duration, .step = 1, .current = startTimestamp},
      m_localAgentManager{std::make_unique<LocalAgentManager>(this)}
{}

//-------------------------------------------------------------------------

void Simulation::dispatchMessage(
    Timestamp occurrence,
    Timestamp delay,
    const std::string& source,
    const std::string& target,
    const std::string& type,
    MessagePayload::Ptr payload) const
{
    queueMessage(
        Message::create(occurrence, occurrence + delay, source, target, type, payload));
}

//-------------------------------------------------------------------------

void Simulation::dispatchGenericMessage(
    Timestamp occurrence,
    Timestamp delay,
    const std::string& source,
    const std::string& target,
    const std::string& type,
    std::map<std::string, std::string> payload) const
{
    queueMessage(Message::create(
        occurrence,
        occurrence + delay,
        source,
        target,
        type,
        MessagePayload::create<GenericPayload>(std::move(payload))));
}

//-------------------------------------------------------------------------

void Simulation::queueMessage(Message::Ptr msg) const
{
    m_messageQueue.push(msg);
}

//-------------------------------------------------------------------------

void Simulation::simulate()
{
    if (m_state == SimulationState::STOPPED) return;
    else if (m_state == SimulationState::INACTIVE) start();

    while (m_time.current < m_time.start + m_time.duration) {
        step();
    }

    stop();
}

//-------------------------------------------------------------------------

taosim::accounting::Account& Simulation::account(const LocalAgentId& id) const noexcept
{
    return m_exchange->account(id);
}

//-------------------------------------------------------------------------

std::span<const std::unique_ptr<Agent>> Simulation::agents() const noexcept
{
    return m_localAgentManager->agents(); 
}

//-------------------------------------------------------------------------

Timestamp Simulation::currentTimestamp() const noexcept
{
    return m_time.current;
}

//-------------------------------------------------------------------------

Timestamp Simulation::duration() const noexcept
{
    return m_time.duration;
}

//-------------------------------------------------------------------------

MultiBookExchangeAgent* Simulation::exchange() const noexcept
{
    return m_exchange;
}

//-------------------------------------------------------------------------

const std::string& Simulation::id() const noexcept
{
    return m_id;
}

//-------------------------------------------------------------------------

const fs::path& Simulation::logDir() const noexcept
{
    return m_logDir;
}

//-------------------------------------------------------------------------

SimulationSignals& Simulation::signals() const noexcept
{
    return m_signals;
}

//-------------------------------------------------------------------------

std::mt19937& Simulation::rng() const noexcept
{
    return m_rng;
};

//-------------------------------------------------------------------------

ParameterStorage& Simulation::parameters() const noexcept
{
    return *m_parameters;
}

//-------------------------------------------------------------------------

void Simulation::receiveMessage(Message::Ptr msg)
{
    // TODO: Do something?
}

//-------------------------------------------------------------------------

void Simulation::configure(const pugi::xml_node& node)
{
    m_config2 = taosim::simulation::SimulationConfig::fromXML(node);

    pugi::xml_attribute attr;
    
    if (attr = node.attribute("start"); attr.empty()) {
        throw std::invalid_argument(fmt::format(
            "{}: missing required attribute 'start'",
            std::source_location::current().function_name()));
    }
    m_time.start = attr.as_ullong();

    if (attr = node.attribute("duration"); attr.empty()) {
        throw std::invalid_argument(fmt::format(
            "{}: missing required attribute 'duration'",
            std::source_location::current().function_name()));
    }
    m_time.duration = attr.as_ullong();

    m_time.step = node.attribute("step").as_ullong(1);
    m_time.current = node.attribute("current").as_ullong(m_time.start);

    if (attr = node.attribute("seed"); !attr.empty()) {
        m_rng = std::mt19937{attr.as_ullong()};
    } else {
        m_rng = std::mt19937{std::random_device{}()};
    }

    m_testMode = node.attribute("test").as_bool();

    m_config = [node] -> std::string {
        std::ostringstream oss;
        node.print(oss);
        return oss.str();
    }();

    if (node.attribute("traceTime").as_bool()) {
        m_signals.step.connect([this] { fmt::println("TIME : {}", m_time.current); });
    }

    if (node.attribute("enableCheckpointing").as_bool()) {
        m_signals.step.connect([this] { saveCheckpoint(); });
    }

    if (node.attribute("debug").as_bool()) {
        m_debug = true;
    }

    // NOTE: Ordering important!
    configureId(node);
    configureLogging(node);
    configureAgents(node);
}

//-------------------------------------------------------------------------

void Simulation::saveCheckpoint()
{
    fmt::println("Saving checkpoint...");
    rapidjson::Document json{rapidjson::kObjectType};
    auto& allocator = json.GetAllocator();

    // Config file contents, with current id and time added.
    logDebug("Serializing config...");
    taosim::json::serializeHelper(
        json,
        "config",
        [this](rapidjson::Document& json) {
            const auto config = [this] -> std::string {
                pugi::xml_document doc;
                pugi::xml_parse_result result = doc.load_string(m_config.c_str());
                pugi::xml_node node = doc.child("Simulation");
                if (pugi::xml_attribute attr = node.attribute("id")) {
                    attr.set_value(m_id.c_str());
                } else {
                    node.append_attribute("id") = m_id.c_str();
                }
                if (pugi::xml_attribute attr = node.attribute("current")) {
                    attr.set_value(m_time.current);
                } else {
                    node.append_attribute("current") = m_time.current;
                }
                std::ostringstream oss;
                doc.print(oss, "");
                return oss.str();
            }();
            json.SetString(config.c_str(), config.size(), json.GetAllocator());
        });

    // Accounts, including standing orders.
    logDebug("Serializing accounts...");
    m_exchange->checkpointSerialize(json, "accounts");

    // Active OrderContexts.
    logDebug("Serializing OrderContexts...");
    taosim::json::serializeHelper(
        json,
        "order2clientCtx",
        [this](rapidjson::Document& json) {
            json.SetArray();
            auto& allocator = json.GetAllocator();
            for (const auto& book : m_exchange->books()) {
                rapidjson::Document subJson{rapidjson::kObjectType, &allocator};
                for (const auto& [orderId, clientCtx] : book->m_order2clientCtx) {
                    clientCtx.checkpointSerialize(subJson, std::to_string(orderId));
                }
                json.PushBack(subJson, allocator);
            }
        }
    );

    // ID counters of order & trade factories.
    logDebug("Serializing Order & Trade factories...");
    taosim::json::serializeHelper(
        json,
        "orderIdCounters",
        [this](rapidjson::Document& json) {
            json.SetArray();
            auto& allocator = json.GetAllocator();
            for (const auto& book : m_exchange->books()) {
                rapidjson::Document subJson{&allocator};
                book->orderFactory().checkpointSerialize(subJson);
                json.PushBack(subJson, allocator);
            }
        });
    taosim::json::serializeHelper(
        json,
        "tradeIdCounters",
        [this](rapidjson::Document& json) {
            json.SetArray();
            auto& allocator = json.GetAllocator();
            for (const auto& book : m_exchange->books()) {
                rapidjson::Document subJson{&allocator};
                book->tradeFactory().checkpointSerialize(subJson);
                json.PushBack(subJson, allocator);
            }
        });

    // L3 record.
    logDebug("Serializing L3Record...");
    m_exchange->m_L3Record.checkpointSerialize(json, "L3Record");

    // Subscriptions.
    logDebug("Serializing Subscriptions...");
    taosim::json::serializeHelper(
        json,
        "subscriptions",
        [this](rapidjson::Document& json) {
            json.SetObject();
            m_exchange->m_localMarketOrderSubscribers.checkpointSerialize(json, "market");
            logDebug("Serialized localMarketOrderSubscribers");
            m_exchange->m_localLimitOrderSubscribers.checkpointSerialize(json, "limit");
            logDebug("Serialized localLimitOrderSubscribers");
            m_exchange->m_localTradeSubscribers.checkpointSerialize(json, "trade");
            logDebug("Serialized localTradeSubscribers");
            taosim::json::serializeHelper(
                json,
                "tradeByOrder",
                [this](rapidjson::Document& json) {
                    json.SetObject();
                    for (const auto& [orderId, subs] : m_exchange->m_localTradeByOrderSubscribers) {
                        subs.checkpointSerialize(json, std::to_string(orderId));
                    }
                });
        });

    // Misc.
    logDebug("Serializing Misc...");
    json.AddMember("gracePeriod", rapidjson::Value{m_exchange->m_gracePeriod}, allocator);
    json.AddMember("retainRecord", rapidjson::Value{m_exchange->m_retainRecord}, allocator);
    json.AddMember(
        "checkpointWriteTime",
        rapidjson::Value{
            date::format(
                "%Y%m%d_%H%M%S",
                date::make_zoned(
                    date::current_zone(),
                    std::chrono::system_clock::now())).c_str(), allocator},
        allocator);
    json.AddMember("state", rapidjson::Value{std::to_underlying(m_state)}, allocator);
    m_exchange->m_bookProcessManager->checkpointSerialize(json, "processManager");    

    // Save to disk...
    const fs::path ckptDir = m_logDir / "ckpt";
    logDebug("Replacing checkpoint data at {}...", ckptDir.string());
    fs::create_directories(ckptDir);

    // ...and the log files up to this point.
    logDebug("Storing log file sizes...");
    taosim::json::serializeHelper(
        json,
        "logs",
        [this](rapidjson::Document& json) -> void {
            json.SetObject();
            auto& allocator = json.GetAllocator();
            for ([[maybe_unused]] const auto& [_, logger] : m_exchange->m_L2Loggers) {
                // std::string lastLine = util::getLastLines(logger->filepath().string(), 1)[0];
                std::ifstream source{logger->filepath().generic_string(), std::ios_base::binary};
                source.seekg(0, std::ios_base::end);
                const size_t size = static_cast<size_t>(source.tellg());
                logDebug("Storing size {} for file {}...", size, logger->filepath().c_str());
                json.AddMember(
                    rapidjson::Value{logger->filepath().filename().c_str(), allocator},
                    rapidjson::Value{size},
                    allocator);
            }
            for ([[maybe_unused]] const auto& [_, logger] : m_exchange->m_L3EventLoggers) {
                // std::string lastLine = util::getLastLines(logger->filepath().string(), 1)[0];
                std::ifstream source{logger->filepath().generic_string(), std::ios_base::binary};
                source.seekg(0, std::ios_base::end);
                const size_t size = static_cast<size_t>(source.tellg());
                logDebug("Storing size {} for file {}...", size, logger->filepath().c_str());
                json.AddMember(
                    rapidjson::Value{logger->filepath().filename().c_str(), allocator},
                    rapidjson::Value{size},
                    allocator);
            }
        }
    );

    const fs::path ckptPath = ckptDir / "ckpt.json";
    const fs::path ckptTmpPath = ckptDir / "ckpt.tmp.json";
    std::ofstream ckptTmpFile{ckptTmpPath};
    if (!ckptTmpFile) {
        throw std::runtime_error(fmt::format(
            "{}: Error writing checkpoint to '{}'",
            std::source_location::current().function_name(),
            ckptTmpPath.c_str()));
    }
    logDebug("Writing new checkpoint data...");
    taosim::json::dumpJson(json, ckptTmpFile);
    logDebug("Cleaning up...");
    fs::remove(ckptPath);
    fs::rename(ckptTmpPath, ckptPath);

    fmt::println("Checkpoint saved!");
}

//-------------------------------------------------------------------------

std::unique_ptr<Simulation> Simulation::fromConfig(const fs::path& path)
{
    pugi::xml_document doc;

    pugi::xml_parse_result result = doc.load_file(path.c_str());
    fmt::println(" - '{}' loaded successfully", path.c_str());

    pugi::xml_node node = doc.child("Simulation");
    auto simulation = std::make_unique<Simulation>(std::make_shared<ParameterStorage>());
    simulation->configure(node);

    return simulation;
}

//-------------------------------------------------------------------------

std::unique_ptr<Simulation> Simulation::fromCheckpoint(const fs::path& path)
{
    fmt::println("Resuming simulation from checkpoint at {}", path.string());
    rapidjson::Document json = taosim::json::loadJson(path);

    auto simulation = std::make_unique<Simulation>(std::make_shared<ParameterStorage>());

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(json["config"].GetString());
    pugi::xml_node node = doc.child("Simulation");

    // Config.
    fmt::println("Configuring simulation...");
    simulation->configure(node);
    
    fmt::println("\nRestoring accounts...");
    simulation->exchange()->accounts().registerJson(json["accounts"]);

    // Books & Accounts.
    fmt::println("Restoring books...");
    int cnt = 0;
    const BookId bookCount = simulation->exchange()->books().size();
    for (const auto& member : json["accounts"].GetObject()) {
        const rapidjson::Value& accountJson = member.value;
        const AgentId agentId = accountJson["agentId"].GetInt();
        for (BookId bookId = 0; bookId < bookCount; ++bookId) {
            Book::Ptr book = simulation->exchange()->books()[bookId];
            book->m_orderFactory.m_idCounter =
                json["orderIdCounters"][bookId]["idCounter"].GetUint64();
            book->m_tradeFactory.m_idCounter =
                json["tradeIdCounters"][bookId]["idCounter"].GetUint64();
            book->m_initMode = true;
            const rapidjson::Value& bookOrdersJson = accountJson["orders"][bookId];
            for (const rapidjson::Value& orderJson : bookOrdersJson.GetArray()) {
                LimitOrder::Ptr order = LimitOrder::fromJson(
                    orderJson, 
                    simulation->exchange()->m_config.parameters().priceIncrementDecimals, 
                    simulation->exchange()->m_config.parameters().volumeIncrementDecimals);
                book->m_order2clientCtx[order->id()] =
                    OrderClientContext::fromJson(
                        json["order2clientCtx"][bookId][std::to_string(order->id()).c_str()]);
                book->placeOrder(order);
                simulation->exchange()->accounts()[agentId].activeOrders()[bookId].insert(order);
                fmt::print("Restored Agent {} Book {} Order {}         \r", agentId, bookId, order->id());
                cnt++;
            }
            book->m_initMode = false;
        }
    }

    // L3 record.
    fmt::println("\nRestoring L3Record...");
    simulation->exchange()->m_L3Record = L3RecordContainer::fromJson(json["L3Record"]);

    // Subscriptions.
    fmt::println("Restoring Subscriptions: localMarketOrderSubscribers");
    simulation->exchange()->m_localMarketOrderSubscribers =
        SubscriptionRegistry<LocalAgentId>::fromJson(json["subscriptions"]["market"]);
    fmt::println("Restoring Subscriptions: localLimitOrderSubscribers");
    simulation->exchange()->m_localLimitOrderSubscribers =
        SubscriptionRegistry<LocalAgentId>::fromJson(json["subscriptions"]["limit"]);
    fmt::println("Restoring Subscriptions: localTradeSubscribers");
    simulation->exchange()->m_localTradeSubscribers = 
        SubscriptionRegistry<LocalAgentId>::fromJson(json["subscriptions"]["trade"]);
    fmt::println("Restoring Subscriptions: localTradeByOrderSubscribers");
    for (const auto& member : json["subscriptions"]["tradeByOrder"].GetObject()) {
        const char* name = member.name.GetString();
        const BookId bookId = std::stoi(name);
        simulation->exchange()->m_localTradeByOrderSubscribers[bookId] =
            SubscriptionRegistry<LocalAgentId>::fromJson(
                json["subscriptions"]["tradeByOrder"][name]);
    }

    // Misc.
    fmt::println("Restoring Misc..");
    simulation->exchange()->m_gracePeriod = json["gracePeriod"].GetUint64();
    simulation->exchange()->retainRecord(json["retainRecord"].GetBool());
    simulation->m_state = SimulationState{json["state"].GetUint()};
    simulation->exchange()->m_bookProcessManager = BookProcessManager::fromCheckpoint(
        json["processManager"], simulation.get());

    // Replace log files with those from the checkpoint.
    fmt::println("Aligning Logs with Checkpoint..");
    std::regex pattern{".*L[23].*\\.log$"};
    for (const fs::directory_entry& entry : fs::directory_iterator(simulation->m_logDir)) {
        const std::string filename = entry.path().filename().string();
        if (fs::is_regular_file(entry) && std::regex_match(filename, pattern)) {
            off_t size = json["logs"][filename.c_str()].GetInt();
            auto logFile = (simulation->m_logDir / filename);
            fs::resize_file(logFile, size);
            fmt::println("Truncated file {} to size {}", logFile.string(), size);
        }
    }
    fmt::println("Resumed from checkpoint!");
    return simulation;
}

//-------------------------------------------------------------------------

void Simulation::configureAgents(pugi::xml_node node)
{
    static constexpr auto ctx = std::source_location::current().function_name();

    static const std::set<std::string> specialAgents{
        "DISTRIBUTED_PROXY_AGENT",
        "EXCHANGE",
        "LOGGER_TRADES"
    };

    pugi::xml_node agentsNode;

    if (agentsNode = node.child("Agents"); !agentsNode) {
        throw std::invalid_argument{fmt::format(
            "{}: missing required child 'Agents'", ctx)};
    }

    m_localAgentManager->createAgentsInstanced(
        agentsNode,
        [&](pugi::xml_node agentNode) {
            const std::string agentType = agentNode.name();
            if (specialAgents.contains(agentNode.attribute("name").as_string())) return;
            if (m_exchange == nullptr) {
                throw std::runtime_error{fmt::format("{}: m_exchange == nullptr!", ctx)};
            }
            if (m_exchange->accounts().agentTypeAccountTemplates().contains(agentType)) return;
            if (pugi::xml_node balancesNode = agentNode.child("Balances")) {
                m_exchange->accounts().setAccountTemplate(
                    agentType,
                    taosim::accounting::Account{
                        static_cast<uint32_t>(m_exchange->books().size()),
                        taosim::accounting::Balances{
                            taosim::accounting::Balance::fromXML(
                                balancesNode.child("Base"),
                                m_exchange->config().parameters().baseIncrementDecimals),
                            taosim::accounting::Balance::fromXML(
                                balancesNode.child("Quote"),
                                m_exchange->config().parameters().quoteIncrementDecimals),
                            m_exchange->config().parameters().baseIncrementDecimals,
                            m_exchange->config().parameters().quoteIncrementDecimals
                        }});
            }
        });

    auto it = std::find_if(
        m_localAgentManager->begin(),
        m_localAgentManager->end(),
        [](const auto& agent) { return agent->name() == "EXCHANGE"; });
    if (it == m_localAgentManager->end()) {
        throw std::invalid_argument{fmt::format(
            "{}: missing required agent named 'EXCHANGE'", ctx)};
    }

    for (const auto& agent : m_localAgentManager->agents()) {
        if (specialAgents.contains(agent->name())) continue;
        m_exchange->accounts().registerLocal(agent->name(), agent->type());
    }

    m_signals.agentsCreated();
}

//-------------------------------------------------------------------------

void Simulation::configureId(pugi::xml_node node)
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

    std::string id{node.attribute("id").as_string()};
    if (id != "{{BG_CONFIG}}") {
        m_id = !id.empty()
            ? std::move(id) 
            : boost::uuids::to_string(boost::uuids::random_generator{}());
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
    const std::string books = getAttr(agentsNode, "MultiBookExchangeAgent/Books", "instanceCount");
    const std::string baseBalance = getAttr(agentsNode, "MultiBookExchangeAgent/Balances/Base", "total");
    const std::string quoteBalance = getAttr(agentsNode, "MultiBookExchangeAgent/Balances/Quote", "total");
    const std::string priceDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "priceDecimals");
    const std::string volumeDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "volumeDecimals");
    const std::string baseDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "baseDecimals");
    const std::string quoteDecimals = getAttr(agentsNode, "MultiBookExchangeAgent", "quoteDecimals");
    const std::string iCount = getAttr(agentsNode, "InitializationAgent", "instanceCount");
    const std::string iPrice = getAttr(agentsNode, "InitializationAgent", "price");
    const std::string fWeight = getAttr(agentsNode, "StylizedTraderAgent", "sigmaF");
    const std::string cWeight = getAttr(agentsNode, "StylizedTraderAgent", "sigmaC");
    const std::string nWeight = getAttr(agentsNode, "StylizedTraderAgent", "sigmaN");
    const std::string priceF0 = getAttr(agentsNode, "StylizedTraderAgent", "priceF0");
    const std::string tau = getAttr(agentsNode, "StylizedTraderAgent", "tau");
    const std::string sigmaEps = getAttr(agentsNode, "StylizedTraderAgent", "sigmaEps");
    const std::string riskAversion = getAttr(agentsNode, "StylizedTraderAgent", "r_aversion");
    m_id = fmt::format(
        "{}-{}-{}-{}_{}-i{}_p{}-f{}_c{}_n{}_pf{}_t{}_s{}_r{}_d{}_v{}_b{}_q{}",
        dt, duration, books, baseBalance, quoteBalance, iCount, iPrice, fWeight, cWeight, nWeight,
        priceF0, tau, sigmaEps, riskAversion, priceDecimals, volumeDecimals, baseDecimals, quoteDecimals);
}

//-------------------------------------------------------------------------

void Simulation::configureLogging(pugi::xml_node node)
{
    if (m_id.empty()) configureId(node);
    if (m_testMode) fs::current_path(fs::temp_directory_path());
    m_logDir = fs::current_path() / "logs" / m_id;    
    fs::create_directories(m_logDir);
    pugi::xml_document doc;
    doc.append_copy(node);
    doc.save_file((m_logDir / "config.xml").c_str());
}

//-------------------------------------------------------------------------

void Simulation::deliverMessage(Message::Ptr msg)
{
    for (const auto& target : msg->targets) {
        if (target == "*") {
            receiveMessage(msg);
            for (const auto& agent : m_localAgentManager->agents()) {
                agent->receiveMessage(msg);
            }
        }
        else if (target == "EXCHANGE") {
            m_exchange->receiveMessage(msg);
        }
        else if (target == name()) {
            receiveMessage(msg);
        }
        else if (target.back() == '*') {
            const auto prefix = target.substr(0, target.size() - 1);
            auto lb = std::lower_bound(
                m_localAgentManager->begin(),
                m_localAgentManager->end(),
                prefix,
                [](const auto& agent, const auto& needle) {
                    const auto& haystack = agent->name();
                    return haystack.find(needle);
                });
            auto ub = std::upper_bound(
                m_localAgentManager->begin(),
                m_localAgentManager->end(),
                prefix,
                [](const auto& needle, const auto& agent) {
                    const auto& haystack = agent->name();
                    return haystack.find(needle);
                });
            std::for_each(lb, ub, [msg](const auto& agent) { agent->receiveMessage(msg); });
        }
        else {
            auto it = std::lower_bound(
                m_localAgentManager->begin(),
                m_localAgentManager->end(),
                target,
                [](const auto& agent, const auto& val) { return agent->name() < val; });
            if ((*it)->name() != target) {
                return;
            }
            else if (it == m_localAgentManager->end()) {
                throw SimulationException(fmt::format(
                    "{}: unknown message target '{}'",
                    std::source_location::current().function_name(),
                    target));
            }
            (*it)->receiveMessage(msg);
        }
    }
}

//-------------------------------------------------------------------------

void Simulation::start()
{
    dispatchMessage(
        m_time.start,
        0,
        "SIMULATION",
        "*",
        "EVENT_SIMULATION_START",
        MessagePayload::create<StartSimulationPayload>(logDir().generic_string()));
    dispatchMessage(
        m_time.start,
        m_time.duration - 1,
        "SIMULATION",
        "*",
        "EVENT_SIMULATION_STOP",
        MessagePayload::create<EmptyPayload>());

    m_state = SimulationState::STARTED;
    m_signals.start();
}

//-------------------------------------------------------------------------

void Simulation::step()
{
    const Timestamp cutoff = m_time.current + m_time.step;

    m_exchange->checkMarginCall();

    auto loopCondition = [&] -> bool {
        return !m_messageQueue.empty()
            && m_messageQueue.top()->arrival < cutoff;
    };

    loop:
    while (loopCondition()) {
        Message::Ptr msg = m_messageQueue.top();
        m_messageQueue.pop();
        try {
            if (msg->arrival > m_time.current) {
                m_signals.timeAboutToProgress({.begin = m_time.current, .end = msg->arrival});
            }
            if (!m_messageQueue.empty() && msg->arrival > m_messageQueue.top()->arrival) {
                m_messageQueue.push(msg);
                continue;
            }
            updateTime(msg->arrival);
            deliverMessage(msg);
        }
        catch (const std::exception &exc) {
            fmt::println(
                "step: Error processing {} Message From {} at {} to {} at {} : {}\n{}",
                msg->type,
                msg->source,
                msg->occurrence,
                fmt::join(msg->targets, ","),
                msg->arrival,
                exc.what(),
                taosim::json::jsonSerializable2str(msg));
            std::exit(1);
        }
    }

    if (m_time.current < cutoff) {
        m_signals.timeAboutToProgress({.begin = m_time.current, .end = cutoff});
        if (!m_messageQueue.empty() && m_messageQueue.top()->arrival < cutoff) {
            goto loop;
        }
    }
    updateTime(std::max(m_time.current, cutoff));
    m_signals.step();
}

//-------------------------------------------------------------------------

void Simulation::stop()
{
    m_state = SimulationState::STOPPED;
    m_signals.stop();
}

//-------------------------------------------------------------------------

void Simulation::updateTime(Timestamp newTime)
{
    if (newTime == m_time.current) return;
    if (newTime < m_time.current) {
        throw SimulationException(fmt::format(
            "{}: Attempt to update to earlier time : Current {} | New {}",
            std::source_location::current().function_name(),
            m_time.current, newTime));
    }
    Timestamp oldTime = std::exchange(m_time.current, newTime);
    m_signals.time({.begin = oldTime + 1, .end = newTime});
}

//-------------------------------------------------------------------------
