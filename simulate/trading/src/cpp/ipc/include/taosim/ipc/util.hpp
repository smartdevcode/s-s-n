/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <ctime>

//-------------------------------------------------------------------------

namespace taosim::ipc
{

//-------------------------------------------------------------------------

[[nodiscard]] inline timespec makeTimespec(size_t ns)
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    auto total = ts.tv_nsec + ns;
    static constexpr size_t nsInSecs = 1'000'000'000;
    ts.tv_sec += ns / nsInSecs;
    ts.tv_nsec += ns % nsInSecs;
    if (ts.tv_nsec >= nsInSecs) {
        ++ts.tv_sec;
        ts.tv_nsec -= nsInSecs;
    }
    return ts;
}

//-------------------------------------------------------------------------

}  // namespace taosim::ipc

//-------------------------------------------------------------------------
