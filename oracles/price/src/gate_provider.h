#pragma once

#include "exchange_provider.h"

namespace oracle
{

/**
 * Gate.io exchange price provider.
 * API: https://api.gateio.ws/api/v4/spot/candlesticks
 * Symbol format: BTC_USDT (uppercase with underscore)
 */
class GatePriceProvider : public ExchangePriceProvider
{
public:
    explicit GatePriceProvider(const std::string& apiKey = "");

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

    std::string getApiKeyHeader() const override;

private:
    static constexpr const char* BASE_URL = "https://api.gateio.ws";
    static constexpr double RATE_LIMIT_FREE = 1.0;   // 1 req/sec for free tier
    static constexpr double RATE_LIMIT_PAID = 0.2;   // 5 req/sec for paid tier
};

} // namespace oracle
