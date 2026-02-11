#pragma once

#include "exchange_provider.h"

namespace oracle
{

/**
 * Binance exchange price provider.
 * API: https://api.binance.com/api/v3/klines
 * Symbol format: BTCUSDT (uppercase, no separator)
 */
class BinancePriceProvider : public ExchangePriceProvider
{
public:
    explicit BinancePriceProvider(const std::string& apiKey = "");

protected:
    std::string buildKlineUrl(
        const std::string& symbol,
        int64_t startTimeMs,
        int64_t endTimeMs,
        int limit) override;

    std::string formatSymbol(
        const std::string& base,
        const std::string& quote) override;

    uint16_t parseKlineResponse(
        const std::string& response,
        std::string& closePriceString) override;

private:
    static constexpr const char* BASE_URL = "https://api.binance.com";
    static constexpr double RATE_LIMIT_FREE = 0.5;   // 2 req/sec for free tier
    static constexpr double RATE_LIMIT_PAID = 0.1;   // 10 req/sec for paid tier
};

} // namespace oracle
