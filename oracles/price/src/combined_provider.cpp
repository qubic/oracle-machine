#include "combined_provider.h"
#include "binance_provider.h"
#include "mexc_provider.h"
#include "gate_provider.h"
#include "om_common/logger.h"

namespace oracle
{

// Helper function for GCD
static int64_t gcd(int64_t a, int64_t b)
{
    while (b != 0)
    {
        int64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

CombinedPriceProvider::CombinedPriceProvider(
    const std::string& name,
    std::shared_ptr<PriceProvider> provider1,
    std::shared_ptr<PriceProvider> provider2) :
    PriceProvider(name)
{
    _providers.push_back(provider1);
    _providers.push_back(provider2);

    OM_LOG_INFO() << "[" << _name << "] Combined provider initialized with: "
                  << provider1->getName() << " + " << provider2->getName();
}

uint16_t CombinedPriceProvider::getPrice(
    const std::string& currency1,
    const std::string& currency2,
    int64_t& numerator,
    int64_t& denominator)
{
    std::vector<std::pair<int64_t, int64_t>> prices;

    // Query all providers
    for (auto& provider : _providers)
    {
        int64_t num = 0, denom = 1;
        uint16_t result = provider->getPrice(currency1, currency2, num, denom);

        if (result != RETURN_NO_ERROR)
        {
            OM_LOG_ERROR() << "[" << _name << "] Provider " << provider->getName()
                          << " failed with error 0x" << std::hex << result << std::dec;
            // If ANY provider fails, propagate the error
            return result;
        }

        prices.emplace_back(num, denom);
        OM_LOG_DEBUG() << "[" << _name << "] " << provider->getName()
                       << " returned: " << num << "/" << denom;
    }

    // Compute mean of all prices
    if (prices.size() == 2)
    {
        computeMean(
            prices[0].first, prices[0].second,
            prices[1].first, prices[1].second,
            numerator, denominator);
    }
    else
    {
        // Fallback for single provider (shouldn't happen in normal usage)
        numerator = prices[0].first;
        denominator = prices[0].second;
    }

    OM_LOG_DEBUG() << "[" << _name << "] Combined price: " << numerator << "/" << denominator;

    return RETURN_NO_ERROR;
}

uint16_t CombinedPriceProvider::getPriceAtTimestamp(
    const std::string& currency1,
    const std::string& currency2,
    int64_t timestampMs,
    int64_t& numerator,
    int64_t& denominator)
{
    std::vector<std::pair<int64_t, int64_t>> prices;

    // Query all providers at the specified timestamp
    for (auto& provider : _providers)
    {
        int64_t num = 0, denom = 1;
        uint16_t result = provider->getPriceAtTimestamp(currency1, currency2, timestampMs, num, denom);

        if (result != RETURN_NO_ERROR)
        {
            OM_LOG_ERROR() << "[" << _name << "] Provider " << provider->getName()
                          << " failed with error 0x" << std::hex << result << std::dec
                          << " for timestamp " << timestampMs;
            // If ANY provider fails, propagate the error
            return result;
        }

        prices.emplace_back(num, denom);
        OM_LOG_DEBUG() << "[" << _name << "] " << provider->getName()
                       << " returned: " << num << "/" << denom << " at timestamp " << timestampMs;
    }

    // Compute mean of all prices
    if (prices.size() == 2)
    {
        computeMean(
            prices[0].first, prices[0].second,
            prices[1].first, prices[1].second,
            numerator, denominator);
    }
    else
    {
        // Fallback for single provider (shouldn't happen in normal usage)
        numerator = prices[0].first;
        denominator = prices[0].second;
    }

    OM_LOG_DEBUG() << "[" << _name << "] Combined price at timestamp " << timestampMs
                   << ": " << numerator << "/" << denominator;

    return RETURN_NO_ERROR;
}

void CombinedPriceProvider::computeMean(
    int64_t num1, int64_t denom1,
    int64_t num2, int64_t denom2,
    int64_t& resultNum, int64_t& resultDenom)
{
    // Mean of a/b and c/d = (a*d + c*b) / (2*b*d)
    // If both have the same denominator, we can simplify: mean = (num1 + num2) / 2

    if (denom1 == denom2)
    {
        // Common case: same denominator
        int64_t sum = num1 + num2;

        // Check if sum is even for exact division
        if (sum % 2 == 0)
        {
            resultNum = sum / 2;
            resultDenom = denom1;
        }
        else
        {
            // Odd sum: keep full precision
            resultNum = sum;
            resultDenom = denom1 * 2;
        }
    }
    else
    {
        // General case: different denominators
        // mean = (num1*denom2 + num2*denom1) / (2*denom1*denom2)
        // Use careful computation to avoid overflow

        // For typical crypto prices with 10^8 denominator, this should be safe
        int64_t crossNum1 = num1 * (denom2 / gcd(denom1, denom2));
        int64_t crossNum2 = num2 * (denom1 / gcd(denom1, denom2));
        int64_t lcmDenom = (denom1 / gcd(denom1, denom2)) * denom2;

        resultNum = (crossNum1 + crossNum2) / 2;
        resultDenom = lcmDenom;

        // Handle odd sum
        if ((crossNum1 + crossNum2) % 2 != 0)
        {
            resultNum = crossNum1 + crossNum2;
            resultDenom = lcmDenom * 2;
        }
    }
}

// Factory functions
std::shared_ptr<CombinedPriceProvider> createBinanceMexcProvider(
    const std::string& binanceApiKey,
    const std::string& mexcApiKey)
{
    return std::make_shared<CombinedPriceProvider>(
        "Binance+MEXC",
        std::make_shared<BinancePriceProvider>(binanceApiKey),
        std::make_shared<MexcPriceProvider>(mexcApiKey));
}

std::shared_ptr<CombinedPriceProvider> createBinanceGateProvider(
    const std::string& binanceApiKey,
    const std::string& gateApiKey)
{
    return std::make_shared<CombinedPriceProvider>(
        "Binance+Gate",
        std::make_shared<BinancePriceProvider>(binanceApiKey),
        std::make_shared<GatePriceProvider>(gateApiKey));
}

std::shared_ptr<CombinedPriceProvider> createMexcGateProvider(
    const std::string& mexcApiKey,
    const std::string& gateApiKey)
{
    return std::make_shared<CombinedPriceProvider>(
        "MEXC+Gate",
        std::make_shared<MexcPriceProvider>(mexcApiKey),
        std::make_shared<GatePriceProvider>(gateApiKey));
}

} // namespace oracle
