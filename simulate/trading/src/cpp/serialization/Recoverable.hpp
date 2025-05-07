/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "mp.hpp"

#include <filesystem>

//-------------------------------------------------------------------------

namespace taosim::serialization
{

template<typename T>
concept Recoverable = requires (T t, const std::filesystem::path& path) {
    { t.saveCheckpoint() } -> std::same_as<void>;
    { T::fromCheckpoint(path) } -> mp::IsTypeOrPointerToType<T>;
};

}  // namespace taosim::serialization

//-------------------------------------------------------------------------
