#include "gate_provider.h"
#include "om_common/logger.h"

#include <algorithm>
#include <sstream>

namespace oracle
{

GatePriceProvider::GatePriceProvider(const std::string& apiKey) :
    ExchangePriceProvider(
        "Gate",
        BASE_URL,
        apiKey,
        RATE_LIMIT_FREE,
        RATE_LIMIT_PAID)
{
}

std::string GatePriceProvider::buildKlineUrl(
    const std::string& symbol,
    int64_t startTimeMs,
    int64_t endTimeMs,
    int limit)
{
    // Gate.io uses seconds, not milliseconds
    int64_t startTimeSec = startTimeMs / 1000;
    int64_t endTimeSec = endTimeMs / 1000;

    std::ostringstream url;
    url << _apiBaseUrl << "/api/v4/spot/candlesticks"
        << "?currency_pair=" << symbol
        << "&interval=1m"
        << "&from=" << startTimeSec
        << "&to=" << endTimeSec
        << "&limit=" << limit;
    return url.str();
}

std::string GatePriceProvider::formatSymbol(
    const std::string& base,
    const std::string& quote)
{
    // Gate.io format: BTC_USDT (uppercase with underscore)
    std::string symbol = base + "_" + quote;
    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
    return symbol;
}

uint16_t GatePriceProvider::parseKlineResponse(
    const std::string& response,
    std::string& closePriceString)
{
    // Gate.io candlestick response format:
    // [["timestamp", "volume", "close", "high", "low", "open", "is_final"], ...]
    // Close price is at index 2 (3rd element)

    // Check for empty response or empty array
    if (response.empty() || response == "[]")
    {
        OM_LOG_ERROR() << "[Gate] Empty response - pair may not be supported";
        return RETURN_ERROR_INVALID_ARG;
    }

    // Find the start of the inner array
    size_t startPos = response.find('[');
    if (startPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[Gate] Invalid response format: " << response;
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Skip to inner array
    startPos = response.find('[', startPos + 1);
    if (startPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[Gate] No kline data in response: " << response;
        return RETURN_ERROR_INVALID_ARG;
    }

    // Find close price (3rd comma-separated value, index 2)
    int commaCount = 0;
    size_t pos = startPos + 1;
    while (commaCount < 2 && pos < response.size())
    {
        if (response[pos] == ',')
        {
            commaCount++;
        }
        pos++;
    }

    if (commaCount != 2)
    {
        OM_LOG_ERROR() << "[Gate] Could not find close price in response";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Extract close price (quoted string)
    size_t quoteStart = response.find('"', pos);
    size_t quoteEnd = response.find('"', quoteStart + 1);

    if (quoteStart == std::string::npos || quoteEnd == std::string::npos)
    {
        OM_LOG_ERROR() << "[Gate] Could not parse close price";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    closePriceString = response.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    return RETURN_NO_ERROR;
}

std::string GatePriceProvider::getApiKeyHeader() const
{
    if (_apiKey.empty())
    {
        return "";
    }
    return "KEY: " + _apiKey;
}

} // namespace oracle
