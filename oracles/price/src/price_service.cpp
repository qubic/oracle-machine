#include "price_service.h"
#include <algorithm>
#include <chrono>
#include <sstream> 
#include <cstring>
#include <ctime>
#include <iostream>
#include <thread>

// For HTTP requests (using libcurl)
#include <curl/curl.h>

namespace oracle
{

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

    std::cout << "[" << _name << "] Initialized with default mock prices" << std::endl;
}

bool MockPriceProvider::getPrice(
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
        std::cout << "[" << _name << "] Price found: " << pair << " = " << numerator << "/"
                  << denominator << std::endl;
        return true;
    }

    std::cerr << "[" << _name << "] Price not found: " << pair << std::endl;
    return false;
}

void MockPriceProvider::setPrice(const std::string& pair, int64_t num, int64_t denom)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _prices[pair] = std::make_pair(num, denom);
    std::cout << "[" << _name << "] Set price: " << pair << " = " << num << "/" << denom
              << std::endl;
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
        std::cout << "[" << _name << "] Configured with API key: " << " (type: " << _apiType << ")"
                  << std::endl;
    }
    else
    {
        std::cout << "[" << _name << "] Using free tier (no API key)" << std::endl;
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

bool CoinGeckoPriceProvider::getPrice(
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
                std::cout << "[" << _name << "] Cache hit: " << pair << " = " << numerator << "/"
                          << denominator << std::endl;
                return true;
            }
        }
    }

    // Cache miss - fetch from API
    std::cout << "[" << _name << "] Cache miss - fetching " << pair << std::endl;

    if (fetchFromAPI(currency1, currency2, numerator, denominator))
    {
        // Update cache
        CacheEntry entry;
        entry.numerator = numerator;
        entry.denominator = denominator;
        entry.timestamp = time(nullptr);

        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cache[pair] = entry;

        return true;
    }

    return false;
}

bool CoinGeckoPriceProvider::fetchFromAPI(
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
            std::cout << "  [Rate limit] Sleeping " << sleepTime << "s" << std::endl;

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
        std::cerr << "[" << _name << "] Unknown currency: " << currency1 << std::endl;
        return false;
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

    std::cout << "[" << _name << "] Fetching: " << url << std::endl;

    // Initialize libcurl
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        std::cerr << "[" << _name << "] Failed to initialize curl" << std::endl;
        return false;
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
        std::cerr << "[" << _name << "] Curl error: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return false;
    }

    // Check HTTP status
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (httpCode != 200)
    {
        std::cerr << "[" << _name << "] HTTP error: " << httpCode << std::endl;
        std::cerr << "  Response: " << response << std::endl;
        return false;
    }

    // Parse JSON response (simple parsing for this specific format)
    // Example: {"bitcoin":{"usd":45000.5}}

    // Find the price value
    std::string searchKey = "\"" + vsCurrency + "\":";
    size_t pos = response.find(searchKey);
    if (pos == std::string::npos)
    {
        std::cerr << "[" << _name << "] Price not found in response" << std::endl;
        return false;
    }

    pos += searchKey.length();

    // Extract number (simple approach - works for this API)
    size_t endPos = response.find_first_of("},", pos);
    std::string priceStr = response.substr(pos, endPos - pos);

    try
    {
        double price = std::stod(priceStr);

        // Convert to rational number (numerator/denominator)
        // For simplicity, use 6 decimal places of precision
        numerator = static_cast<int64_t>(price * 1000000);
        denominator = 1000000;

        std::cout << "[" << _name << "] Price fetched: " << currency1 << "/" << currency2 << " = "
                  << price << " (" << numerator << "/" << denominator << ")" << std::endl;

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[" << _name << "] Failed to parse price: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Price Service

PriceService::PriceService(const std::string& rHostname, uint16_t hostPort) :
    BaseOracleService(rHostname, hostPort, "Price", PRICE_ORACLE_QUERY_SIZE, PRICE_ORACLE_REPLY_SIZE)
{
    // Register default providers
    registerProvider("mock", std::make_shared<MockPriceProvider>());

    // Register CoinGecko if API key is available
    const char* apiKey = std::getenv("COINGECKO_API_KEY");
    const char* apiType = std::getenv("COINGECKO_API_TYPE");
    registerProvider(
        "coingecko", std::make_shared<CoinGeckoPriceProvider>(apiKey ? apiKey : "", apiType ? apiType : "free"));
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

    std::cout << "[Price] Registered provider: " << oracleId << " (" << provider->getName() << ")"
              << std::endl;
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
        std::cerr << "[Price] Invalid query size: " << queryPayload.size() << std::endl;
        return 1; // Parse error
    }

    Price::OracleQuery query;
    std::memcpy(&query, queryPayload.data(), sizeof(Price::OracleQuery));

    // Extract strings
    std::string oracleId = bytesToString((const char*)query.oracle.m256i_i8, 32);
    std::string currency1 = bytesToString((const char*)query.currency1.m256i_i8, 32);
    std::string currency2 = bytesToString((const char*)query.currency2.m256i_i8, 32);

    std::cout << "  Query: oracle=" << oracleId << ", " << currency1 << "/" << currency2
              << ", timestamp=" << getTimeStampString(query.timestamp) << std::endl;

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
        std::cerr << "[Price] Unknown oracle provider: " << oracleId << std::endl;
        return 2; // Provider not found
    }

    // Get price
    Price::OracleReply reply;
    int64_t numerator = 0;
    int64_t denominator = 1;
    if (!provider->getPrice(currency1, currency2, numerator, denominator))
    {
        return 3;  // Price not available
    }
    
    reply.numerator = numerator;
    reply.denominator = denominator;
    std::cout << "  Result: " << currency1 << "/" << currency2 
              << " = " << reply.numerator << "/" << reply.denominator << std::endl;

    // Build the reply
    replyPayload.resize(sizeof(Price::OracleReply));
    std::memcpy(replyPayload.data(), &reply, sizeof(Price::OracleReply));
    
    return 0; 
}

} // namespace oracle