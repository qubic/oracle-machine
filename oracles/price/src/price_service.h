#pragma once

#include "base_oracle_service.h"

#include "om_common/qpi_adapter.h"
#include "oracle_interfaces/Price.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace oracle
{

// Price Interface Constants
constexpr size_t PRICE_ORACLE_QUERY_SIZE = sizeof(Price::OracleQuery); // 32 + 8 + 32 + 32
constexpr size_t PRICE_ORACLE_REPLY_SIZE = sizeof(Price::OracleReply); // 8 + 8

static std::string getTimeStampString(const QPI::DateAndTime& rQpiDateTime); 

// Price Provider Interface
class PriceProvider
{
public:
    PriceProvider(const std::string& name) : _name(name) {}
    virtual ~PriceProvider() = default;

    /**
     * Get price for currency1/currency2.
     *
     * @param currency1 Base currency (e.g., "BTC")
     * @param currency2 Quote currency (e.g., "USD")
     * @param timeStamp Unix timestamp of querry
     * @param numerator Output: price numerator
     * @param denominator Output: price denominator
     * @return true if price available, false otherwise
     */
    virtual bool getPrice(
        const std::string& currency1,
        const std::string& currency2,
        uint64_t timeStamp,
        int64_t& numerator,
        int64_t& denominator) = 0;

    const std::string& getName() const { return _name; }

protected:
    std::string _name;
};

// Mock Price Provider (for testing)
class MockPriceProvider : public PriceProvider
{
public:
    MockPriceProvider();

    bool getPrice(
        const std::string& currency1,
        const std::string& currency2,
        uint64_t timeStamp,
        int64_t& numerator,
        int64_t& denominator) override;

    // Set mock price
    void setPrice(const std::string& pair, int64_t num, int64_t denom);

private:
    std::map<std::string, std::pair<int64_t, int64_t>> _prices;
    std::mutex _mutex;
};

// Specific price provider.
// Depend on each provider API implementation, we will have different derived class here.

// CoinGecko Price Provider
class CoinGeckoPriceProvider : public PriceProvider
{
public:
    CoinGeckoPriceProvider(const std::string& apiKey = "", const std::string& apiType = "free");

    /**
     * Price fetch - automatically selects best endpoint based on timestamp age.
     * 
     * Accuracy levels:
     * - < 1 day old:     5-minute granularity (market_chart/range)
     * - 1-90 days old:   1-hour granularity (market_chart/range)
     * - > 90 days old:   1-day granularity (history)
     * 
     * @param currency1 Base currency (e.g., "BTC")
     * @param currency2 Quote currency (e.g., "USD")
     * @param timestamp Unix timestamp in seconds
     * @param numerator Output: price numerator
     * @param denominator Output: price denominator
     * @return true if price fetched successfully
     */
    bool getPrice(
        const std::string& currency1,
        const std::string& currency2,
        uint64_t timeStamp,
        int64_t& numerator,
        int64_t& denominator) override;

private:
    // TODO: move this to base class
    struct CacheEntry
    {
        int64_t numerator;
        int64_t denominator;
        uint64_t queryTimestamp;
        time_t fetchTime;
    };

    std::string _apiKey;
    std::string _apiType;
    std::map<std::string, std::string> _coinMap;
    std::map<std::string, CacheEntry> _cache;
    std::mutex _cacheMutex;
    std::mutex _rateLimitMutex;
    time_t _lastRequestTime;

    static constexpr int CACHE_TTL = 60;            // 60 seconds
    static constexpr double RATE_LIMIT_DELAY = 2.0; // 2 seconds

    std::string getCoinId(const std::string& currency);
    std::string getCacheKey(const std::string& pair, uint64_t timestamp) const;
    // Static helper to determine cache TTL based on timestamp age
    int getCacheTTL(uint64_t timestamp) const;

    // API fetch methods
    bool fetchCurrentPrice(
        const std::string& currency1,
        const std::string& currency2,
        int64_t& numerator,
        int64_t& denominator);
    
    bool fetchPriceRange(
        const std::string& currency1,
        const std::string& currency2,
        uint64_t targetTimestamp,
        int64_t& numerator,
        int64_t& denominator,
        uint64_t windowSeconds);
    
    bool fetchPriceHistory(
        const std::string& currency1,
        const std::string& currency2,
        uint64_t timestamp,
        int64_t& numerator,
        int64_t& denominator);

        bool httpGet(const std::string& url, std::string& response);
    void applyRateLimit();
    
    // Response parsers
    bool parseSimplePriceResponse(
        const std::string& response,
        const std::string& vsCurrency,
        int64_t& numerator,
        int64_t& denominator);
    
    bool parseRangeResponse(
        const std::string& response,
        uint64_t targetTimestamp,
        const std::string& vsCurrency,
        int64_t& numerator,
        int64_t& denominator);
    
    bool parseHistoryResponse(
        const std::string& response,
        const std::string& vsCurrency,
        int64_t& numerator,
        int64_t& denominator);
};

// Price Service
// This class implement the logic handle connection and using the price provider
class PriceService : public BaseOracleService
{
public:
    PriceService(const std::string& rHostname, uint16_t hostPort);
    ~PriceService() override;

    // Register a price provider
    void registerProvider(const std::string& oracleId, std::shared_ptr<PriceProvider> provider);

protected:
    // Implement BaseOracleService abstract methods
    uint16_t processInterfaceQuery(
        const std::vector<uint8_t>& queryPayload,
        std::vector<uint8_t>& replyPayload) override;

private:
    std::map<std::string, std::shared_ptr<PriceProvider>> _providers;
    std::mutex _providersMutex;

    // Helper: convert bytes to null-terminated string
    static std::string bytesToString(const char* data, size_t maxLen);
};

} // namespace oracle