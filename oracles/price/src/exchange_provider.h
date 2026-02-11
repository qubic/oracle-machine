#pragma once

#include "price_service.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace oracle
{

/**
 * Abstract base class for exchange-based price providers.
 * Provides common HTTP request handling, caching, rate limiting, and timestamp-based price lookup.
 */
class ExchangePriceProvider : public PriceProvider
{
public:
    ExchangePriceProvider(
        const std::string& name,
        const std::string& baseUrl,
        const std::string& apiKey = "",
        double rateLimitDelayFree = 0.5,
        double rateLimitDelayPaid = 0.1,
        int cacheTtl = 60);

    virtual ~ExchangePriceProvider() = default;

    /**
     * Get current price (uses latest completed 1-minute candle).
     */
    uint16_t getPrice(
        const std::string& currency1,
        const std::string& currency2,
        int64_t& numerator,
        int64_t& denominator) override;

    /**
     * Get price at a specific timestamp (previous finished minute candle).
     * @param currency1 Base currency (e.g., "BTC")
     * @param currency2 Quote currency (e.g., "USDT", "USD", "USDC")
     * @param timestampMs Unix timestamp in milliseconds
     * @param numerator Output: price numerator
     * @param denominator Output: price denominator
     * @return RETURN_NO_ERROR on success
     */
    virtual uint16_t getPriceAtTimestamp(
        const std::string& currency1,
        const std::string& currency2,
        int64_t timestampMs,
        int64_t& numerator,
        int64_t& denominator);

protected:
    // Must be implemented by each exchange provider

    /**
     * Build the kline/candlestick API URL.
     * @param symbol Trading symbol in exchange format (e.g., "BTCUSDT" or "BTC_USDT")
     * @param startTimeMs Start time in milliseconds
     * @param endTimeMs End time in milliseconds
     * @param limit Number of candles to fetch
     * @return Full API URL
     */
    virtual std::string buildKlineUrl(
        const std::string& symbol,
        int64_t startTimeMs,
        int64_t endTimeMs,
        int limit = 1) = 0;

    /**
     * Format the trading symbol for this exchange.
     * @param base Base currency (e.g., "BTC")
     * @param quote Quote currency (e.g., "USDT")
     * @return Symbol in exchange format (e.g., "BTCUSDT" or "BTC_USDT")
     */
    virtual std::string formatSymbol(
        const std::string& base,
        const std::string& quote) = 0;

    /**
     * Parse the kline response and extract the close price.
     * @param response Raw API response
     * @param closePrice Output: extracted close price
     * @return RETURN_NO_ERROR on success
     */
    virtual uint16_t parseKlineResponse(
        const std::string& response,
        std::string& closePriceString) = 0;

    // Common HTTP functionality
    uint16_t httpGet(const std::string& url, std::string& response);

    // Rate limiting
    void enforceRateLimit();

    // Cache structure with timestamp support
    struct CacheEntry
    {
        int64_t numerator;
        int64_t denominator;
        int64_t candleTimestampMs;
        std::chrono::steady_clock::time_point fetchTime;
    };

    std::string _apiKey;
    std::string _apiBaseUrl;
    double _rateLimitDelay;
    int _cacheTtl;

    std::map<std::string, CacheEntry> _cache;  // key: "BTC/USDT@timestamp"
    std::mutex _cacheMutex;
    std::mutex _rateLimitMutex;
    std::chrono::steady_clock::time_point _lastRequestTime;
};

} // namespace oracle
