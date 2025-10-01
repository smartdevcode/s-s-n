/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "taosim/ipc/PosixMessageQueue.hpp"

#include <fmt/format.h>

#include <chrono>
#include <source_location>
#include <stdexcept>
#include <thread>
#include <vector>

//-------------------------------------------------------------------------

namespace taosim::ipc
{

//-------------------------------------------------------------------------

PosixMessageQueue::PosixMessageQueue(const PosixMessageQueueDesc& desc)
    : m_desc{desc}
{
    m_desc.name = desc.name.starts_with("/") ? desc.name : "/" + desc.name;

    m_handle = mq_open(
        m_desc.name.c_str(),
        m_desc.oflag,
        m_desc.mode,
        &m_desc.attr);

    if (m_handle == static_cast<mqd_t>(-1)) {
        throw std::runtime_error{fmt::format(
            "{}: Failed to create POSIX mqueue with name '{}': {} ({})",
            std::source_location::current().function_name(),
            m_desc.name, errno, std::strerror(errno)
        )};
    }
}

//-------------------------------------------------------------------------

PosixMessageQueue::~PosixMessageQueue() noexcept
{
    mq_close(m_handle);
    mq_unlink(m_desc.name.c_str());
}

//-------------------------------------------------------------------------

bool PosixMessageQueue::send(std::span<const char> msg, uint32_t priority) noexcept
{
    for (size_t r{}; r < m_desc.retries; ++r) {
        if (mq_send(m_handle, msg.data(), msg.size(), priority) == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds{m_desc.timeout});
    }
    return false;
}

//-------------------------------------------------------------------------

ssize_t PosixMessageQueue::receive(std::span<char> msg, uint32_t* priority) noexcept
{
    for (size_t r{}; r < m_desc.retries; ++r) {
        const auto ret = mq_receive(m_handle, msg.data(), msg.size(), priority);
        if (ret != -1) {
            return ret;
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds{m_desc.timeout});
    }
    return -1;
}

//-------------------------------------------------------------------------

void PosixMessageQueue::flush() noexcept
{
    std::vector<char> sink(m_desc.attr.mq_maxmsg * m_desc.attr.mq_msgsize);
    while (mq_receive(m_handle, sink.data(), sink.size(), nullptr) != -1);
}

//-------------------------------------------------------------------------

}  // namespace taosim::ipc

//-------------------------------------------------------------------------