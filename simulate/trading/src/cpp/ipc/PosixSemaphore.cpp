/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "taosim/ipc/PosixSemaphore.hpp"

#include "taosim/ipc/util.hpp"

#include <fmt/format.h>

#include <source_location>
#include <stdexcept>

//-------------------------------------------------------------------------

namespace taosim::ipc
{

//-------------------------------------------------------------------------

PosixSemaphore::PosixSemaphore(const PosixSemaphoreDesc& desc)
    : m_desc{desc}
{
    m_desc.name = desc.name.starts_with("/") ? desc.name : "/" + desc.name;

    m_sem = sem_open(m_desc.name.c_str(), m_desc.oflag, m_desc.mode, m_desc.value);

    if (m_sem == SEM_FAILED) {
        throw std::runtime_error{fmt::format(
            "{}: Failed to create POSIX semaphore with name '{}': {} ({})",
            std::source_location::current().function_name(),
            m_desc.name, errno, std::strerror(errno)
        )};
    }
}

//-------------------------------------------------------------------------

PosixSemaphore::~PosixSemaphore() noexcept
{
    sem_close(m_sem);
    sem_unlink(m_desc.name.c_str());
}

//-------------------------------------------------------------------------

bool PosixSemaphore::timedWait() const noexcept
{
    const timespec ts = makeTimespec(m_desc.timeout);
    return sem_timedwait(m_sem, &ts) == 0;
}

//-------------------------------------------------------------------------

void PosixSemaphore::flush() const noexcept
{
    int32_t sink;
    while (sem_getvalue(m_sem, &sink) == 0 && sink > 0) {
        sem_trywait(m_sem);
    }
}

//-------------------------------------------------------------------------

}  // namespace taosim::ipc

//-------------------------------------------------------------------------
