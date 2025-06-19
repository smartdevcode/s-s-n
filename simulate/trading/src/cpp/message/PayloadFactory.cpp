/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "PayloadFactory.hpp"

#include "Balance.hpp"
#include "Cancellation.hpp"
#include "MultiBookMessagePayloads.hpp"
#include "Order.hpp"

#include <fmt/core.h>

#include <vector>

//-------------------------------------------------------------------------

MessagePayload::Ptr PayloadFactory::createFromJsonMessage(const rapidjson::Value& json)
{
    const auto& payloadJson = json["payload"];
    std::string_view type = json["type"].GetString();

    if (type == "PLACE_ORDER_MARKET") {
        return PlaceOrderMarketPayload::fromJson(payloadJson);
    }
    else if (type == "RESPONSE_PLACE_ORDER_MARKET") {
        return PlaceOrderMarketResponsePayload::fromJson(payloadJson);
    }
    else if (type == "ERROR_RESPONSE_PLACE_ORDER_MARKET") {
        return PlaceOrderMarketErrorResponsePayload::fromJson(payloadJson);
    }
    else if (type == "PLACE_ORDER_LIMIT") {
        return PlaceOrderLimitPayload::fromJson(payloadJson);
    }
    else if (type == "RESPONSE_PLACE_ORDER_LIMIT") {
        return PlaceOrderLimitResponsePayload::fromJson(payloadJson);
    }
    else if (type == "ERROR_RESPONSE_PLACE_ORDER_LIMIT") {
        return PlaceOrderLimitErrorResponsePayload::fromJson(payloadJson);
    }
    else if (type == "RETRIEVE_ORDERS") {
        return RetrieveOrdersPayload::fromJson(payloadJson);
    }
    else if (type == "CANCEL_ORDERS") {
        return CancelOrdersPayload::fromJson(payloadJson);
    }
    else if (type == "RESPONSE_CANCEL_ORDERS") {
        return CancelOrdersResponsePayload::fromJson(payloadJson);
    }
    else if (type == "ERROR_RESPONSE_CANCEL_ORDERS") {
        return CancelOrdersErrorResponsePayload::fromJson(payloadJson);
    }
    else if (type == "RETRIEVE_L1") {
        return RetrieveL1Payload::fromJson(payloadJson);
    }
    else if (type == "RESPONSE_RETRIEVE_L1") {
        return RetrieveL1ResponsePayload::fromJson(payloadJson);
    }
    else if (type == "RETRIEVE_BOOK_ASK") {
        return RetrieveBookPayload::fromJson(payloadJson);
    }
    else if (type == "RETRIEVE_BOOK_BID") {
        return RetrieveBookPayload::fromJson(payloadJson);
    }
    else if (type == "SUBSCRIBE_EVENT_ORDER_MARKET") {
        return MessagePayload::create<EmptyPayload>();
    }
    else if (type == "SUBSCRIBE_EVENT_ORDER_LIMIT") {
        return MessagePayload::create<EmptyPayload>();
    }
    else if (type == "SUBSCRIBE_EVENT_TRADE") {
        return MessagePayload::create<EmptyPayload>();
    }
    else if (type == "SUBSCRIBE_EVENT_ORDER_TRADE") {
        return SubscribeEventTradeByOrderPayload::fromJson(payloadJson);
    }
    // TODO: Is this of use?
    else if (type == "REGISTER_ACCOUNT") {
        
    }
    else if (type == "RESET_AGENT") {
        return ResetAgentsPayload::fromJson(payloadJson);
    }
    else if (type == "RESPONSE_RESET_AGENT") {
        return ResetAgentsResponsePayload::fromJson(payloadJson);
    }
    else if (type == "ERROR_RESPONSE_RESET_AGENT") {
        return ResetAgentsErrorResponsePayload::fromJson(payloadJson);
    }
    else if (type == "EVENT_SIMULATION_START" || type == "EVENT_SIMULATION_END") {
        return MessagePayload::create<EmptyPayload>();
    }
    
    throw std::runtime_error(fmt::format("Unrecognized message type '{}'", type));
}

//-------------------------------------------------------------------------