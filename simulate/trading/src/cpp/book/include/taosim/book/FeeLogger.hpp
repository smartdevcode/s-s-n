/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "JsonSerializable.hpp"
#include "TimeConfig.hpp"
#include "ExchangeSignals.hpp"
#include "FeeLogEvent.hpp"
#include "taosim/exchange/FeePolicyWrapper.hpp"

#include "util.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>

// using namespace taosim::exchange;

//-------------------------------------------------------------------------

class Simulation;

//-------------------------------------------------------------------------

class FeeLogger
{
public:
    FeeLogger(
        const fs::path& filepath,
        std::chrono::system_clock::time_point startTimePoint,
        decltype(ExchangeSignals::feeLog)& signal,
        const Simulation* simulation) noexcept;

    [[nodiscard]] const fs::path& filepath() const noexcept { return m_filepath; }

private:
    void log(const FeePolicyWrapper* feePolicyWrapper, taosim::FeeLogEvent event);

    std::unique_ptr<spdlog::logger> m_logger;
    fs::path m_filepath;
    std::chrono::system_clock::time_point m_startTimePoint;
    bs2::scoped_connection m_feed;
    const Simulation* m_simulation;
    taosim::simulation::TimestampConversionFn m_timeConverter{};
};

//-------------------------------------------------------------------------
