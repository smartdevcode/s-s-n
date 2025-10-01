/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/accounting/Balance.hpp"
#include "taosim/accounting/Balances.hpp"

//-------------------------------------------------------------------------

namespace msgpack
{

MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{

namespace adaptor
{

template<>
struct pack<taosim::accounting::Balances>
{
    template<typename Stream>
    msgpack::packer<Stream>& operator()(
        msgpack::packer<Stream>& o, const taosim::accounting::Balances& v) const
    {
        using namespace std::string_literals;

        o.pack_map(9);

        o.pack("baseDecimals"s);
        o.pack(v.m_baseDecimals);

        o.pack("quoteDecimals"s);
        o.pack(v.m_quoteDecimals);

        o.pack("baseLoan"s);
        o.pack(v.m_baseLoan);

        o.pack("quoteLoan"s);
        o.pack(v.m_quoteLoan);

        o.pack("baseCollateral"s);
        o.pack(v.m_baseCollateral);

        o.pack("quoteCollateral"s);
        o.pack(v.m_quoteCollateral);

        o.pack("base"s);
        o.pack(v.base);

        o.pack("quote"s);
        o.pack(v.quote);

        o.pack("Loans"s);
        o.pack_array(v.m_loans.size());
        for (const auto& [id, loan] : v.m_loans) {
            o.pack_map(5);
            o.pack("id"s);
            o.pack(id);
            o.pack("amount"s);
            o.pack(loan.amount());
            o.pack("currency"s);
            o.pack(std::to_underlying(
                loan.direction() == OrderDirection::BUY ? Currency::QUOTE : Currency::BASE));
            o.pack("baseCollateral"s);
            o.pack(loan.collateral().base());
            o.pack("quoteCollateral"s);
            o.pack(loan.collateral().quote());
        }

        return o;
    }
};

}  // namespace adaptor

}  // MSGPACK_API_VERSION_NAMESPACE

}  // namespace msgpack

//-------------------------------------------------------------------------
