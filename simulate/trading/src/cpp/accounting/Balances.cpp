/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "Balances.hpp"

//-------------------------------------------------------------------------

namespace taosim::accounting
{

//-------------------------------------------------------------------------

Balances::Balances(const BalancesDesc& desc) noexcept
    : base{desc.base},
      quote{desc.quote},
      m_baseDecimals{desc.roundParams.baseDecimals},
      m_quoteDecimals{desc.roundParams.quoteDecimals},
      m_roundParams{desc.roundParams}
{}

//-------------------------------------------------------------------------

Balances::Balances(
    Balance base, Balance quote, uint32_t baseDecimals, uint32_t quoteDecimals) noexcept
    : base{std::move(base)},
      quote{std::move(quote)},
      m_baseDecimals{baseDecimals},
      m_quoteDecimals{quoteDecimals},
      m_roundParams{.baseDecimals = baseDecimals, .quoteDecimals = quoteDecimals}
{}

//-------------------------------------------------------------------------

bool Balances::canBorrow(
    decimal_t collateralAmount, decimal_t price, OrderDirection direction) const noexcept
{
    const decimal_t requiredCollateral = direction == OrderDirection::BUY
        ? roundUpQuote(collateralAmount) : roundUpQuote(collateralAmount * price);
    return requiredCollateral <= getWealth(price);
}

//-------------------------------------------------------------------------

bool Balances::canFree(OrderID id, OrderDirection direction) const noexcept
{
    bool hasBaseReservation = base.getReservation(id).has_value();
    bool hasQuoteReservation = quote.getReservation(id).has_value();
    return hasBaseReservation || hasQuoteReservation;
}

//-------------------------------------------------------------------------

ReservationAmounts Balances::freeReservation(OrderID id, decimal_t price, decimal_t bestBid, decimal_t bestAsk,
    OrderDirection direction, std::optional<decimal_t> amount)
{
    
    if (getLeverage(id, direction) == 0_dec) {
        if (direction == OrderDirection::BUY) {
            const auto freed = ReservationAmounts{.quote = quote.freeReservation(id, amount)};
            quote.checkConsistency(std::source_location::current());
            return freed;
        } else {
            const auto freed = ReservationAmounts{.base = base.freeReservation(id, amount)};
            base.checkConsistency(std::source_location::current());
            return freed;
        }
    }

    const auto freed = [&] -> ReservationAmounts {
        if (!amount.has_value()) {
            return ReservationAmounts{
                .base = base.tryFreeReservation(id), .quote = quote.tryFreeReservation(id)};
        }
        if (direction == OrderDirection::BUY) {
            const decimal_t baseQuoteValue =
                roundQuote(base.getReservation(id).value_or(0_dec) * price);
            if (amount.value() <= baseQuoteValue) {
                return ReservationAmounts{.base = base.tryFreeReservation(id, amount.value())};
            } else {
                return ReservationAmounts{
                    .base = base.tryFreeReservation(id),
                    .quote = quote.tryFreeReservation(id, amount.value() - baseQuoteValue)
                };
            }
        }
        else {
            const decimal_t quoteBaseValue =
                roundBase(quote.getReservation(id).value_or(0_dec) / price);
            if (amount.value() <= quoteBaseValue) {
                return ReservationAmounts{.quote = quote.tryFreeReservation(id, amount.value())};
            } else {
                return ReservationAmounts{
                    .base = base.tryFreeReservation(id, amount.value() - quoteBaseValue),
                    .quote = quote.tryFreeReservation(id)
                };
            }
        }
    }();

    if (getReservationInQuote(id, price) == 0_dec && m_loans.find(id) == m_loans.end()) {
        (direction == OrderDirection::BUY ? m_buyLeverages : m_sellLeverages).erase(id);
    }

    base.checkConsistency(std::source_location::current());
    quote.checkConsistency(std::source_location::current());
    
    return freed;
}

//-------------------------------------------------------------------------

ReservationAmounts Balances::makeReservation(OrderID id, decimal_t price, decimal_t bestBid, decimal_t bestAsk,
    decimal_t amount, decimal_t leverage, OrderDirection direction)
{
    if (leverage == 0_dec) {
        if (direction == OrderDirection::BUY) {
            const ReservationAmounts reserved{.quote = quote.makeReservation(id, amount)};
            quote.checkConsistency(std::source_location::current());
            return reserved;
        } else {
            const ReservationAmounts reserved{.base = base.makeReservation(id, amount)};
            base.checkConsistency(std::source_location::current());
            return reserved;
        }
    }

    const auto reserved = [&] -> ReservationAmounts {
        if (direction == OrderDirection::BUY) {
            const auto reserved = [&] {
                if (quote.canReserve(amount)) {
                    return ReservationAmounts{.quote = quote.makeReservation(id, amount)};
                } else {
                    const decimal_t requiredBase = roundUpBase((amount - quote.getFree()) / price);
                    return ReservationAmounts{
                        .base = base.makeReservation(id, requiredBase),
                        .quote = quote.makeReservation(id, quote.getFree())
                    };
                }
            }();
            m_buyLeverages.insert({id, leverage});
            return reserved;
        }
        else {
            const auto reserved = [&] {
                if (base.canReserve(amount)) {
                    return ReservationAmounts{.base = base.makeReservation(id, amount)};
                } else {
                    const decimal_t requiredQuote = roundUpQuote((amount - base.getFree()) * price);
                    return ReservationAmounts{
                        .base = base.makeReservation(id, base.getFree()),
                        .quote = quote.makeReservation(id, requiredQuote)
                    };
                }
            }();
            m_sellLeverages.insert({id, leverage});
            return reserved;
        }
    }();

    base.checkConsistency(std::source_location::current());
    quote.checkConsistency(std::source_location::current());

    return reserved;
}

//-------------------------------------------------------------------------

std::vector<std::pair<OrderID, decimal_t>> Balances::commit(
    OrderID id,
    OrderDirection direction,
    decimal_t amount,
    decimal_t counterAmount,
    decimal_t fee,
    decimal_t bestBid,
    decimal_t bestAsk,
    decimal_t marginCallPrice,
    BookId bookId,
    SettleFlag settleFlag)
{
    
    amount = roundAmount(amount, direction);
    fee = roundAmount(fee, OrderDirection::BUY);
    const auto leverage = getLeverage(id, direction);

    if (leverage == 0_dec) {
        if (direction == OrderDirection::BUY) {
            quote.voidReservation(id, amount + fee);
            base.deposit(counterAmount);
        } else {
            base.voidReservation(id, amount);
            quote.deposit(counterAmount - fee);
        }
    } else {
        if (direction == OrderDirection::BUY) {
            borrow(id, direction, amount + fee, leverage, bestBid, bestAsk, marginCallPrice, bookId);
            base.deposit(counterAmount);
        } else {
            borrow(id, direction, amount, leverage, bestBid, bestAsk, marginCallPrice, bookId);
            quote.deposit(counterAmount - fee);
        }
    }

    if (std::holds_alternative<SettleType>(settleFlag)) {
        SettleType type = std::get<SettleType>(settleFlag);
        if (type == SettleType::NONE) {
            base.checkConsistency(std::source_location::current());
            quote.checkConsistency(std::source_location::current());
            return {};
        } else if (type == SettleType::FIFO) {
            const auto& ids = settleLoan(
                direction, 
                direction == OrderDirection::BUY ? counterAmount : counterAmount - fee, 
                direction == OrderDirection::BUY ? bestAsk : bestBid);
            return ids;
        }
    } else if (std::holds_alternative<OrderID>(settleFlag)) {
        OrderID marginOrderId = std::get<OrderID>(settleFlag);
        const auto& ids = settleLoan(
            direction, 
            direction == OrderDirection::BUY ? counterAmount : counterAmount - fee, 
            direction == OrderDirection::BUY ? bestAsk : bestBid, 
            marginOrderId);
        return ids;
    }

    return {};
}

//-------------------------------------------------------------------------

decimal_t Balances::getLeverage(OrderID id, OrderDirection direction) const noexcept
{
    const auto& cont = direction == OrderDirection::BUY ? m_buyLeverages : m_sellLeverages;
    auto it = cont.find(id);
    return it != cont.end() ? it->second : 0_dec;
}

//-------------------------------------------------------------------------

decimal_t Balances::getWealth(decimal_t price) const noexcept
{
    return util::fma(base.getFree(), price, quote.getFree());
}

//-------------------------------------------------------------------------

decimal_t Balances::getReservationInQuote(OrderID id, decimal_t price) const noexcept
{
    const decimal_t reserved = roundQuote(base.getReservation(id).value_or(0_dec) * price + 
                        quote.getReservation(id).value_or(0_dec));
    return reserved;
}

//-------------------------------------------------------------------------

decimal_t Balances::getReservationInBase(OrderID id, decimal_t price) const noexcept
{
    const decimal_t reserved = base.getReservation(id).value_or(0_dec) +
        roundBase(quote.getReservation(id).value_or(0_dec) / price); 
    return reserved;
}

//-------------------------------------------------------------------------

std::optional<std::reference_wrapper<const Loan>> Balances::getLoan(OrderID id) const noexcept
{
    auto it = m_loans.find(id);
    return it != m_loans.end() ? std::make_optional(std::cref(it->second)) : std::nullopt;
}

//-------------------------------------------------------------------------

decimal_t Balances::totalLoanInQuote(decimal_t price) const noexcept
{
    return util::fma(m_baseLoan, price, m_quoteLoan);
}

//-------------------------------------------------------------------------

void Balances::jsonSerialize(rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("baseDecimals", rapidjson::Value{m_baseDecimals}, allocator);
        json.AddMember("quoteDecimals", rapidjson::Value{m_quoteDecimals}, allocator);
        json.AddMember("quoteLoan", rapidjson::Value{taosim::util::decimal2double(m_quoteLoan)}, allocator);
        json.AddMember("baseLoan", rapidjson::Value{taosim::util::decimal2double(m_baseLoan)}, allocator);
        json.AddMember("quoteCollateral", rapidjson::Value{taosim::util::decimal2double(m_quoteCollateral)}, allocator);
        json.AddMember("baseCollateral", rapidjson::Value{taosim::util::decimal2double(m_baseCollateral)}, allocator);
        base.jsonSerialize(json, "base");
        quote.jsonSerialize(json, "quote");
        json::serializeHelper(
            json,
            "Loans",
            [this](rapidjson::Document& json) {
                json.SetArray();
                auto& allocator = json.GetAllocator();
                for (const auto& [id, loan] : m_loans) {
                    rapidjson::Document loanJson{rapidjson::kObjectType, &allocator};
                    loanJson.AddMember("id", rapidjson::Value{id}, allocator);
                    loanJson.AddMember("amount", rapidjson::Value{taosim::util::decimal2double(loan.amount())}, allocator);
                    loanJson.AddMember("currency", rapidjson::Value{
                        std::to_underlying(loan.direction() == OrderDirection::BUY ? Currency::QUOTE : Currency::BASE)
                    }, allocator);
                    loanJson.AddMember("baseCollateral", rapidjson::Value{taosim::util::decimal2double(loan.collateral().base())}, allocator);
                    loanJson.AddMember("quoteCollateral", rapidjson::Value{taosim::util::decimal2double(loan.collateral().quote())}, allocator);
                }
            }
        );
    };
    json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

void Balances::checkpointSerialize(
    rapidjson::Document& json, const std::string& key) const
{
    auto serialize = [this](rapidjson::Document& json) {
        json.SetObject();
        auto& allocator = json.GetAllocator();
        json.AddMember("baseDecimals", rapidjson::Value{m_baseDecimals}, allocator);
        json.AddMember("quoteDecimals", rapidjson::Value{m_quoteDecimals}, allocator);
        json.AddMember("quoteLoan", rapidjson::Value{taosim::util::decimal2double(m_quoteLoan)}, allocator);
        json.AddMember("baseLoan", rapidjson::Value{taosim::util::decimal2double(m_baseLoan)}, allocator);
        json.AddMember("quoteCollateral", rapidjson::Value{taosim::util::decimal2double(m_quoteCollateral)}, allocator);
        json.AddMember("baseCollateral", rapidjson::Value{taosim::util::decimal2double(m_baseCollateral)}, allocator);
        base.checkpointSerialize(json, "base");
        quote.checkpointSerialize(json, "quote");
        json::serializeHelper(
            json,
            "Loans",
            [this](rapidjson::Document& json) {
                json.SetArray();
                auto& allocator = json.GetAllocator();
                for (const auto& [id, loan] : m_loans) {
                    rapidjson::Document loanJson{rapidjson::kObjectType, &allocator};
                    loanJson.AddMember("id", rapidjson::Value{id}, allocator);
                    loanJson.AddMember("amount", rapidjson::Value{taosim::util::decimal2double(loan.amount())}, allocator);
                    loanJson.AddMember("currency", rapidjson::Value{
                        std::to_underlying(loan.direction() == OrderDirection::BUY ? Currency::QUOTE : Currency::BASE)
                    }, allocator);
                    loanJson.AddMember("baseCollateral", rapidjson::Value{taosim::util::decimal2double(loan.collateral().base())}, allocator);
                    loanJson.AddMember("quoteCollateral", rapidjson::Value{taosim::util::decimal2double(loan.collateral().quote())}, allocator);
                }
            }
        );
    };
    json::serializeHelper(json, key, serialize);
}

//-------------------------------------------------------------------------

Balances Balances::fromJson(const rapidjson::Value& json)
{
    return Balances{
        Balance::fromJson(json["base"]),
        Balance::fromJson(json["quote"]),
        json["baseDecimals"].GetUint(),
        json["quoteDecimals"].GetUint()
    };
}

//-------------------------------------------------------------------------

Balances Balances::fromXML(pugi::xml_node node, const RoundParams& roundParams)
{
    if (std::string_view{node.attribute("type").as_string()} == "pareto") {
        const auto scale = node.attribute("scale").as_double();
        const auto shape = node.attribute("shape").as_double();
        const auto wealth = node.attribute("wealth").as_double();
        const auto price = node.attribute("price").as_double();
        const auto symbol = node.attribute("symbol").as_string();
        std::mt19937 rng{std::random_device{}()};
        const auto u = std::uniform_real_distribution{0.0, 1.0}(rng);
        const auto r = scale * std::pow(1.0 - u, -1.0 / shape);
        return Balances({
            .base = Balance{
                decimal_t{1 / (1 + r) * wealth / price}, symbol, roundParams.baseDecimals},
            .quote = Balance{
                decimal_t{r / (1 + r) * wealth}, symbol, roundParams.quoteDecimals},
            .roundParams = roundParams});
    }
    else if (std::string_view{node.attribute("type").as_string()} == "pareto-50") {
        const auto scale = node.attribute("scale").as_double();
        const auto shape = node.attribute("shape").as_double();
        const auto wealth = node.attribute("wealth").as_double();
        const auto price = node.attribute("price").as_double();
        const auto symbol = node.attribute("symbol").as_string();
        std::mt19937 rng{std::random_device{}()};
        const auto u = std::uniform_real_distribution{0.0, 1.0}(rng);
        const auto r = scale * std::pow(1.0 - u, -1.0 / shape);
        if (std::bernoulli_distribution{0.5}(rng)) {
            return Balances({
                .base = Balance{
                    decimal_t{r / (1 + r) * wealth / price}, symbol, roundParams.baseDecimals},
                .quote = Balance{
                    decimal_t{1 / (1 + r) * wealth}, symbol, roundParams.quoteDecimals},
                .roundParams = roundParams});
        } else {
            return Balances({
                .base = Balance{
                    decimal_t{1 / (1 + r) * wealth / price}, symbol, roundParams.baseDecimals},
                .quote = Balance{
                    decimal_t{r / (1 + r) * wealth}, symbol, roundParams.quoteDecimals},
                .roundParams = roundParams});
        }
    }
    return Balances({
        .base = Balance::fromXML(node.child("Base"), roundParams.baseDecimals),
        .quote = Balance::fromXML(node.child("Quote"), roundParams.quoteDecimals),
        .roundParams = roundParams});
}

//-------------------------------------------------------------------------

std::vector<std::pair<OrderID, decimal_t>> Balances::settleLoan(
    OrderDirection direction, decimal_t amount, decimal_t price, std::optional<OrderID> marginOrderId)
{
    /*
        Settles the loan based on FIFO by default, unless the marginOrderId is specified
    */

    if (m_loans.empty() || amount <= 0_dec) return {};

    std::vector<std::pair<OrderID, decimal_t>> settledLoanIds;

    auto settleSingleLoan = [&](auto it) {
        auto& loan = it->second;
        if (loan.direction() == direction) return false;

        decimal_t settleAmount = std::min(loan.amount(), amount);
        auto collateral = loan.settle(
            settleAmount,
            price,
            {.baseDecimals = m_baseDecimals, .quoteDecimals = m_quoteDecimals}
        );

        amount = roundAmount(amount - settleAmount, loan.direction());
        m_baseCollateral -= collateral.base();
        m_quoteCollateral -= collateral.quote();

        if (direction == OrderDirection::BUY) {
            base.deposit(collateral.base() - settleAmount);
            quote.deposit(collateral.quote());
            m_baseLoan -= settleAmount;
        } else {
            base.deposit(collateral.base());
            quote.deposit(collateral.quote() - settleAmount);
            m_quoteLoan -= settleAmount;
        }

        if (loan.amount() == 0_dec) {
            settledLoanIds.emplace_back(it->first, loan.marginCallPrice());

            if (getReservationInQuote(it->first, price) == 0_dec) {
                auto& leverages = (direction == OrderDirection::BUY ? m_sellLeverages : m_buyLeverages);
                leverages.erase(it->first);
            }
            m_loans.erase(it);
        }

        return true;
    };

    if (marginOrderId) {
        auto it = m_loans.find(*marginOrderId);
        if (it != m_loans.end()) {
            settleSingleLoan(it);
        }
    } else {
        for (auto it = m_loans.begin(); it != m_loans.end() && amount > 0_dec;) {
            if (settleSingleLoan(it)) {
                it = m_loans.begin();
            } else {
                ++it;
            }
        }
    }

    base.checkConsistency(std::source_location::current());
    quote.checkConsistency(std::source_location::current());

    return settledLoanIds;
}


//-------------------------------------------------------------------------

void Balances::borrow(
    OrderID id,
    OrderDirection direction,
    decimal_t amount,
    decimal_t leverage,
    decimal_t bestBid,
    decimal_t bestAsk,
    decimal_t marginCallPrice,
    BookId bookId)
{
    Collateral collateral;
    const decimal_t collateralAmount = roundAmount(amount / util::dec1p(leverage), direction);

    if (direction == OrderDirection::BUY) {
        const auto quoteReserved = quote.getReservation(id).value_or(0_dec);
        if (quoteReserved >= collateralAmount) {
            collateral.quote() = collateralAmount;
        } else {
            decimal_t remainingBase = roundUpBase((collateralAmount - quoteReserved) / bestAsk);
            const auto baseReserved = base.getReservation(id).value_or(0_dec);
            // if (remainingBase > baseReserved) {
            //     fmt::println(
            //         "BOOK : {}, ORDER {}: borrow with amount={} and leverage={} (CollAmount={}), bestBid={}, bestAsk={} "
            //         "baseReserved={}, quoteReserved={}; remainingBase ({}) exceeded baseReserved ({})",
            //         bookId, id, amount, leverage, collateralAmount, bestBid, bestAsk, baseReserved, quoteReserved, remainingBase, baseReserved);
            //     remainingBase = baseReserved;
            // }
            collateral.base() = remainingBase;
            collateral.quote() = quoteReserved;
        }
    } else {
        const auto baseReserved = base.getReservation(id).value_or(0_dec);
        if (baseReserved >= collateralAmount) {
            collateral.base() = collateralAmount;
        } else {
            decimal_t remainingQuote = roundUpQuote((collateralAmount - baseReserved) * bestBid);
            const auto quoteReserved = quote.getReservation(id).value_or(0_dec);
            if (remainingQuote > quoteReserved) {
                fmt::println(
                    "BOOK : {}, ORDER {}: borrow with amount={} and leverage={} (CollAmount={}), bestBid={}, bestAsk={} "
                    "baseReserved={}, quoteReserved={}; remainingQuote ({}) exceeded quoteReserved ({})",
                    bookId, id, amount, leverage, collateralAmount, bestBid, bestAsk, baseReserved, quoteReserved, remainingQuote, quoteReserved);
                remainingQuote = quoteReserved;
            }
            collateral.base() = baseReserved;
            collateral.quote() = remainingQuote;
        }
    }

    m_baseCollateral += collateral.base();
    m_quoteCollateral += collateral.quote();

    decimal_t loanAmount = [&] {
        if (direction == OrderDirection::BUY) {
            const auto loanAmount =
                roundQuote(collateral.valueInQuote(bestAsk) * util::dec1p(leverage));
            m_quoteLoan += loanAmount;
            return std::min(loanAmount, amount);
        } else {
            const auto loanAmount =
                roundBase(collateral.valueInBase(bestBid) * util::dec1p(leverage)); 
            m_baseLoan += loanAmount;
            return std::min(loanAmount, amount);
        }
    }();

    if (collateral.base() > 0_dec) base.voidReservation(id, collateral.base());
    if (collateral.quote() > 0_dec) quote.voidReservation(id, collateral.quote());

     // checking if there is no reservation left
    if (!base.getReservation(id).has_value() && !quote.getReservation(id).has_value()) {
        // if (loanAmount != amount){
        //     fmt::println(
        //         "BOOK : {}, ORDER {}: borrow with amount={} and leverage={} (CollAmount={}), bestBid={}, bestAsk={} "
        //         "Calculated loanAmount={} was not equal to the requested amount={} to borrow",
        //         bookId, id, amount, leverage, collateralAmount, bestBid, bestAsk, loanAmount, amount);
        // }
        loanAmount = amount;
    } 

    Loan loan({
        .amount = loanAmount,
        .direction = direction,
        .leverage = leverage,
        .collateral = collateral,
        .marginCallPrice = marginCallPrice
    });

    auto it = m_loans.find(id);
    if (it != m_loans.end()) {
        it->second += loan;
    } else {
        m_loans.insert({id, loan});
    }

    base.checkConsistency(std::source_location::current());
    quote.checkConsistency(std::source_location::current());
}

//-------------------------------------------------------------------------

decimal_t Balances::roundAmount(decimal_t amount, OrderDirection direction) const noexcept
{
    return util::round(
        amount,
        direction == OrderDirection::BUY 
            ? m_roundParams.quoteDecimals : m_roundParams.baseDecimals);
}

//-------------------------------------------------------------------------

std::optional<decimal_t> Balances::roundAmount(
    std::optional<decimal_t> amount, OrderDirection direction) const noexcept
{
    return amount.transform([&](decimal_t val) { return roundAmount(val, direction); });
}

//-------------------------------------------------------------------------

decimal_t Balances::roundBase(decimal_t amount) const noexcept
{
    return util::round(amount, m_baseDecimals);
}

//-------------------------------------------------------------------------

decimal_t Balances::roundQuote(decimal_t amount) const noexcept
{
    return util::round(amount, m_quoteDecimals);
}

//-------------------------------------------------------------------------

decimal_t Balances::roundUpAmount(decimal_t amount, OrderDirection direction) const noexcept
{
    return util::roundUp(
        amount,
        direction == OrderDirection::BUY 
            ? m_roundParams.quoteDecimals : m_roundParams.baseDecimals);
}

//-------------------------------------------------------------------------

decimal_t Balances::roundUpBase(decimal_t amount) const noexcept
{
    return util::roundUp(amount, m_baseDecimals);
}

//-------------------------------------------------------------------------

decimal_t Balances::roundUpQuote(decimal_t amount) const noexcept
{
    return util::roundUp(amount, m_quoteDecimals);
}

//-------------------------------------------------------------------------

}  // namespace taosim::accounting

//-------------------------------------------------------------------------
