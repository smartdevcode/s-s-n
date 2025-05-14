/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "OrderPlacementValidator.hpp"

#include "MultiBookExchangeAgent.hpp"
#include "Simulation.hpp"

//-------------------------------------------------------------------------

namespace taosim::exchange
{

//-------------------------------------------------------------------------

OrderPlacementValidator::OrderPlacementValidator(
    const OrderPlacementValidator::Parameters& params, MultiBookExchangeAgent* exchange) noexcept
    : m_params{params},
      m_exchange{exchange}
{}

//-------------------------------------------------------------------------

OrderPlacementValidator::ExpectedResult
    OrderPlacementValidator::validateMarketOrderPlacement(
        const accounting::Account& account,
        Book::Ptr book,
        PlaceOrderMarketPayload::Ptr payload,
        FeePolicy& feePolicy,
        decimal_t maxLeverage,
        decimal_t maxLoan,
        AgentId agentId) const
{
    // A market order is valid if the initiating account has either
    //   - sufficient funds to at least partially collect the requested shares from the book (buy)
    //   - enough inventory *and* the book can at least partially fill the order (sell)
    // AND
    //   - the order volume respects the minimum increment

    payload->volume = util::round(payload->volume, m_params.volumeIncrementDecimals);
    payload->leverage = util::round(payload->leverage, m_params.volumeIncrementDecimals);
    const decimal_t payloadTotalVolume = util::round(
        payload->volume * util::dec1p(payload->leverage), m_params.volumeIncrementDecimals);

    if (payload->leverage < 0_dec || payload->leverage > maxLeverage)
        return std::unexpected{OrderErrorCode::INVALID_LEVERAGE};

    if (payload->volume <= 0_dec)
        return std::unexpected{OrderErrorCode::INVALID_VOLUME};
    
    const auto& balances = account.at(book->id());
    const auto& baseBalance = balances.base;
    const auto& quoteBalance = balances.quote;

    if (payload->direction == OrderDirection::BUY) {
        if (book->sellQueue().empty()) {
            return std::unexpected{OrderErrorCode::EMPTY_BOOK};
        }
        decimal_t volume{};
        decimal_t volumeWeightedPrice{};
        for (auto it = book->sellQueue().cbegin(); it != book->sellQueue().cend(); ++it) {
            const auto& level = *it;
            for (const auto tick : level) {
                if (volume + tick->totalVolume() >= payloadTotalVolume) {
                    const decimal_t partialVolume = payloadTotalVolume - volume;
                    volume += partialVolume;
                    decimal_t tradeCost = util::round(tick->price() * partialVolume * util::dec1p(feePolicy.getRates().taker), m_params.quoteIncrementDecimals);
                    volumeWeightedPrice += tradeCost;
                    m_exchange->simulation()->logDebug(
                        "{} | AGENT #{} BOOK {} : CALCULATED PRE-RESERVATION OF {} QUOTE ({}*{}*{}) FOR TRADE OF BUY ORDER {}x{}@MARKET AGAINST {}@{}",
                         m_exchange->simulation()->currentTimestamp(), agentId, book->id(), tradeCost, partialVolume, tick->price(), util::dec1p(feePolicy.getRates().taker), util::dec1p(payload->leverage), payload->volume, tick->totalVolume(), tick->price());
                    goto checkQuoteBalance;
                }
                volume += tick->totalVolume();
                const decimal_t tradeCost = util::round(tick->price() * tick->totalVolume() * util::dec1p(feePolicy.getRates().taker), m_params.quoteIncrementDecimals);
                volumeWeightedPrice += tradeCost;
                m_exchange->simulation()->logDebug(
                    "{} | AGENT #{} BOOK {} : CALCULATED PRE-RESERVATION OF {} QUOTE ({}*{}*{}) FOR TRADE OF BUY ORDER {}x{}@MARKET AGAINST {}@{}",
                     m_exchange->simulation()->currentTimestamp(), agentId, book->id(), tradeCost, tick->totalVolume(), tick->price(), util::dec1p(feePolicy.getRates().taker), util::dec1p(payload->leverage), payload->volume, tick->totalVolume(), tick->price());
            }
        }
        checkQuoteBalance:
        volumeWeightedPrice = util::round(volumeWeightedPrice, m_params.quoteIncrementDecimals);
        if (payload->leverage == 0_dec){
            if (!quoteBalance.canReserve(volumeWeightedPrice)) {
                return std::unexpected{OrderErrorCode::INSUFFICIENT_QUOTE};
            }
        } else {
            volumeWeightedPrice = util::round(volumeWeightedPrice / util::dec1p(payload->leverage), m_params.quoteIncrementDecimals);
            const decimal_t price = book->bestAsk();
            if (!balances.canBorrow(volumeWeightedPrice, price, payload->direction) ||
                volumeWeightedPrice * payload->leverage + balances.totalLoanInQuote(price) > maxLoan) {
                return std::unexpected{OrderErrorCode::EXCEEDING_LOAN};
            }
        }
        return OrderPlacementValidator::ExpectedResult{
            Result{
                .direction = payload->direction,
                .amount = volumeWeightedPrice,
                .leverage = payload->leverage
            }
        };
    }
    else {
        if (book->buyQueue().empty()) {
            return std::unexpected{OrderErrorCode::EMPTY_BOOK};
        }
        if (payload->leverage == 0_dec){
            if (!baseBalance.canReserve(payload->volume)) {
                return std::unexpected{OrderErrorCode::INSUFFICIENT_BASE};
            }
        } else {
            payload->volume = util::round(payload->volume / util::dec1p(payload->leverage), m_params.baseIncrementDecimals);
            const decimal_t price = book->bestBid();
            if (!balances.canBorrow(payload->volume, price, payload->direction) ||
            payload->volume * payload->leverage + balances.totalLoanInQuote(price) > maxLoan) {
                return std::unexpected{OrderErrorCode::EXCEEDING_LOAN};
            }
        }
        return Result{
            .direction = payload->direction,
            .amount = payload->volume,
            .leverage = payload->leverage
        };
    }
}

//-------------------------------------------------------------------------

OrderPlacementValidator::ExpectedResult
    OrderPlacementValidator::validateLimitOrderPlacement(
        const accounting::Account& account,
        Book::Ptr book,
        PlaceOrderLimitPayload::Ptr payload,
        FeePolicy& feePolicy,
        decimal_t maxLeverage,
        decimal_t maxLoan,
        AgentId agentId) const
{
    // A limit order is valid if the initiating account has either
    //   - sufficient funds to place the order (limit buy)
    //   - sufficient inventory available to cover the to-be-sold volume (limit sell)
    // AND
    //   - the price and volume of the order are in accord with their respective minimum increments

    payload->price = util::round(payload->price, m_params.priceIncrementDecimals);
    payload->volume = util::round(payload->volume, m_params.volumeIncrementDecimals);
    payload->leverage = util::round(payload->leverage, m_params.volumeIncrementDecimals);
    const auto payloadTotalVolume = util::round(
        payload->volume * util::dec1p(payload->leverage), m_params.volumeIncrementDecimals);
    
    if (payload->leverage < 0_dec || payload->leverage > maxLeverage) {
        return std::unexpected{OrderErrorCode::INVALID_LEVERAGE};
    }

    if (payload->volume <= 0_dec) {
        return std::unexpected{OrderErrorCode::INVALID_VOLUME};
    }   

    auto violationChecker = limitOrderFlag2ViolationChecker.at(std::to_underlying(payload->flag));
    const bool violatesContract = violationChecker(book, payload);
    if (violatesContract) {
        return std::unexpected{OrderErrorCode::CONTRACT_VIOLATION};
    }

    const auto& balances = account.at(book->id());
    const auto& baseBalance = balances.base;
    const auto& quoteBalance = balances.quote;

    if (payload->direction == OrderDirection::BUY) {
        decimal_t takerVolume{};
        decimal_t takerTotalPrice{};
        for (auto it = book->sellQueue().cbegin(); it != book->sellQueue().cend(); ++it) {
            const auto& level = *it;
            if (payload->price < level.price()) break;
            for (const auto tick : level) {
                if (takerVolume + tick->totalVolume() >= payloadTotalVolume) {
                    const decimal_t partialVolume = payloadTotalVolume - takerVolume;
                    takerVolume += partialVolume;
                    decimal_t tradeCost = util::round(tick->price() * partialVolume * util::dec1p(feePolicy.getRates().taker), m_params.quoteIncrementDecimals);
                    takerTotalPrice += tradeCost;
                    m_exchange->simulation()->logDebug(
                        "{} | AGENT #{} BOOK {} : CALCULATED PRE-RESERVATION OF {} QUOTE ({}*{}*{}) FOR TRADE OF BUY ORDER {}x{}@{} AGAINST {}@{}",
                         m_exchange->simulation()->currentTimestamp(), agentId, book->id(),
                          tradeCost, partialVolume, tick->price(), util::dec1p(feePolicy.getRates().taker), util::dec1p(payload->leverage), payload->volume, payload->price, tick->totalVolume(), tick->price());
                    goto checkQuoteBalance;
                }
                takerVolume += tick->totalVolume();
                decimal_t tradeCost = util::round(tick->price() * tick->totalVolume() * util::dec1p(feePolicy.getRates().taker), m_params.quoteIncrementDecimals);
                takerTotalPrice += tradeCost;
                m_exchange->simulation()->logDebug(
                    "{} | AGENT #{} BOOK {} : CALCULATED PRE-RESERVATION OF {} QUOTE ({}*{}*{}) FOR TRADE OF BUY ORDER {}x{}@{} AGAINST {}@{}",
                     m_exchange->simulation()->currentTimestamp(), agentId, book->id(), 
                     tradeCost, tick->totalVolume(), tick->price(), util::dec1p(feePolicy.getRates().taker), util::dec1p(payload->leverage), payload->volume, payload->price, tick->totalVolume(), tick->price());
            }
        }
        checkQuoteBalance:
        takerTotalPrice = util::round(takerTotalPrice, m_params.quoteIncrementDecimals);
        
        const decimal_t makerVolume = payloadTotalVolume - takerVolume;
        const decimal_t makerTotalPrice = util::round(payload->price * makerVolume * util::dec1p(feePolicy.getRates().maker), m_params.quoteIncrementDecimals);
        m_exchange->simulation()->logDebug(
            "{} | AGENT #{} BOOK {} : CALCULATED PRE-RESERVATION OF {} QUOTE ({}*{}*{}) FOR PLACE OF BUY ORDER {}x{}@{}",
             m_exchange->simulation()->currentTimestamp(), agentId, book->id(), 
             makerTotalPrice, makerVolume, payload->price, util::dec1p(feePolicy.getRates().maker), util::dec1p(payload->leverage), payload->volume, payload->price);
        decimal_t volumeWeightedPrice = util::round(takerTotalPrice + makerTotalPrice, m_params.quoteIncrementDecimals);


        if (payload->leverage == 0_dec){
            if (!quoteBalance.canReserve(volumeWeightedPrice)) {
                return std::unexpected{OrderErrorCode::INSUFFICIENT_QUOTE};
            }
        } else {
            volumeWeightedPrice = util::round(volumeWeightedPrice / util::dec1p(payload->leverage), m_params.quoteIncrementDecimals);
            const decimal_t price = payload->price; //book->bestAsk();
            if (!balances.canBorrow(volumeWeightedPrice, price, payload->direction)||
                volumeWeightedPrice * payload->leverage + balances.totalLoanInQuote(price) > maxLoan) {
                return std::unexpected{OrderErrorCode::EXCEEDING_LOAN};
            }
        }
        return Result{
            .direction = payload->direction,
            .amount = volumeWeightedPrice,
            .leverage = payload->leverage
        };
    }
    else {
        if (payload->leverage == 0_dec){
            if (!baseBalance.canReserve(payload->volume)) {
                return std::unexpected{OrderErrorCode::INSUFFICIENT_BASE};
            }
        } else {
            const decimal_t price = payload->price; //book->bestBid();
            if (!balances.canBorrow(payload->volume, price, payload->direction) ||
                payload->volume * payload->leverage + balances.totalLoanInQuote(price) > maxLoan) {
                return std::unexpected{OrderErrorCode::EXCEEDING_LOAN};
            }
        }
        return Result{
            .direction = payload->direction,
            .amount = payload->volume,
            .leverage = payload->leverage
        };
    }
}

//-------------------------------------------------------------------------

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
