/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "MessageQueue.hpp"

#include <memory>
#include <shared_mutex>

//-------------------------------------------------------------------------

class ThreadSafeMessageQueue
{
public:
    ThreadSafeMessageQueue() noexcept;
    explicit ThreadSafeMessageQueue(MessageQueue underlying) noexcept;

    [[nodiscard]] Message::Ptr top() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t size() const;

    void push(PrioritizedMessage pmsg);
    void push(Message::Ptr msg);
    void pop();
    void clear();

private:
    void push(const MessageQueue::PrioritizedMessageWithId& realMsg)
    {
        std::shared_lock lock{*m_mtx};
        m_underlying.push(realMsg);
    }

    [[nodiscard]] const MessageQueue::PrioritizedMessageWithId& prioTop() const
    {
        return m_underlying.prioTop();
    }

    MessageQueue m_underlying;
    std::unique_ptr<std::shared_mutex> m_mtx;

    friend class Simulation;
    friend class MultiBookExchangeAgent;
};

//-------------------------------------------------------------------------
