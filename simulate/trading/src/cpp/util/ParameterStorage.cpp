/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "ParameterStorage.hpp"

#include "SimulationException.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <source_location>

//-------------------------------------------------------------------------

void ParameterStorage::set(const Key& key, const Val& val)
{
    m_parameterMap[key] = val;
}

//-------------------------------------------------------------------------

const ParameterStorage::Val& ParameterStorage::get(const Key& key) const
{
    if (const auto it = m_parameterMap.find(key); it != m_parameterMap.end()) {
        return it->second;
    }
    throw SimulationException(fmt::format(
        "{}: no parameter with name '{}' is currently in the parameter storage",
        std::source_location::current().function_name(),
        key));
}

//-------------------------------------------------------------------------

bool ParameterStorage::tryGet(const Key& name, Val& val)
{
    if (const auto it = m_parameterMap.find(name); it != m_parameterMap.end()) {
        val = it->second;
        return true;
    }
    return false;
}

//-------------------------------------------------------------------------

std::string ParameterStorage::processString(const std::string& stringToProcess)
{
    std::string ret;

    auto it = stringToProcess.cbegin();
    const auto endIt = stringToProcess.cend();
    do {
        const auto dollarIt = std::find(it, endIt, '$');
        ret.append(it, dollarIt);
        it = dollarIt;
        if (dollarIt != endIt) {
            if (++it != endIt) {
                if (*it == '{') {
                    const auto paramEndIt = std::find(++it, endIt, '}');
                    if (paramEndIt != endIt) {
                        const std::string paramName = std::string(it, paramEndIt);
                        std::string paramValue;
                        if (tryGet(paramName, paramValue)) {
                            ret.append(paramValue);
                        }
                        else {
                            throw SimulationException(fmt::format(
                                "{}: unknown parameter name '{}' encountered in the "
                                "string '{}'",
                                std::source_location::current().function_name(),
                                paramName,
                                stringToProcess));
                        }
                        it = paramEndIt;
                        ++it;
                    }
                    else {
                        throw SimulationException(fmt::format(
                            "{}: parameter reference opening '${{' encountered but no "
                            "matching closing bracket '}}' found "
                            "in the string '{}'",
                            std::source_location::current().function_name(),
                            stringToProcess));
                    }
                }
                else {
                    --it;
                }
            }
        }
    } while (it != endIt);

    return ret;
}

//-------------------------------------------------------------------------

const ParameterStorage::Val& ParameterStorage::operator[](const Key& key) const
{
    return get(key);
}

//-------------------------------------------------------------------------

ParameterStorage::Val& ParameterStorage::operator[](const Key& key)
{
    if (const auto it = m_parameterMap.find(key); it != m_parameterMap.end()) {
        return it->second;
    }
    throw SimulationException(fmt::format(
        "{}: no parameter with name '{}' is currently in the parameter storage",
        std::source_location::current().function_name(),
        key));
}

//-------------------------------------------------------------------------
