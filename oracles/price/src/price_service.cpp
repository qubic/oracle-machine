#include "price_service.h"
#include "binance_provider.h"
#include "mexc_provider.h"
#include "gate_provider.h"
#include "combined_provider.h"
#include "om_common/logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <cmath>

// For HTTP requests (using libcurl)
#include <curl/curl.h>

namespace oracle
{

bool priceStringToRational(const std::string& priceStr, int64_t& numerator, int64_t& denominator)
{
    // Try to convert string to rational directly
    numerator = 0;
    denominator = 1;
    bool okay = true;
    bool seenDot = false;
    for (size_t i = 0; i < priceStr.size(); ++i)
    {
        char c = priceStr[i];
        if (c >= '0' && c <= '9')
        {
            // regular digit
            numerator = numerator * 10 + (c - '0');
            if (seenDot)
                denominator *= 10;
        }
        else if (c == '.' && !seenDot)
        {
            // decimal point
            seenDot = true;
        }
        else
        {
            // unexpected character
            okay = false;
            break;
        }
    }
    OM_LOG_DEBUG() << "priceStringToRational(): priceStr " << priceStr << ", num " << numerator << ", denom " << denominator << ", okay " << okay;
    if (okay)
        return true;

    // Fallback solution
    double price;
    try
    {
        price = std::stod(priceStr);
    }
    catch (const std::exception& e)
    {
        return false;
    }

    // Use fixed 8 decimal places precision (10^8)
    constexpr int64_t PRICE_DENOMINATOR = 100000000;
    numerator = static_cast<int64_t>(std::round(price * PRICE_DENOMINATOR));
    denominator = PRICE_DENOMINATOR;

    return true;
}

static std::string getTimeStampString(const QPI::DateAndTime& rQpiDateTime)
{
    std::ostringstream ss;
    ss << rQpiDateTime.getYear() << "-"
       << std::setfill('0') << std::setw(2) << (int)rQpiDateTime.getMonth() << "-"
       << std::setw(2) << (int)rQpiDateTime.getDay() << " "
       << std::setw(2) << (int)rQpiDateTime.getHour() << ":"
       << std::setw(2) << (int)rQpiDateTime.getMinute() << ":"
       << std::setw(2) << (int)rQpiDateTime.getSecond() << " UTC";

    return ss.str();
}

// Convert QPI::DateAndTime to Unix timestamp in milliseconds (UTC)
static int64_t dateTimeToUnixMs(const QPI::DateAndTime& dt)
{
    // Build tm struct (UTC)
    struct tm tm = {};
    tm.tm_year = dt.getYear() - 1900;  // tm_year is years since 1900
    tm.tm_mon = dt.getMonth() - 1;      // tm_mon is 0-11
    tm.tm_mday = dt.getDay();
    tm.tm_hour = dt.getHour();
    tm.tm_min = dt.getMinute();
    tm.tm_sec = dt.getSecond();
    tm.tm_isdst = 0;  // Not daylight saving

    // Convert to Unix timestamp (UTC) using timegm (POSIX) or _mkgmtime (Windows)
#ifdef _WIN32
    time_t t = _mkgmtime(&tm);
#else
    time_t t = timegm(&tm);
#endif

    if (t == -1)
    {
        return 0;  // Invalid date
    }

    // Convert to milliseconds and add the millisecond component
    return static_cast<int64_t>(t) * 1000 + dt.getMillisec();
}

// Normalize oracle ID for combined oracles (e.g., "gate_binance" -> "binance_gate")
// Sorts exchange names alphabetically so order doesn't matter
static std::string normalizeOracleId(const std::string& oracleId)
{
    size_t underscorePos = oracleId.find('_');
    if (underscorePos == std::string::npos)
    {
        return oracleId;
    }

    std::string first = oracleId.substr(0, underscorePos);
    std::string second = oracleId.substr(underscorePos + 1);

    if (first > second)
    {
        std::swap(first, second);
    }

    return first + "_" + second;
}

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

uint16_t MockPriceProvider::getPrice(
    const std::string& currency1,
    const std::string& currency2,
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
        return RETURN_NO_ERROR;
    }

    OM_LOG_ERROR() << "[" << _name << "] Price not found: " << pair;
    return RETURN_ERROR_INVALID_ARG;
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

uint16_t CoinGeckoPriceProvider::getPrice(
    const std::string& currency1,
    const std::string& currency2,
    int64_t& numerator,
    int64_t& denominator)
{
    std::string pair = currency1 + "/" + currency2;

    // Check cache
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _cache.find(pair);
        if (it != _cache.end())
        {
            time_t now = time(nullptr);
            if (now - it->second.timestamp < CACHE_TTL)
            {
                numerator = it->second.numerator;
                denominator = it->second.denominator;
                OM_LOG_DEBUG() << "[" << _name << "] Cache hit: " << pair << " = " << numerator
                               << "/" << denominator;
                return RETURN_NO_ERROR;
            }
        }
    }

    // Cache miss - fetch from API
    OM_LOG_DEBUG() << "[" << _name << "] Cache miss - fetching " << pair;

    auto returnCode = fetchFromAPI(currency1, currency2, numerator, denominator);
    if (returnCode == RETURN_NO_ERROR)
    {
        // Update cache
        CacheEntry entry;
        entry.numerator = numerator;
        entry.denominator = denominator;
        entry.timestamp = time(nullptr);

        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cache[pair] = entry;
    }

    return returnCode;
}

uint16_t CoinGeckoPriceProvider::fetchFromAPI(
    const std::string& currency1,
    const std::string& currency2,
    int64_t& numerator,
    int64_t& denominator)
{
    // Rate limiting
    {
        std::lock_guard<std::mutex> lock(_rateLimitMutex);
        time_t now = time(nullptr);
        double elapsed = difftime(now, _lastRequestTime);

        if (elapsed < RATE_LIMIT_DELAY)
        {
            double sleepTime = RATE_LIMIT_DELAY - elapsed;
            OM_LOG_DEBUG() << "  [Rate limit] Sleeping " << sleepTime << "s";

            // Cross-platform sleep
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(sleepTime * 1000)));
        }

        _lastRequestTime = time(nullptr);
    }

    // Get coin ID
    std::string coinId = getCoinId(currency1);
    if (coinId.empty())
    {
        OM_LOG_ERROR() << "[" << _name << "] Unknown currency: " << currency1;
        return RETURN_ERROR_INVALID_ARG;
    }

    // Convert currency2 to lowercase
    std::string vsCurrency = currency2;
    std::transform(vsCurrency.begin(), vsCurrency.end(), vsCurrency.begin(), ::tolower);

    // Map stablecoins to USD
    // TODO: depend on provider, this should be fixed. For example, Binance provide USDT
    if (vsCurrency == "usdt" || vsCurrency == "usdc")
    {
        vsCurrency = "usd";
    }

    // Build URL
    std::string url = "https://api.coingecko.com/api/v3/simple/price?ids=" + coinId +
                      "&vs_currencies=" + vsCurrency;

    OM_LOG_INFO() << "[" << _name << "] Fetching: " << url;

    // Initialize libcurl
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to initialize curl";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Response buffer
    std::string response;

    // Set up curl options
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

    // Add API key header if available
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

    // Cleanup headers
    if (headers)
    {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK)
    {
        OM_LOG_ERROR() << "[" << _name << "] Curl error: " << curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Check HTTP status
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (httpCode != 200)
    {
        OM_LOG_ERROR() << "[" << _name << "] HTTP error: " << httpCode;
        OM_LOG_ERROR() << "  Response: " << response;
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Parse JSON response (simple parsing for this specific format)
    // Example: {"bitcoin":{"usd":45000.5}}

    // Find the price value
    std::string searchKey = "\"" + vsCurrency + "\":";
    size_t pos = response.find(searchKey);
    if (pos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Price not found in response";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    pos += searchKey.length();

    // Extract number (simple approach - works for this API)
    size_t endPos = response.find_first_of("},", pos);
    std::string priceStr = response.substr(pos, endPos - pos);

    if (!priceStringToRational(priceStr, numerator, denominator))
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to parse price: " << priceStr;
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }
    else
    {
        OM_LOG_DEBUG() << "[" << _name << "] Price fetched: " << currency1 << "/" << currency2
                       << " = " << priceStr << " (" << numerator << "/" << denominator << ")";

        return RETURN_NO_ERROR;
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

    // Register CoinGecko
    const char* coingeckoApiKey = std::getenv("COINGECKO_API_KEY");
    const char* coingeckoApiType = std::getenv("COINGECKO_API_TYPE");
    registerProvider(
        "coingecko",
        std::make_shared<CoinGeckoPriceProvider>(
            coingeckoApiKey ? coingeckoApiKey : "",
            coingeckoApiType ? coingeckoApiType : "free"));

    // Register exchange providers
    const char* binanceApiKey = std::getenv("BINANCE_API_KEY");
    const char* mexcApiKey = std::getenv("MEXC_API_KEY");
    const char* gateApiKey = std::getenv("GATE_API_KEY");

    // Single source providers
    registerProvider(
        "binance",
        std::make_shared<BinancePriceProvider>(binanceApiKey ? binanceApiKey : ""));

    registerProvider(
        "mexc",
        std::make_shared<MexcPriceProvider>(mexcApiKey ? mexcApiKey : ""));

    registerProvider(
        "gate",
        std::make_shared<GatePriceProvider>(gateApiKey ? gateApiKey : ""));

    // Combined providers (mean of 2 sources)
    registerProvider(
        "binance_mexc",
        createBinanceMexcProvider(
            binanceApiKey ? binanceApiKey : "",
            mexcApiKey ? mexcApiKey : ""));

    registerProvider(
        "binance_gate",
        createBinanceGateProvider(
            binanceApiKey ? binanceApiKey : "",
            gateApiKey ? gateApiKey : ""));

    registerProvider(
        "gate_mexc",  // Alphabetical order: g < m
        createMexcGateProvider(
            mexcApiKey ? mexcApiKey : "",
            gateApiKey ? gateApiKey : ""));
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
uint16_t PriceService::processInterfaceQuery(
    const std::vector<uint8_t>& queryPayload,
    std::vector<uint8_t>& replyPayload)
{
    // Parse the querry
    if (queryPayload.size() < PRICE_ORACLE_QUERY_SIZE)
    {
        OM_LOG_ERROR() << "[Price] Invalid query size: " << queryPayload.size();
        return RETURN_ERROR_INVALID_ARG; // Parse error
    }

    Price::OracleQuery query;
    std::memcpy(&query, queryPayload.data(), sizeof(Price::OracleQuery));

    // Extract strings
    std::string oracleId = bytesToString((const char*)query.oracle.m256i_i8, 32);
    std::string currency1 = bytesToString((const char*)query.currency1.m256i_i8, 32);
    std::string currency2 = bytesToString((const char*)query.currency2.m256i_i8, 32);

    // Check if timestamp is zero (current price request)
    bool isCurrentPrice = (query.timestamp.getYear() == 0 && query.timestamp.getMonth() == 0 &&
                           query.timestamp.getDay() == 0 && query.timestamp.getHour() == 0 &&
                           query.timestamp.getMinute() == 0 && query.timestamp.getSecond() == 0);

    if (isCurrentPrice)
    {
        OM_LOG_DEBUG() << "  Query: oracle=" << oracleId << ", " << currency1 << "/" << currency2
                       << ", timestamp=0 (current price)";
    }
    else
    {
        OM_LOG_DEBUG() << "  Query: oracle=" << oracleId << ", " << currency1 << "/" << currency2
                       << ", timestamp=" << getTimeStampString(query.timestamp);
    }

    // Look for provider (normalize to handle gate_binance == binance_gate)
    std::transform(oracleId.begin(), oracleId.end(), oracleId.begin(), ::tolower);
    oracleId = normalizeOracleId(oracleId);

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
        return RETURN_ERROR_INVALID_ORACLE; // Provider not found
    }

    // Convert query timestamp to Unix milliseconds (UTC)
    // A zero timestamp (all fields = 0) means "current price" - keep timestampMs = 0
    int64_t timestampMs = 0;
    if (!isCurrentPrice)
    {
        timestampMs = dateTimeToUnixMs(query.timestamp);
    }

    // Get price at the specified timestamp
    Price::OracleReply reply;
    int64_t numerator = 0;
    int64_t denominator = 1;
    uint16_t returnValue = provider->getPriceAtTimestamp(
        currency1, currency2, timestampMs, numerator, denominator);
    if (returnValue != RETURN_NO_ERROR)
    {
        return returnValue; // Price not available
    }

    reply.numerator = numerator;
    reply.denominator = denominator;
    OM_LOG_DEBUG() << "  Result: " << currency1 << "/" << currency2 << " = " << reply.numerator
                   << "/" << reply.denominator;

    // Build the reply
    replyPayload.resize(sizeof(Price::OracleReply));
    std::memcpy(replyPayload.data(), &reply, sizeof(Price::OracleReply));

    return RETURN_NO_ERROR;
}

} // namespace oracle