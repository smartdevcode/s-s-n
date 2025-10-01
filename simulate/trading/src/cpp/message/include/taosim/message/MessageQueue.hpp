/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/message/Message.hpp"

#include <limits>
#include <queue>

//-------------------------------------------------------------------------

struct PrioritizedMessage
{
    Message::Ptr msg;
    uint64_t marginCallId;

    PrioritizedMessage(
        Message::Ptr msg, uint64_t marginCallId = std::numeric_limits<uint64_t>::max()) noexcept
        : msg{msg}, marginCallId{marginCallId}
    {}
};

struct PrioritizedMessageWithId
{
    PrioritizedMessage pmsg;
    uint64_t id;

    PrioritizedMessageWithId(PrioritizedMessage pmsg, uint64_t id) noexcept
        : pmsg{pmsg}, id{id}
    {}
};

//-------------------------------------------------------------------------

class MessageQueue
{
public:
    MessageQueue() noexcept = default;

    [[nodiscard]] Message::Ptr top() const { return m_queue.top().pmsg.msg; }
    [[nodiscard]] bool empty() const { return m_queue.empty(); }
    [[nodiscard]] size_t size() const { return m_queue.size(); }

    void push(PrioritizedMessage pmsg) { m_queue.emplace(pmsg, m_idCounter++); }
    void pop() { m_queue.pop(); }
    void clear() { m_queue.clear(); }

private:
    struct PrioritizedMessageWithId
    {
        PrioritizedMessage pmsg;
        uint64_t id;

        PrioritizedMessageWithId(PrioritizedMessage pmsg, uint64_t id) noexcept
            : pmsg{pmsg}, id{id}
        {}
    };

    struct CompareQueueMessages
    {
        bool operator()(PrioritizedMessageWithId lhs, PrioritizedMessageWithId rhs);
    };

    using QueueType = std::priority_queue<
        PrioritizedMessageWithId,
        std::vector<PrioritizedMessageWithId>,
        CompareQueueMessages>;

    struct AccessiblePriorityQueue : public QueueType
    {
        AccessiblePriorityQueue() noexcept = default;
        AccessiblePriorityQueue(std::vector<PrioritizedMessageWithId> messages) noexcept
            : QueueType{CompareQueueMessages{}, std::move(messages)}
        {}

        [[nodiscard]] const container_type& underlying() const noexcept { return c; }

        void clear() { c.clear(); }
    };

    MessageQueue(std::vector<PrioritizedMessageWithId> messages) noexcept;

    [[nodiscard]] const PrioritizedMessageWithId& prioTop() const { return m_queue.top(); }

    void push(PrioritizedMessageWithId pmsgWithId) { m_queue.push(pmsgWithId); }

    AccessiblePriorityQueue m_queue;
    uint64_t m_idCounter{};

    friend class Simulation;
    friend class MultiBookExchangeAgent;
};

//-------------------------------------------------------------------------
