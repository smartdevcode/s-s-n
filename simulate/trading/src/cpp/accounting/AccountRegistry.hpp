/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "Account.hpp"
#include "JsonSerializable.hpp"
#include "common.hpp"

#include <boost/bimap.hpp>

//-------------------------------------------------------------------------

namespace taosim::accounting
{

//-------------------------------------------------------------------------

class AccountRegistry : public JsonSerializable, public CheckpointSerializable
{
public:
    using ContainerType = std::map<AgentId, Account>;

    [[nodiscard]] Account& at(const std::variant<AgentId, LocalAgentId>& agentId);
    [[nodiscard]] Account& operator[](const std::variant<AgentId, LocalAgentId>& agentId);

    [[nodiscard]] decltype(auto) begin(this auto&& self) { return self.m_underlying.begin(); }
    [[nodiscard]] decltype(auto) end(this auto&& self) { return self.m_underlying.end(); }
    
    void registerLocal(const LocalAgentId& agentId, std::optional<Account> account = {}) noexcept;
    void registerLocal(
        const LocalAgentId& agentId,
        const std::string& agentType,
        std::optional<Account> account = {}) noexcept;
    AgentId registerRemote(std::optional<Account> holdings = {}) noexcept;
    void registerJson(const rapidjson::Value& json);

    [[nodiscard]] bool contains(const std::variant<AgentId, LocalAgentId>& agentId) const;
    [[nodiscard]] const boost::bimap<LocalAgentId, AgentId>& idBimap() const noexcept;
    [[nodiscard]] const ContainerType& accounts() const noexcept;
    [[nodiscard]] AgentId getAgentId(const std::variant<AgentId, LocalAgentId>& agentId) const;
    [[nodiscard]] std::optional<std::reference_wrapper<const std::string>> getAgentBaseName(
        AgentId agentId) const noexcept;

    [[nodiscard]] auto&& agentTypeAccountTemplates(this auto&& self) noexcept
    {
        return self.m_agentTypeAccountTemplates;
    }

    void setAccountTemplate(std::function<Account()> factory) noexcept;
    void setAccountTemplate(const std::string& agentType, std::function<Account()> factory) noexcept;
    void reset(AgentId agentId);

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

private:
    // Policy: Local agents have ID < 0, remote >= 0.
    AgentId m_localIdCounter{};
    AgentId m_remoteIdCounter{};

    ContainerType m_underlying;
    std::function<Account()> m_accountTemplate;
    std::map<std::string, std::function<Account()>> m_agentTypeAccountTemplates;
    boost::bimap<LocalAgentId, AgentId> m_idBimap;
    std::map<AgentId, std::string> m_agentIdToBaseName;

    friend class Simulation;
};

//-------------------------------------------------------------------------

}  // namespace taosim::accounting

//-------------------------------------------------------------------------