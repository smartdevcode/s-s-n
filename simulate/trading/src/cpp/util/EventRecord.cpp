/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "EventRecord.hpp"

//-------------------------------------------------------------------------

L3Record& L3RecordContainer::operator[](BookId bookId)
{
    return m_underlying[bookId];
}

//-------------------------------------------------------------------------

L3Record& L3RecordContainer::at(BookId bookId)
{
    return m_underlying.at(bookId);
}

//-------------------------------------------------------------------------

const L3Record& L3RecordContainer::at(BookId bookId) const
{
    return m_underlying.at(bookId);
}

//-------------------------------------------------------------------------

void L3RecordContainer::clear() noexcept
{
    for ([[maybe_unused]] auto& [_, individualRecord] : m_underlying) {
        individualRecord.clear();
    }
}

//-------------------------------------------------------------------------

void L3RecordContainer::jsonSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        for (const auto& [bookId, record] : m_underlying) {
            record.jsonSerialize(json, std::to_string(bookId));
        }
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void L3RecordContainer::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        for (const auto& [bookId, record] : m_underlying) {
            record.checkpointSerialize(json, std::to_string(bookId));
        }
    };
    taosim::json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

L3RecordContainer L3RecordContainer::fromJson(const rapidjson::Value& json)
{
    L3RecordContainer container;
    for (const auto& member : json.GetObject()) {
        const char* name = member.name.GetString();
        const BookId bookId = std::stoi(name);
        for (const rapidjson::Value& entry : json[name].GetArray()) {
            std::string_view type = entry["event"].GetString();
            rapidjson::Document entryCopy;
            entryCopy.SetObject();
            auto& allocator = entryCopy.GetAllocator();
            if (type == "place") {
                rapidjson::Value entryCopy{entry, allocator};
                entryCopy.AddMember("bookId", rapidjson::Value{bookId}, allocator);
                container[bookId].push(OrderEvent::fromJson(entryCopy));
            }
            else if (type == "trade") {                    
                rapidjson::Value entryCopy{entry, allocator};
                entryCopy.AddMember("bookId", rapidjson::Value{bookId}, allocator);
                container[bookId].push(TradeEvent::fromJson(entryCopy));
            }
            else if (type == "cancel") {
                container[bookId].push(Cancellation::fromJson(entry));
            }
            else {
                throw std::runtime_error(fmt::format("Unknown L3 event '{}'", type));
            }
        }
    }
    return container;
}

//-------------------------------------------------------------------------
