/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <fmt/format.h>

#include <cstdint>

//-------------------------------------------------------------------------

enum class MessageType : uint8_t
{
    PLACE_ORDER_MARKET,
    PLACE_ORDER_LIMIT,
    RETRIEVE_ORDERS,
    CANCEL_ORDERS,
    RETRIEVE_L1,
    RETRIEVE_BOOK_ASK,
    RETRIEVE_BOOK_BID,
    SUBSCRIBE_EVENT_ORDER_MARKET,
    SUBSCRIBE_EVENT_ORDER_LIMIT,
    SUBSCRIBE_EVENT_TRADE,
    SUBSCRIBE_EVENT_ORDER_TRADE
};

[[nodiscard]] constexpr const char* MessageType2String(MessageType msgType) noexcept
{
    switch (msgType) {
        case MessageType::PLACE_ORDER_MARKET:
            return "PLACE_ORDER_MARKET";
        case MessageType::PLACE_ORDER_LIMIT:
            return "PLACE_ORDER_LIMIT";
        case MessageType::RETRIEVE_ORDERS:
            return "RETRIEVE_ORDERS";
        case MessageType::CANCEL_ORDERS:
            return "CANCEL_ORDERS";
        case MessageType::RETRIEVE_L1:
            return "RETRIEVE_L1";
        case MessageType::RETRIEVE_BOOK_ASK:
            return "RETRIEVE_BOOK_ASK";
        case MessageType::RETRIEVE_BOOK_BID:
            return "RETRIEVE_BOOK_BID";
        case MessageType::SUBSCRIBE_EVENT_ORDER_MARKET:
            return "SUBSCRIBE_EVENT_ORDER_MARKET";
        case MessageType::SUBSCRIBE_EVENT_ORDER_LIMIT:
            return "SUBSCRIBE_EVENT_ORDER_LIMIT";
        case MessageType::SUBSCRIBE_EVENT_TRADE:
            return "SUBSCRIBE_EVENT_TRADE";
        case MessageType::SUBSCRIBE_EVENT_ORDER_TRADE:
            return "SUBSCRIBE_EVENT_ORDER_TRADE";
        default:
            return "Unknown MessageType";
    }
}

template<>
struct fmt::formatter<MessageType>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const MessageType& msgType, FormatContext& ctx)
    {
        return fmt::format_to(ctx.out(), "{}", MessageType2String(msgType));
    }
};

//-------------------------------------------------------------------------
