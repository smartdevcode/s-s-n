/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "UpdateCounter.hpp"
#include "common.hpp"

//-------------------------------------------------------------------------

class Process : public CheckpointSerializable
{
public:
    using ValueSignal = UnsyncSignal<void(double)>;

    virtual ~Process() noexcept = default;

    virtual void update(Timestamp timestamp) = 0;
    [[nodiscard]] virtual double value() const = 0;
    [[nodiscard]] virtual uint64_t count() const { return 0; }

    [[nodiscard]] auto&& valueSignal(this auto&& self) noexcept { return self.m_valueSignal; }
    [[nodiscard]] auto&& updatePeriod(this auto&& self) noexcept { return self.m_updatePeriod; }

protected:
    Process() noexcept = default;

    ValueSignal m_valueSignal;
    Timestamp m_updatePeriod{1};
};

//-------------------------------------------------------------------------
