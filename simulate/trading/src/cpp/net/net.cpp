/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "net.hpp"

//------------------------------------------------------------------------

net::awaitable<void> timeout(std::chrono::steady_clock::duration duration)
{
    net::steady_timer timer{co_await this_coro::executor};
    timer.expires_after(duration);
    co_await timer.async_wait(use_nothrow_awaitable);
}

//------------------------------------------------------------------------
