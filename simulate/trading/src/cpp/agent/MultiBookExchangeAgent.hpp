/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "AccountRegistry.hpp"
#include "Agent.hpp"
#include "BalanceLogger.hpp"
#include "Book.hpp"
#include "BookProcessManager.hpp"
#include "CheckpointSerializable.hpp"
#include "EventRecord.hpp"
#include "ExchangeAgentConfig.hpp"
#include "ExchangeAgentMessagePayloads.hpp"
#include "ExchangeSignals.hpp"
#include "JsonSerializable.hpp"
#include "L2Logger.hpp"
#include "L3EventLogger.hpp"
#include "taosim/book/FeeLogger.hpp"
#include "MessageQueue.hpp"
#include "MultiBookMessagePayloads.hpp"
#include "Order.hpp"
#include "ClearingManager.hpp"
#include "SubscriptionRegistry.hpp"
#include "taosim/exchange/ExchangeConfig.hpp"

#include <boost/asio.hpp>

#include <set>
#include <span>
#include <map>
#include <tuple>

//-------------------------------------------------------------------------

class MultiBookExchangeAgent
    : public Agent,
      public CheckpointSerializable,
      public JsonSerializable
{
public:

    MultiBookExchangeAgent(Simulation* simulation) noexcept;

    [[nodiscard]] auto&& accounts(this auto&& self) noexcept { return self.m_accounts; }

    [[nodiscard]] std::span<Book::Ptr> books() noexcept;
    [[nodiscard]] taosim::accounting::Account& account(const LocalAgentId& agentId);
    [[nodiscard]] ExchangeSignals* signals(BookId bookId);
    [[nodiscard]] Process* process(const std::string& name, BookId bookId);
    [[nodiscard]] taosim::exchange::ClearingManager& clearingManager() noexcept { return *m_clearingManager; }
    [[nodiscard]] taosim::decimal_t getMaintenanceMargin() const noexcept { return m_config2.maintenanceMargin; }
    [[nodiscard]] taosim::decimal_t getMaxLeverage() const noexcept { return m_config2.maxLeverage; }
    [[nodiscard]] taosim::decimal_t getMaxLoan() const noexcept { return m_config2.maxLoan; }
    [[nodiscard]] const taosim::exchange::ExchangeConfig& config2() const noexcept { return m_config2; }
    [[nodiscard]] auto&& L3Record(this auto&& self) noexcept { return self.m_L3Record; }

    void retainRecord(bool flag) noexcept;
    void checkMarginCall() noexcept;

    virtual void configure(const pugi::xml_node& node) override;
    virtual void receiveMessage(Message::Ptr msg) override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] const taosim::config::ExchangeAgentConfig& config() const noexcept { return m_config; }

private:
    void handleException();

    void handleDistributedMessage(Message::Ptr msg);
    void handleDistributedAgentReset(Message::Ptr msg);
    void handleDistributedPlaceMarketOrder(Message::Ptr msg);
    void handleDistributedPlaceLimitOrder(Message::Ptr msg);
    void handleDistributedRetrieveOrders(Message::Ptr msg);
    void handleDistributedCancelOrders(Message::Ptr msg);
    void handleDistributedClosePositions(Message::Ptr msg);
    void handleDistributedUnknownMessage(Message::Ptr msg);

    void handleLocalMessage(Message::Ptr msg);
    void handleLocalPlaceMarketOrder(Message::Ptr msg);
    void handleLocalPlaceLimitOrder(Message::Ptr msg);
    void handleLocalRetrieveOrders(Message::Ptr msg);
    void handleLocalCancelOrders(Message::Ptr msg);
    void handleLocalClosePositions(Message::Ptr msg);
    void handleLocalRetrieveL1(Message::Ptr msg);
    void handleLocalRetrieveBookAsk(Message::Ptr msg);
    void handleLocalRetrieveBookBid(Message::Ptr msg);
    void handleLocalMarketOrderSubscription(Message::Ptr msg);
    void handleLocalLimitOrderSubscription(Message::Ptr msg);
    void handleLocalTradeSubscription(Message::Ptr msg);
    void handleLocalTradeByOrderSubscription(Message::Ptr msg);
    void handleLocalUnknownMessage(Message::Ptr msg);

    void notifyMarketOrderSubscribers(MarketOrder::Ptr marketOrder);
    void notifyLimitOrderSubscribers(LimitOrder::Ptr limitOrder);
    void notifyTradeSubscribers(TradeWithLogContext::Ptr tradeWithCtx);
    void notifyTradeSubscribersByOrderID(TradeWithLogContext::Ptr tradeWithCtx, OrderID orderId);

    void orderCallback(Order::Ptr order, OrderContext ctx);
    void orderLogCallback(Order::Ptr order, OrderContext ctx);
    void tradeCallback(Trade::Ptr trade, BookId bookId);
    void unregisterLimitOrderCallback(LimitOrder::Ptr limitOrder, BookId bookId);
    void marketOrderProcessedCallback(MarketOrder::Ptr marketOrder, OrderContext ctx);
    
    taosim::decimal_t m_eps;
    taosim::config::ExchangeAgentConfig m_config;
    std::vector<Book::Ptr> m_books;
    std::map<BookId, std::unique_ptr<ExchangeSignals>> m_signals;
    L3RecordContainer m_L3Record;
    bool m_retainRecord = false;
    std::map<BookId, std::unique_ptr<L2Logger>> m_L2Loggers;
    std::map<BookId, std::unique_ptr<L3EventLogger>> m_L3EventLoggers;
    std::map<BookId, std::unique_ptr<FeeLogger>> m_feeLoggers;
    std::unique_ptr<BookProcessManager> m_bookProcessManager;
    std::vector<std::unique_ptr<taosim::accounting::BalanceLogger>> m_balanceLoggers;
    std::unique_ptr<taosim::exchange::ClearingManager> m_clearingManager;
    uint64_t m_marginCallCounter{};
    taosim::exchange::ExchangeConfig m_config2;
    taosim::accounting::AccountRegistry m_accounts;

    SubscriptionRegistry<LocalAgentId> m_localMarketOrderSubscribers;
    SubscriptionRegistry<LocalAgentId> m_localLimitOrderSubscribers;
    SubscriptionRegistry<LocalAgentId> m_localTradeSubscribers;
    std::map<OrderID, SubscriptionRegistry<LocalAgentId>> m_localTradeByOrderSubscribers;

    friend class Simulation;
};

//-------------------------------------------------------------------------
