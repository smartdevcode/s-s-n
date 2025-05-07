/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "ThreadSafeMessageQueue.hpp"

#include <mutex>

//-------------------------------------------------------------------------

ThreadSafeMessageQueue::ThreadSafeMessageQueue() noexcept
    : m_mtx{std::make_unique<std::shared_mutex>()}
{}

//-------------------------------------------------------------------------

ThreadSafeMessageQueue::ThreadSafeMessageQueue(MessageQueue underlying) noexcept
    : m_underlying{std::move(underlying)},
      m_mtx{std::make_unique<std::shared_mutex>()}
{}

//-------------------------------------------------------------------------

Message::Ptr ThreadSafeMessageQueue::top() const
{
    std::shared_lock lock{*m_mtx};
    return m_underlying.top();
}

//-------------------------------------------------------------------------

bool ThreadSafeMessageQueue::empty() const
{
    std::shared_lock lock{*m_mtx};
    return m_underlying.empty();
}

//-------------------------------------------------------------------------

size_t ThreadSafeMessageQueue::size() const
{
    std::shared_lock lock{*m_mtx};
    return m_underlying.size();
}

//-------------------------------------------------------------------------

void ThreadSafeMessageQueue::push(PrioritizedMessage pmsg)
{
    std::unique_lock lock{*m_mtx};
    m_underlying.m_queue.emplace(pmsg, m_underlying.m_idCounter++);
}

//-------------------------------------------------------------------------

void ThreadSafeMessageQueue::push(Message::Ptr msg)
{
    std::unique_lock lock{*m_mtx};
    m_underlying.m_queue.emplace(PrioritizedMessage{msg}, m_underlying.m_idCounter++);
}

//-------------------------------------------------------------------------

void ThreadSafeMessageQueue::pop()
{
    std::unique_lock lock{*m_mtx};
    m_underlying.pop();
}

//-------------------------------------------------------------------------

void ThreadSafeMessageQueue::clear()
{
    std::unique_lock lock{*m_mtx};
    m_underlying.clear();
}

//-------------------------------------------------------------------------
