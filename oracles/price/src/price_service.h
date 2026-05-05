#pragma once

#include "base_oracle_service.h"
#include "om_common/config.h"
#include "om_common/qpi_adapter.h"

#define ORACLE_INTERFACE_INDEX PRICE_ORACLE_INTERFACE_INDEX
#include "oracle_interfaces/Price.h"
#undef ORACLE_INTERFACE_INDEX

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace oracle
{

// Price Interface Constants
constexpr size_t PRICE_ORACLE_QUERY_SIZE = sizeof(Price::OracleQuery); // 32 + 8 + 32 + 32
constexpr size_t PRICE_ORACLE_REPLY_SIZE = sizeof(Price::OracleReply); // 8 + 8

bool priceStringToRational(const std::string& priceStr, int64_t& numerator, int64_t& denominator);

static std::string getTimeStampString(const QPI::DateAndTime& rQpiDateTime);

// Price Provider Interface
class PriceProvider
{
public:
    PriceProvider(const std::string& name) : _name(name) {}
    virtual ~PriceProvider() = default;

    /**
     * Get current price for currency1/currency2.
     *
     * @param currency1 Base currency (e.g., "BTC")
     * @param currency2 Quote currency (e.g., "USD")
     * @param numerator Output: price numerator
     * @param denominator Output: price denominator
     * @return RETURN_NO_ERROR on success, another value of OracleErrorFlags otherwise.
     */
    virtual uint16_t getPrice(
        const std::string& currency1,
        const std::string& currency2,
        int64_t& numerator,
        int64_t& denominator) = 0;

    /**
     * Get price for currency1/currency2 at a specific UTC timestamp.
     * Default implementation returns current price if timestampMs=0,
     * otherwise returns RETURN_ERROR_INVALID_ARG (historical not supported).
     *
     * @param currency1 Base currency (e.g., "BTC")
     * @param currency2 Quote currency (e.g., "USD")
     * @param timestampMs UTC timestamp in milliseconds since epoch (0 = current price)
     * @param numerator Output: price numerator
     * @param denominator Output: price denominator
     * @return RETURN_NO_ERROR on success, another value of OracleErrorFlags otherwise.
     */
    virtual uint16_t getPriceAtTimestamp(
        const std::string& currency1,
        const std::string& currency2,
        int64_t timestampMs,
        int64_t& numerator,
        int64_t& denominator)
    {
        // timestampMs = 0 means get current price
        if (timestampMs == 0)
        {
            return getPrice(currency1, currency2, numerator, denominator);
        }
        // Default: historical prices not supported
        return RETURN_ERROR_INVALID_ARG;
    }

    const std::string& getName() const { return _name; }

protected:
    std::string _name;
};

// Mock Price Provider (for testing)
class MockPriceProvider : public PriceProvider
{
public:
    MockPriceProvider();

    uint16_t getPrice(
        const std::string& currency1,
        const std::string& currency2,
        int64_t& numerator,
        int64_t& denominator) override;

    // Set mock price
    void setPrice(const std::string& pair, int64_t num, int64_t denom);

private:
    std::map<std::string, std::pair<int64_t, int64_t>> _prices;
    std::mutex _mutex;
};

// Specific price providers are implemented in separate h/cpp files

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