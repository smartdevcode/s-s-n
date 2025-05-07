/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"

#include <random>

//-------------------------------------------------------------------------

class RNG : public std::mt19937, public CheckpointSerializable
{
public:
    explicit RNG(uint64_t seed = std::mt19937::default_seed) noexcept;

    std::mt19937::result_type operator()();

    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static RNG fromCheckpoint(const rapidjson::Value& json);

private:
    uint32_t m_callCount{};
    uint64_t m_seed;
};

//-------------------------------------------------------------------------
