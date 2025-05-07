/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <map>
#include <memory>
#include <string>

//-------------------------------------------------------------------------

class ParameterStorage
{
public:
    using Ptr = std::shared_ptr<ParameterStorage>;

    using Key = std::string;
    using Val = std::string;

    ParameterStorage() = default;
    ParameterStorage(std::map<Key, Val> items) : m_parameterMap{std::move(items)} {}

    void set(const Key& key, const Val& val);
    const Val& get(const Key& key) const;

    bool tryGet(const Key& key, Val& val);
    [[nodiscard]] bool contains(const Key& key) const noexcept
    {
        return m_parameterMap.contains(key);
    }

    std::string processString(const std::string& stringToProcess);

    const Val& operator[](const Key& key) const;
    Val& operator[](const Key& key);

private:
    std::map<Key, Val> m_parameterMap;
};

//-------------------------------------------------------------------------
