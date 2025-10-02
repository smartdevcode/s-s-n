/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

extern "C" {
#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>
}

#include <cstdint>
#include <string>

//-------------------------------------------------------------------------

namespace taosim::ipc
{

//-------------------------------------------------------------------------

struct PosixSemaphoreDesc
{
    std::string name;
    int32_t oflag = O_CREAT;
    mode_t mode = 0666;
    uint32_t value = 0;
    size_t timeout{30'000'000'000};
};

//-------------------------------------------------------------------------

class PosixSemaphore
{
public:
    explicit PosixSemaphore(const PosixSemaphoreDesc& desc);
    ~PosixSemaphore() noexcept;

    [[nodiscard]] bool timedWait() const noexcept;

    void flush() const noexcept;

private:
    PosixSemaphoreDesc m_desc;
    sem_t* m_sem;
};

//-------------------------------------------------------------------------

}  // namespace taosim::ipc

//-------------------------------------------------------------------------