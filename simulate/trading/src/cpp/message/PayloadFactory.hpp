/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ExchangeAgentMessagePayloads.hpp"
#include "MessagePayload.hpp"

//-------------------------------------------------------------------------

class PayloadFactory
{
public:
    [[nodiscard]] static MessagePayload::Ptr createFromJsonMessage(const rapidjson::Value& json);

private:
    PayloadFactory() = default;
};

//-------------------------------------------------------------------------
