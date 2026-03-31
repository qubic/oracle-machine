#include "mexc_provider.h"
#include "om_common/logger.h"

#include <algorithm>
#include <sstream>

namespace oracle
{

MexcPriceProvider::MexcPriceProvider(const std::string& apiKey) :
    ExchangePriceProvider(
        "MEXC",
        BASE_URL,
        apiKey,
        RATE_LIMIT_FREE,
        RATE_LIMIT_PAID)
{
}

std::string MexcPriceProvider::buildKlineUrl(
    const std::string& symbol,
    int64_t startTimeMs,
    int64_t endTimeMs,
    int limit)
{
    std::ostringstream url;
    url << _apiBaseUrl << "/api/v3/klines"
        << "?symbol=" << symbol
        << "&interval=1m"
        << "&startTime=" << startTimeMs
        << "&endTime=" << endTimeMs
        << "&limit=" << limit;
    return url.str();
}

std::string MexcPriceProvider::formatSymbol(
    const std::string& base,
    const std::string& quote)
{
    // MEXC format: BTCUSDT (uppercase, no separator) - same as Binance
    std::string symbol = base + quote;
    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
    return symbol;
}

uint16_t MexcPriceProvider::parseKlineResponse(
    const std::string& response,
    std::string& closePriceString)
{
    // MEXC uses same kline format as Binance
    // [[openTime, open, high, low, close, volume, closeTime, ...], ...]
    // Close price is at index 4 (5th element)

    // Check for empty response or empty array
    if (response.empty() || response == "[]")
    {
        OM_LOG_ERROR() << "[MEXC] Empty response - pair may not be supported";
        return RETURN_ERROR_INVALID_ARG;
    }

    // Find the start of the inner array
    size_t startPos = response.find('[');
    if (startPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[MEXC] Invalid response format: " << response;
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Skip to inner array
    startPos = response.find('[', startPos + 1);
    if (startPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[MEXC] No kline data in response: " << response;
        return RETURN_ERROR_INVALID_ARG;
    }

    // Find close price (5th comma-separated value, index 4)
    int commaCount = 0;
    size_t pos = startPos + 1;
    while (commaCount < 4 && pos < response.size())
    {
        if (response[pos] == ',')
        {
            commaCount++;
        }
        pos++;
    }

    if (commaCount != 4)
    {
        OM_LOG_ERROR() << "[MEXC] Could not find close price in response";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Extract close price (quoted string)
    size_t quoteStart = response.find('"', pos);
    size_t quoteEnd = response.find('"', quoteStart + 1);

    if (quoteStart == std::string::npos || quoteEnd == std::string::npos)
    {
        OM_LOG_ERROR() << "[MEXC] Could not parse close price";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    closePriceString = response.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    return RETURN_NO_ERROR;
}

std::string MexcPriceProvider::getApiKeyHeader() const
{
    if (_apiKey.empty())
    {
        return "";
    }
    return "X-MEXC-APIKEY: " + _apiKey;
}

} // namespace oracle
