/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "CheckpointSerializable.hpp"
#include "FreeInfo.hpp"
#include "JsonSerializable.hpp"
#include "Loan.hpp"
#include "Order.hpp"
#include "common.hpp"

//-------------------------------------------------------------------------

namespace taosim::accounting
{

//-------------------------------------------------------------------------

class Balance : public JsonSerializable, public CheckpointSerializable
{
public:
    explicit Balance(
        decimal_t total = {},
        const std::string& symbol = {},
        uint32_t roundingDecimals = 4);

    ~Balance() noexcept = default;
    Balance(const Balance&) noexcept = default;
    Balance& operator=(const Balance&) noexcept = default;
    Balance(Balance&& other) noexcept;
    Balance& operator=(Balance&& other) noexcept;

    [[nodiscard]] decimal_t getFree() const noexcept { return m_free; }
    [[nodiscard]] decimal_t getTotal() const noexcept { return m_total; }
    [[nodiscard]] decimal_t getReserved() const noexcept { return m_reserved; }
    [[nodiscard]] std::optional<decimal_t> getReservation(OrderID id) const noexcept;
    [[nodiscard]] const std::map<OrderID, decimal_t>& getReservations() const noexcept;

    [[nodiscard]] FreeInfo canFree(OrderID id, std::optional<decimal_t> amount = {}) const noexcept;
    [[nodiscard]] bool canReserve(decimal_t amount) const noexcept;

    void deposit(decimal_t amount);
    decimal_t makeReservation(OrderID id, decimal_t amount);
    decimal_t freeReservation(OrderID id, std::optional<decimal_t> amount = {});
    decimal_t tryFreeReservation(OrderID orderId, std::optional<decimal_t> amount = {});
    void voidReservation(OrderID id, std::optional<decimal_t> amount = {});

    virtual void jsonSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;
    virtual void checkpointSerialize(
        rapidjson::Document& json, const std::string& key = {}) const override;

    friend std::ostream& operator<<(std::ostream& os, const Balance& bal) noexcept;

    [[nodiscard]] static Balance fromXML(pugi::xml_node node, uint32_t roundingDecimals);
    [[nodiscard]] static Balance fromJson(const rapidjson::Value& json);

private:
    void checkConsistency(std::source_location sl) const;
    void move(Balance&& other) noexcept;

    [[nodiscard]] decimal_t roundAmount(decimal_t amount) const;
    [[nodiscard]] std::optional<decimal_t> roundAmount(std::optional<decimal_t> amount) const;   

    decimal_t m_free{};
    decimal_t m_reserved{};
    decimal_t m_total{};
    std::map<OrderID, decimal_t> m_reservations;
    std::string m_symbol;
    uint32_t m_roundingDecimals;

    friend class Balances;
};

//-------------------------------------------------------------------------

}  // namespace taosim::accounting

//-------------------------------------------------------------------------

template<>
struct fmt::formatter<taosim::accounting::Balance>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const taosim::accounting::Balance& bal, FormatContext& ctx)
    {
        return fmt::format_to(
            ctx.out(),
            "{} ({} | {})",
            bal.getTotal(),
            bal.getFree(),
            bal.getReserved());
    }
};

//-------------------------------------------------------------------------
