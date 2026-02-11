#pragma once

#include "price_service.h"
#include "exchange_provider.h"

#include <memory>
#include <vector>

namespace oracle
{

/**
 * Combined price provider that aggregates prices from multiple sources.
 * Returns the arithmetic mean of prices from all configured providers.
 * Returns error if ANY source is unavailable or returns an error.
 */
class CombinedPriceProvider : public PriceProvider
{
public:
    CombinedPriceProvider(
        const std::string& name,
        std::shared_ptr<PriceProvider> provider1,
        std::shared_ptr<PriceProvider> provider2);

    uint16_t getPrice(
        const std::string& currency1,
        const std::string& currency2,
        int64_t& numerator,
        int64_t& denominator) override;

    uint16_t getPriceAtTimestamp(
        const std::string& currency1,
        const std::string& currency2,
        int64_t timestampMs,
        int64_t& numerator,
        int64_t& denominator) override;

private:
    std::vector<std::shared_ptr<PriceProvider>> _providers;

    /**
     * Compute mean of two prices.
     */
    static void computeMean(
        int64_t num1, int64_t denom1,
        int64_t num2, int64_t denom2,
        int64_t& resultNum, int64_t& resultDenom);
};

// Convenience factory functions for creating combined providers
std::shared_ptr<CombinedPriceProvider> createBinanceMexcProvider(
    const std::string& binanceApiKey = "",
    const std::string& mexcApiKey = "");

std::shared_ptr<CombinedPriceProvider> createBinanceGateProvider(
    const std::string& binanceApiKey = "",
    const std::string& gateApiKey = "");

std::shared_ptr<CombinedPriceProvider> createMexcGateProvider(
    const std::string& mexcApiKey = "",
    const std::string& gateApiKey = "");

} // namespace oracle
