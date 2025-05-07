/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Agent.hpp"
#include "SimulationConfig.hpp"
#include "IConfigurable.hpp"
#include "IMessageable.hpp"
#include "LocalAgentManager.hpp"
#include "Message.hpp"
#include "ThreadSafeMessageQueue.hpp"
#include "ParameterStorage.hpp"
#include "Recoverable.hpp"
#include "SimulationSignals.hpp"
#include "SimulationState.hpp"
#include "common.hpp"

#include <fmt/core.h>

//-------------------------------------------------------------------------

class MultiBookExchangeAgent;

//-------------------------------------------------------------------------

class Simulation : public IMessageable, public IConfigurable
{
public:
    explicit Simulation(ParameterStorage::Ptr parameters);

    Simulation(
        ParameterStorage::Ptr parameters,
        Timestamp startTimestamp,
        Timestamp duration,
        const std::string& directory);

    void dispatchMessage(
        Timestamp occurrence,
        Timestamp delay,
        const std::string& source,
        const std::string& target,
        const std::string& type,
        MessagePayload::Ptr payload = MessagePayload::create<EmptyPayload>()) const;

    template<typename... PrioArgs>
    requires std::constructible_from<PrioritizedMessage, Message::Ptr, PrioArgs...>
    void dispatchMessageWithPriority(
        Timestamp occurrence,
        Timestamp delay,
        const std::string& source,
        const std::string& target,
        const std::string& type,
        MessagePayload::Ptr payload,
        PrioArgs&&... prioArgs) const
    {
        queueMessageWithPriority(
            Message::create(occurrence, occurrence + delay, source, target, type, payload),
            std::forward<PrioArgs>(prioArgs)...);
    }

    void dispatchGenericMessage(
        Timestamp occurrence,
        Timestamp delay,
        const std::string& source,
        const std::string& target,
        const std::string& type,
        std::map<std::string, std::string> payload) const;

    void queueMessage(Message::Ptr msg) const;

    template<typename... Args>
    requires std::constructible_from<PrioritizedMessage, Args...>
    void queueMessageWithPriority(Args&&... args) const
    {
        m_messageQueue.push(PrioritizedMessage(std::forward<Args>(args)...));
    }

    void simulate();

    [[nodiscard]] taosim::accounting::Account& account(const LocalAgentId& id) const noexcept;
    [[nodiscard]] std::span<const std::unique_ptr<Agent>> agents() const noexcept;
    [[nodiscard]] Timestamp currentTimestamp() const noexcept;
    [[nodiscard]] Timestamp duration() const noexcept;
    [[nodiscard]] MultiBookExchangeAgent* exchange() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const fs::path& logDir() const noexcept;
    [[nodiscard]] SimulationSignals& signals() const noexcept;
    [[nodiscard]] std::mt19937& rng() const noexcept;
    [[nodiscard]] ParameterStorage& parameters() const noexcept;
    [[nodiscard]] const taosim::simulation::SimulationConfig& config() const noexcept { return m_config2; }
    [[nodiscard]] const std::unique_ptr<LocalAgentManager>& localAgentManager() const noexcept { return m_localAgentManager; }

    virtual void receiveMessage(Message::Ptr msg) override;
    virtual void configure(const pugi::xml_node& node) override;

    template<typename... Args>
    void logDebug(fmt::format_string<Args...> fmt, Args&&... args) const noexcept
    {
        if (m_debug) {
            fmt::println(fmt, std::forward<Args>(args)...);
        }
    }
    
    void setDebug(bool flag) noexcept { m_debug = flag; }
    [[nodiscard]] bool debug() const noexcept { return m_debug; }

    void saveCheckpoint();

    [[nodiscard]] static std::unique_ptr<Simulation> fromConfig(const fs::path& path);
    [[nodiscard]] static std::unique_ptr<Simulation> fromCheckpoint(const fs::path& path);

private:
    void configureAgents(pugi::xml_node node);
    void configureId(pugi::xml_node node);
    void configureLogging(pugi::xml_node node);
    void deliverMessage(Message::Ptr msg);
    void start();
    void step();
    void stop();
    void updateTime(Timestamp newTime);

    // TODO: Simulation* everywhere should be non-const
    mutable ThreadSafeMessageQueue m_messageQueue;
    SimulationState m_state{SimulationState::INACTIVE};
    struct { Timestamp start, duration, step, current; } m_time;
    mutable SimulationSignals m_signals;
    std::unique_ptr<LocalAgentManager> m_localAgentManager;
    MultiBookExchangeAgent* m_exchange{};
    mutable std::mt19937 m_rng;
    std::string m_id;
    std::string m_config;
    bool m_testMode = false;
    bool m_debug = false;
    fs::path m_logDir;
    ParameterStorage::Ptr m_parameters{};
    taosim::simulation::SimulationConfig m_config2;

    friend class LocalAgentManager;
    friend class MultiBookExchangeAgent;
};

static_assert(taosim::serialization::Recoverable<Simulation>);

//-------------------------------------------------------------------------
