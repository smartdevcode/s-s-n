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
class EventRecord : public JsonSerializable
{
public:
    using Entry = std::variant<Ts...>;

    [[nodiscard]] size_t size() const noexcept { return m_entries.size(); }

    void push(Entry entry) noexcept { m_entries.push_back(std::move(entry)); }
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

private:
    std::vector<Entry> m_entries;
};

//-------------------------------------------------------------------------

using L3Record = EventRecord<OrderEvent, TradeEvent, CancellationEvent>;

//-------------------------------------------------------------------------

class L3RecordContainer : public JsonSerializable
{
public:
    L3RecordContainer() noexcept = default;
    explicit L3RecordContainer(uint32_t bookCount) noexcept : m_underlying(bookCount) {}

    auto&& operator[](this auto&& self, BookId bookId) { return self.m_underlying[bookId]; }

    auto&& at(this auto&& self, BookId bookId) { return self.m_underlying.at(bookId); }

    void clear() noexcept;

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

private:
    std::vector<L3Record> m_underlying;
};

//-------------------------------------------------------------------------
