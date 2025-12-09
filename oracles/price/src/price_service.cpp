#include "price_service.h"
#include "om_common/logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <thread>

// For HTTP requests (using libcurl)
#include <curl/curl.h>

namespace oracle
{

std::string PriceService::bytesToString(const char* data, size_t maxLen)
{
    // Find null terminator or end of buffer
    size_t len = 0;
    while (len < maxLen && data[len] != '\0')
    {
        len++;
    }
    return std::string(data, len);
}

uint64_t timestampToUnixSeconds(const QPI::DateAndTime& timestamp)
{
    // Basic validation
    if (!timestamp.isValid())
    {
        return 0;
    }

    uint16_t year = timestamp.getYear();
    uint8_t month = timestamp.getMonth();
    uint8_t day = timestamp.getDay();
    uint8_t hour = timestamp.getHour();
    uint8_t minute = timestamp.getMinute();
    uint8_t second = timestamp.getSecond();
    
    
    // Build std::tm structure
    std::tm timeinfo = {};
    timeinfo.tm_year = year - 1900;  // tm_year is years since 1900
    timeinfo.tm_mon = month - 1;     // tm_mon is 0-11
    timeinfo.tm_mday = day;          // tm_mday is 1-31
    timeinfo.tm_hour = hour;         // tm_hour is 0-23
    timeinfo.tm_min = minute;        // tm_min is 0-59
    timeinfo.tm_sec = second;        // tm_sec is 0-59
    timeinfo.tm_isdst = 0;           // No daylight saving time
    
    // Convert to Unix timestamp (UTC)
#ifdef _WIN32
    time_t unixTime = _mkgmtime(&timeinfo);
#else
    time_t unixTime = timegm(&timeinfo);
#endif
    
    if (unixTime == -1)
    {
        return 0;
    }
    
    return static_cast<uint64_t>(unixTime);
}

static std::string getTimeStampString(const QPI::DateAndTime& rQpiDateTime)
{
    std::ostringstream ss;
    ss << rQpiDateTime.getYear();
    ss << rQpiDateTime.getMonth();
    ss << rQpiDateTime.getDay();
    ss << rQpiDateTime.getHour();
    ss << rQpiDateTime.getMinute();
    ss << rQpiDateTime.getSecond();

    return ss.str();
}

// ============================================================================
// Mock Price Provider

MockPriceProvider::MockPriceProvider() : PriceProvider("MockProvider")
{
    // Set some default mock prices
    setPrice("BTC/USD", 45000, 1);
    setPrice("ETH/USD", 3000, 1);
    setPrice("BTC/ETH", 15, 1);
    setPrice("ETH/BTC", 1, 15);

    OM_LOG_INFO() << "[" << _name << "] Initialized with default mock prices";
}

bool MockPriceProvider::getPrice(
    const std::string& currency1,
    const std::string& currency2,
    uint64_t timestamp,
    int64_t& numerator,
    int64_t& denominator)
{
    std::string pair = currency1 + "/" + currency2;

    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _prices.find(pair);
    if (it != _prices.end())
    {
        numerator = it->second.first;
        denominator = it->second.second;
        OM_LOG_DEBUG() << "[" << _name << "] Price found: " << pair << " = " << numerator << "/"
                       << denominator;
        return true;
    }

    OM_LOG_ERROR() << "[" << _name << "] Price not found: " << pair;
    return false;
}

void MockPriceProvider::setPrice(const std::string& pair, int64_t num, int64_t denom)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _prices[pair] = std::make_pair(num, denom);
    OM_LOG_DEBUG() << "[" << _name << "] Set price: " << pair << " = " << num << "/" << denom;
}

// ============================================================================
// CoinGecko Price Provider

CoinGeckoPriceProvider::CoinGeckoPriceProvider(
    const std::string& apiKey,
    const std::string& apiType) :
    PriceProvider("CoinGecko"), _apiKey(apiKey), _apiType(apiType), _lastRequestTime(0)
{
    // Initialize coin mappings
    // TODO: Move this map to generic/config file ?
    _coinMap["BTC"] = "bitcoin";
    _coinMap["ETH"] = "ethereum";
    _coinMap["USDT"] = "tether";
    _coinMap["BNB"] = "binancecoin";
    _coinMap["USDC"] = "usd-coin";
    _coinMap["XRP"] = "ripple";
    _coinMap["ADA"] = "cardano";
    _coinMap["SOL"] = "solana";
    _coinMap["DOGE"] = "dogecoin";

    if (!_apiKey.empty())
    {
        OM_LOG_INFO() << "[" << _name << "] Configured with API key: " << " (type: " << _apiType
                      << ")";
    }
    else
    {
        OM_LOG_INFO() << "[" << _name << "] Using free tier (no API key)";
    }
}

std::string CoinGeckoPriceProvider::getCoinId(const std::string& currency)
{
    // Convert to uppercase for comparison
    std::string upper = currency;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    auto it = _coinMap.find(upper);
    if (it != _coinMap.end())
    {
        return it->second;
    }
    return "";
}

// TODO: improve the cache key to include timestamp bucket
std::string CoinGeckoPriceProvider::getCacheKey(const std::string& pair, uint64_t timestamp) const
{
    uint64_t bucket = timestamp;

    std::ostringstream oss;
    oss << pair << ":" << bucket;
    return oss.str();
}

int CoinGeckoPriceProvider::getCacheTTL(uint64_t timestamp) const
{
    if (timestamp == 0)
    {
        return 60; // Current price: 60 seconds TTL
    }

    time_t now = time(nullptr);
    int64_t ageSeconds = now - static_cast<time_t>(timestamp);

    if (ageSeconds < 86400)        // < 1 day
        return 300;                // 5 minutes TTL
    else if (ageSeconds < 7776000) // < 90 days
        return 3600;               // 1 hour TTL
    else
        return 86400; // 24 hours TTL (historical data doesn't change)
}

bool CoinGeckoPriceProvider::getPrice(
    const std::string& currency1,
    const std::string& currency2,
    uint64_t timestamp,
    int64_t& numerator,
    int64_t& denominator)
{
    std::string pair = currency1 + "/" + currency2;

    // Validate timestamp
    if (timestamp != 0) // 0 means current price
    {
        time_t now = time(nullptr);
        int64_t ageSeconds = now - static_cast<time_t>(timestamp);

        if (ageSeconds < 0)
        {
            OM_LOG_ERROR() << "[" << _name << "] Timestamp is in the future!";
            return false;
        }
    }

    // Check cache
    std::string cacheKey = getCacheKey(pair, timestamp);
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _cache.find(cacheKey);
        if (it != _cache.end())
        {
            time_t now = time(nullptr);
            int ttl = getCacheTTL(timestamp);

            if (now - it->second.fetchTime < ttl)
            {
                numerator = it->second.numerator;
                denominator = it->second.denominator;
                OM_LOG_DEBUG() << "[" << _name << "] Cache hit: " << pair << " at t=" << timestamp
                               << " = " << numerator << "/" << denominator;
                return true;
            }
        }
    }

    // Cache miss - fetch from API
    OM_LOG_DEBUG() << "[" << _name << "] Cache miss - fetching " << pair
                   << " at timestamp=" << timestamp;

    bool success = false;

    // Determine which API endpoint to use based on timestamp
    if (timestamp == 0)
    {
        // Current price
        OM_LOG_INFO() << "[" << _name << "] Fetching current price";
        success = fetchCurrentPrice(currency1, currency2, numerator, denominator);
    }
    else
    {
        time_t now = time(nullptr);
        int64_t ageSeconds = now - static_cast<time_t>(timestamp);

        if (ageSeconds < 86400) // < 1 day
        {
            OM_LOG_INFO() << "[" << _name
                          << "] Using 5-minute granularity (age: " << (ageSeconds / 60)
                          << " minutes)";
            success = fetchPriceRange(currency1, currency2, timestamp, numerator, denominator, 300);
        }
        else if (ageSeconds < 7776000) // < 90 days
        {
            OM_LOG_INFO() << "[" << _name
                          << "] Using 1-hour granularity (age: " << (ageSeconds / 86400)
                          << " days)";
            success =
                fetchPriceRange(currency1, currency2, timestamp, numerator, denominator, 3600);
        }
        else
        {
            OM_LOG_INFO() << "[" << _name
                          << "] Using daily granularity (age: " << (ageSeconds / 86400) << " days)";
            success = fetchPriceHistory(currency1, currency2, timestamp, numerator, denominator);
        }
    }

    if (success)
    {
        // Update cache
        CacheEntry entry;
        entry.numerator = numerator;
        entry.denominator = denominator;
        entry.queryTimestamp = timestamp;
        entry.fetchTime = time(nullptr);

        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cache[cacheKey] = entry;
    }

    return success;
}

bool CoinGeckoPriceProvider::fetchCurrentPrice(
    const std::string& currency1,
    const std::string& currency2,
    int64_t& numerator,
    int64_t& denominator)
{
    std::string coinId = getCoinId(currency1);
    if (coinId.empty())
    {
        OM_LOG_ERROR() << "[" << _name << "] Unknown currency: " << currency1;
        return false;
    }

    std::string vsCurrency = currency2;
    std::transform(vsCurrency.begin(), vsCurrency.end(), vsCurrency.begin(), ::tolower);

    // Map stablecoins to USD
    if (vsCurrency == "usdt" || vsCurrency == "usdc")
    {
        vsCurrency = "usd";
    }

    // Build URL for current price
    std::string url = "https://api.coingecko.com/api/v3/simple/price?ids=" + coinId +
                      "&vs_currencies=" + vsCurrency;

    OM_LOG_DEBUG() << "[" << _name << "] Fetching current: " << url;

    applyRateLimit();

    std::string response;
    if (!httpGet(url, response))
    {
        return false;
    }

    return parseSimplePriceResponse(response, vsCurrency, numerator, denominator);
}

bool CoinGeckoPriceProvider::fetchPriceRange(
    const std::string& currency1,
    const std::string& currency2,
    uint64_t targetTimestamp,
    int64_t& numerator,
    int64_t& denominator,
    uint64_t windowSeconds)
{
    std::string coinId = getCoinId(currency1);
    if (coinId.empty())
    {
        OM_LOG_ERROR() << "[" << _name << "] Unknown currency: " << currency1;
        return false;
    }

    std::string vsCurrency = currency2;
    std::transform(vsCurrency.begin(), vsCurrency.end(), vsCurrency.begin(), ::tolower);

    if (vsCurrency == "usdt" || vsCurrency == "usdc")
    {
        vsCurrency = "usd";
    }

    // Create time range around target
    uint64_t fromTimestamp = targetTimestamp - windowSeconds;
    uint64_t toTimestamp = targetTimestamp + windowSeconds;

    // Build URL
    std::string url = "https://api.coingecko.com/api/v3/coins/" + coinId +
                      "/market_chart/range?vs_currency=" + vsCurrency +
                      "&from=" + std::to_string(fromTimestamp) +
                      "&to=" + std::to_string(toTimestamp);

    OM_LOG_DEBUG() << "[" << _name << "] Fetching range: " << url;

    applyRateLimit();

    std::string response;
    if (!httpGet(url, response))
    {
        return false;
    }

    return parseRangeResponse(response, targetTimestamp, vsCurrency, numerator, denominator);
}

bool CoinGeckoPriceProvider::fetchPriceHistory(
    const std::string& currency1,
    const std::string& currency2,
    uint64_t timestamp,
    int64_t& numerator,
    int64_t& denominator)
{
    std::string coinId = getCoinId(currency1);
    if (coinId.empty())
    {
        OM_LOG_ERROR() << "[" << _name << "] Unknown currency: " << currency1;
        return false;
    }

    std::string vsCurrency = currency2;
    std::transform(vsCurrency.begin(), vsCurrency.end(), vsCurrency.begin(), ::tolower);

    if (vsCurrency == "usdt" || vsCurrency == "usdc")
    {
        vsCurrency = "usd";
    }

    // Convert timestamp to date string (DD-MM-YYYY)
    time_t time = static_cast<time_t>(timestamp);
    std::tm tm;

#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    char dateStr[16];
    std::strftime(dateStr, sizeof(dateStr), "%d-%m-%Y", &tm);

    // Build URL
    std::string url = "https://api.coingecko.com/api/v3/coins/" + coinId +
                      "/history?date=" + dateStr + "&localization=false";

    OM_LOG_DEBUG() << "[" << _name << "] Fetching history: " << url;

    applyRateLimit();

    std::string response;
    if (!httpGet(url, response))
    {
        return false;
    }

    return parseHistoryResponse(response, vsCurrency, numerator, denominator);
}

bool CoinGeckoPriceProvider::httpGet(const std::string& url, std::string& response)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to initialize curl";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            std::string* str = static_cast<std::string*>(userdata);
            str->append(static_cast<char*>(ptr), size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Add API key if available
    struct curl_slist* headers = nullptr;
    if (!_apiKey.empty())
    {
        std::string headerName = (_apiType == "demo") ? "x-cg-demo-api-key" : "x-cg-pro-api-key";
        std::string headerValue = headerName + ": " + _apiKey;
        headers = curl_slist_append(headers, headerValue.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    if (headers)
    {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK)
    {
        OM_LOG_ERROR() << "[" << _name << "] Curl error: " << curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return false;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (httpCode != 200)
    {
        OM_LOG_ERROR() << "[" << _name << "] HTTP error: " << httpCode;
        OM_LOG_ERROR() << "  Response: " << response;
        return false;
    }

    return true;
}

void CoinGeckoPriceProvider::applyRateLimit()
{
    std::lock_guard<std::mutex> lock(_rateLimitMutex);

    time_t now = time(nullptr);
    double elapsed = difftime(now, _lastRequestTime);

    if (elapsed < RATE_LIMIT_DELAY)
    {
        double sleepTime = RATE_LIMIT_DELAY - elapsed;
        OM_LOG_DEBUG() << "[" << _name << "] Rate limit: sleeping " << sleepTime << "s";

        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(sleepTime * 1000)));
    }

    _lastRequestTime = time(nullptr);
}

bool CoinGeckoPriceProvider::parseSimplePriceResponse(
    const std::string& response,
    const std::string& vsCurrency,
    int64_t& numerator,
    int64_t& denominator)
{
    // Response format: {"bitcoin":{"usd":45000.5}}

    std::string searchKey = "\"" + vsCurrency + "\":";
    size_t pos = response.find(searchKey);
    if (pos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Price not found in response";
        return false;
    }

    pos += searchKey.length();
    size_t endPos = response.find_first_of("},", pos);
    std::string priceStr = response.substr(pos, endPos - pos);

    try
    {
        double price = std::stod(priceStr);

        numerator = static_cast<int64_t>(price * 1000000);
        denominator = 1000000;

        OM_LOG_DEBUG() << "[" << _name << "] Current price: " << price << " (" << numerator << "/"
                       << denominator << ")";

        return true;
    }
    catch (const std::exception& e)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to parse price: " << e.what();
        return false;
    }
}

bool CoinGeckoPriceProvider::parseRangeResponse(
    const std::string& response,
    uint64_t targetTimestamp,
    const std::string& vsCurrency,
    int64_t& numerator,
    int64_t& denominator)
{
    // Response format: {"prices":[[timestamp_ms, price], [timestamp_ms, price], ...]}

    size_t pricesPos = response.find("\"prices\"");
    if (pricesPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] No prices found in response";
        return false;
    }

    size_t arrayStart = response.find("[[", pricesPos);
    if (arrayStart == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Invalid prices array format";
        return false;
    }

    // Find closest price to target timestamp
    double bestPrice = 0.0;
    uint64_t bestTimestampDiff = UINT64_MAX;

    size_t pos = arrayStart;
    while (true)
    {
        // Find next price entry [timestamp, price]
        size_t bracketStart = response.find("[", pos);
        if (bracketStart == std::string::npos || response.find("]]", pos) < bracketStart)
        {
            break; // End of array
        }

        // Parse timestamp (in milliseconds)
        size_t timestampEnd = response.find(",", bracketStart);
        if (timestampEnd == std::string::npos)
        {
            break;
        }

        std::string timestampStr =
            response.substr(bracketStart + 1, timestampEnd - bracketStart - 1);
        uint64_t timestampMs = std::stoull(timestampStr);
        uint64_t timestampSec = timestampMs / 1000;

        // Parse price
        size_t priceEnd = response.find("]", timestampEnd);
        if (priceEnd == std::string::npos)
        {
            break;
        }

        std::string priceStr = response.substr(timestampEnd + 1, priceEnd - timestampEnd - 1);
        double price = std::stod(priceStr);

        // Check if this is closer to target
        uint64_t diff = (timestampSec > targetTimestamp) ? (timestampSec - targetTimestamp) :
                                                           (targetTimestamp - timestampSec);

        if (diff < bestTimestampDiff)
        {
            bestTimestampDiff = diff;
            bestPrice = price;
        }

        pos = priceEnd + 1;
    }

    if (bestPrice == 0.0)
    {
        OM_LOG_ERROR() << "[" << _name << "] No valid prices found";
        return false;
    }

    // Convert to rational number
    numerator = static_cast<int64_t>(bestPrice * 1000000);
    denominator = 1000000;

    OM_LOG_INFO() << "[" << _name << "] Found price within " << bestTimestampDiff
                  << " seconds of target";
    OM_LOG_DEBUG() << "[" << _name << "] Price: " << bestPrice << " (" << numerator << "/"
                   << denominator << ")";

    return true;
}

bool CoinGeckoPriceProvider::parseHistoryResponse(
    const std::string& response,
    const std::string& vsCurrency,
    int64_t& numerator,
    int64_t& denominator)
{
    // Response format: {"market_data":{"current_price":{"usd":45000.5}}}

    size_t marketDataPos = response.find("\"market_data\"");
    if (marketDataPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] market_data not found";
        return false;
    }

    size_t currentPricePos = response.find("\"current_price\"", marketDataPos);
    if (currentPricePos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] current_price not found";
        return false;
    }

    // Find currency price
    std::string searchKey = "\"" + vsCurrency + "\":";
    size_t pos = response.find(searchKey, currentPricePos);
    if (pos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Price for " << vsCurrency << " not found";
        return false;
    }

    pos += searchKey.length();
    size_t endPos = response.find_first_of(",}", pos);
    std::string priceStr = response.substr(pos, endPos - pos);

    try
    {
        double price = std::stod(priceStr);

        numerator = static_cast<int64_t>(price * 1000000);
        denominator = 1000000;

        OM_LOG_DEBUG() << "[" << _name << "] Historical price: " << price << " (" << numerator
                       << "/" << denominator << ")";

        return true;
    }
    catch (const std::exception& e)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to parse price: " << e.what();
        return false;
    }
}

// ============================================================================
// Price Service

PriceService::PriceService(const std::string& rHostname, uint16_t hostPort) :
    BaseOracleService(
        rHostname,
        hostPort,
        "Price",
        PRICE_ORACLE_QUERY_SIZE,
        PRICE_ORACLE_REPLY_SIZE)
{
    // Register default providers
    registerProvider("mock", std::make_shared<MockPriceProvider>());

    // Register CoinGecko if API key is available
    const char* apiKey = std::getenv("COINGECKO_API_KEY");
    const char* apiType = std::getenv("COINGECKO_API_TYPE");
    registerProvider(
        "coingecko",
        std::make_shared<CoinGeckoPriceProvider>(apiKey ? apiKey : "", apiType ? apiType : "free"));
}

PriceService::~PriceService()
{
    printStatistics();
}

void PriceService::registerProvider(
    const std::string& oracleId,
    std::shared_ptr<PriceProvider> provider)
{
    std::lock_guard<std::mutex> lock(_providersMutex);

    std::string lowerOracleId = oracleId;
    std::transform(lowerOracleId.begin(), lowerOracleId.end(), lowerOracleId.begin(), ::tolower);

    _providers[lowerOracleId] = provider;

    OM_LOG_INFO() << "[Price] Registered provider: " << oracleId << " (" << provider->getName()
                  << ")";
}

// ============================================================================
// BaseOracleService Implementation
// TODO: uint16_t is the error type get from core code, verify it
uint16_t PriceService::processInterfaceQuery(
    const std::vector<uint8_t>& queryPayload,
    std::vector<uint8_t>& replyPayload)
{
    // Parse the querry
    if (queryPayload.size() < PRICE_ORACLE_QUERY_SIZE)
    {
        OM_LOG_ERROR() << "[Price] Invalid query size: " << queryPayload.size();
        return ORACLE_FLAG_INVALID_ARG; // Parse error
    }

    Price::OracleQuery query;
    std::memcpy(&query, queryPayload.data(), sizeof(Price::OracleQuery));

    // Extract strings
    std::string oracleId = bytesToString((const char*)query.oracle.m256i_i8, 32);
    std::string currency1 = bytesToString((const char*)query.currency1.m256i_i8, 32);
    std::string currency2 = bytesToString((const char*)query.currency2.m256i_i8, 32);

    OM_LOG_DEBUG() << "  Query: oracle=" << oracleId << ", " << currency1 << "/" << currency2
                   << ", timestamp=" << getTimeStampString(query.timestamp);

    // Look for provider
    std::transform(oracleId.begin(), oracleId.end(), oracleId.begin(), ::tolower);

    std::shared_ptr<PriceProvider> provider;
    {
        std::lock_guard<std::mutex> lock(_providersMutex);
        auto it = _providers.find(oracleId);
        if (it != _providers.end())
        {
            provider = it->second;
        }
    }

    if (!provider)
    {
        OM_LOG_ERROR() << "[Price] Unknown oracle provider: " << oracleId;
        return ORACLE_FLAG_INVALID_ORACLE; // Provider not found
    }

    // Get price
    Price::OracleReply reply;
    int64_t numerator = 0;
    int64_t denominator = 1;
    uint64_t timestamp = timestampToUnixSeconds(query.timestamp);
    if (timestamp == 0)
    {
        OM_LOG_ERROR() << "[Price] Invalid timestamp in query";
        return ORACLE_FLAG_INVALID_ARG; // Invalid timestamp
    }

    if (!provider->getPrice(currency1, currency2, timestamp, numerator, denominator))
    {
        return ORACLE_FLAG_ORACLE_UNAVAIL; // Price not available
    }

    reply.numerator = numerator;
    reply.denominator = denominator;
    OM_LOG_DEBUG() << "  Result: " << currency1 << "/" << currency2 << " = " << reply.numerator
                   << "/" << reply.denominator;

    // Build the reply
    replyPayload.resize(sizeof(Price::OracleReply));
    std::memcpy(replyPayload.data(), &reply, sizeof(Price::OracleReply));

    return 0;
}

} // namespace oracle