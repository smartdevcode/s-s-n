/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/book/TickContainer.hpp"

//-------------------------------------------------------------------------

namespace taosim::book
{

//-------------------------------------------------------------------------

class OrderContainer : public std::deque<TickContainer>
{
public:
    [[nodiscard]] taosim::decimal_t volume() const noexcept { return m_volume; }

    void updateVolume(taosim::decimal_t deltaVolume) noexcept { m_volume += deltaVolume; }

private:
    taosim::decimal_t m_volume{};
};

//-------------------------------------------------------------------------

}  // namespace taosim::book

//-------------------------------------------------------------------------
