/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "Cancellation.hpp"
#include "CheckpointSerializable.hpp"
#include "JsonSerializable.hpp"
#include "OrderFactory.hpp"
#include "Trade.hpp"
#include "mp.hpp"
#include "util.hpp"

#include <map>
#include <variant>
#include <vector>

//-------------------------------------------------------------------------

template<typename... Ts>
class EventRecord : public JsonSerializable, public CheckpointSerializable
{
public:
    using Entry = std::variant<Ts...>;

    [[nodiscard]] size_t size() const noexcept { return m_entries.size(); }

    void push(Entry entry) noexcept { m_entries.push_back(entry); }
    void clear() noexcept { m_entries.clear(); }

    [[nodiscard]] decltype(auto) begin(this auto&& self)
    {
        return std::forward_like<decltype(self)>(self.m_entries.begin());
    }

    [[nodiscard]] decltype(auto) end(this auto&& self)
    {
        return std::forward_like<decltype(self)>(self.m_entries.end());
    }

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const
    {
        auto serialize = [this](rapidjson::Document& json) {
            json.SetArray();
            auto& allocator = json.GetAllocator();
            for (const auto& entry : m_entries) {
                std::visit(
                    [&](auto&& entry) {
                        using T = std::remove_cvref_t<decltype(entry)>;
                        rapidjson::Document entryJson{&allocator};
                        if constexpr (taosim::mp::IsPointer<T>) {
                            entry->jsonSerialize(entryJson);
                        } else {
                            entry.jsonSerialize(entryJson);
                        }
                        json.PushBack(entryJson, allocator);
                    },
                    entry);
            }
            if (json.Size() == 0) {
                json.SetNull();
            }
        };
        taosim::json::serializeHelper(json, key, serialize);
    }

    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const
    {
        auto serialize = [this](rapidjson::Document& json) {
            json.SetArray();
            auto& allocator = json.GetAllocator();
            for (const auto& entry : m_entries) {
                std::visit(
                    [&](auto&& entry) {
                        using T = std::remove_cvref_t<decltype(entry)>;
                        rapidjson::Document entryJson{&allocator};
                        if constexpr (taosim::mp::IsPointer<T>) {
                            entry->checkpointSerialize(entryJson);
                        } else {
                            entry.checkpointSerialize(entryJson);
                        }
                        json.PushBack(entryJson, allocator);
                    },
                    entry);
            }
            if (json.Size() == 0) {
                json.SetNull();
            }
        };
        taosim::json::serializeHelper(json, key, serialize);
    }

private:
    std::vector<Entry> m_entries;
};

//-------------------------------------------------------------------------

using L3Record = EventRecord<OrderEvent::Ptr, TradeEvent::Ptr, Cancellation::Ptr>;

//-------------------------------------------------------------------------

class L3RecordContainer : public JsonSerializable, public CheckpointSerializable
{
public:
    L3Record& operator[](BookId bookId);

    L3Record& at(BookId bookId);
    const L3Record& at(BookId bookId) const;

    void clear() noexcept;

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    [[nodiscard]] static L3RecordContainer fromJson(const rapidjson::Value& json);

private:
    std::map<BookId, L3Record> m_underlying;
};

//-------------------------------------------------------------------------
