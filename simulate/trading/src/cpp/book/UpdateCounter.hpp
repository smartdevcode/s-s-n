/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Timestamp.hpp>

#include <pugixml.hpp>

#include <algorithm>

//-------------------------------------------------------------------------

class UpdateCounter
{
public:
    UpdateCounter() noexcept = default;
    explicit UpdateCounter(Timestamp period) noexcept;

    [[nodiscard]] Timestamp state() const noexcept { return m_counter; }
    [[nodiscard]] Timestamp period() const noexcept { return m_internalPeriod + 1; }
    [[nodiscard]] Timestamp stepsUntilUpdate() const noexcept { return m_internalPeriod - m_counter; }
    [[nodiscard]] bool check() const noexcept { return m_counter == m_internalPeriod; }

    void setState(Timestamp value) noexcept { m_counter = value; }

    [[nodiscard]] static UpdateCounter fromXML(pugi::xml_node node);

private:
    Timestamp m_internalPeriod{};
    Timestamp m_counter{};
};

//-------------------------------------------------------------------------
