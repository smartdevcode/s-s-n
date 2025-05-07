/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "common.hpp"

//-------------------------------------------------------------------------

struct SimulationSignals
{
    bs2::signal<void()> start;
    bs2::signal<void()> step;
    bs2::signal<void()> stop;
    bs2::signal<void(Timespan)> time;
    bs2::signal<void(Timespan)> timeAboutToProgress;
    bs2::signal<void()> agentsCreated;
};

//-------------------------------------------------------------------------
