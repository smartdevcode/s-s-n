#include "taosim/exchange/ExchangeConfig.hpp"

#include <fmt/format.h>

//-------------------------------------------------------------------------

namespace taosim::exchange
{

ExchangeConfig makeExchangeConfig(pugi::xml_node node)
{
    using namespace literals;
    using accounting::validateDecimalPlaces;

    static constexpr auto sl = std::source_location::current();

    const auto priceDecimals = validateDecimalPlaces(node.attribute("priceDecimals").as_uint(), sl);

    // TODO: Validation for leverage.
    const auto maxLeverage = decimal_t{node.attribute("maxLeverage").as_double()};
    const auto maintenanceMargin = [&] {
        const decimal_t maintenanceMargin{node.attribute("maintenanceMargin").as_double()};
        const decimal_t maxAllowedMaintenance = 1_dec / (2_dec * util::dec1p(maxLeverage));
        if (maintenanceMargin > maxAllowedMaintenance){
            throw std::invalid_argument{fmt::format(
                "{}: 'maintenanceMargin' {} cannot be less than {} when maxLeverage is {}",
                sl.function_name(),
                maintenanceMargin,
                maxAllowedMaintenance,
                maxLeverage)};
        }
        return maintenanceMargin;
    }();

    return {
        .priceDecimals = priceDecimals,
        .volumeDecimals = validateDecimalPlaces(node.attribute("volumeDecimals").as_uint(), sl),
        .baseDecimals = validateDecimalPlaces(node.attribute("baseDecimals").as_uint(), sl),
        .quoteDecimals = validateDecimalPlaces(node.attribute("quoteDecimals").as_uint(), sl),
        .maxLeverage = maxLeverage,
        // TODO: Validation for loan.
        .maxLoan = decimal_t{node.attribute("maxLoan").as_double()},
        .maintenanceMargin = maintenanceMargin,
        // TODO: Validation for price.
        .initialPrice = decimal_t{node.attribute("initialPrice").as_double()}
    };
}

}  // namespace taosim::exchange

//-------------------------------------------------------------------------
