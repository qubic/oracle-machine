#include "exchange_provider.h"
#include "om_common/logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <thread>

#include <curl/curl.h>

namespace oracle
{

ExchangePriceProvider::ExchangePriceProvider(
    const std::string& name,
    const std::string& baseUrl,
    const std::string& apiKey,
    double rateLimitDelayFree,
    double rateLimitDelayPaid,
    int cacheTtl) :
    PriceProvider(name),
    _apiKey(apiKey),
    _apiBaseUrl(baseUrl),
    _rateLimitDelay(apiKey.empty() ? rateLimitDelayFree : rateLimitDelayPaid),
    _cacheTtl(cacheTtl),
    _lastRequestTime(std::chrono::steady_clock::now() - std::chrono::seconds(10))
{
    if (!apiKey.empty())
    {
        OM_LOG_INFO() << "[" << _name << "] Using paid API tier";
    }
    else
    {
        OM_LOG_INFO() << "[" << _name << "] Using free tier (rate limited)";
    }
}

uint16_t ExchangePriceProvider::getPrice(
    const std::string& currency1,
    const std::string& currency2,
    int64_t& numerator,
    int64_t& denominator)
{
    // Get current time and calculate the previous finished minute
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Get the previous finished minute (floor to minute, then go back one minute)
    int64_t currentMinuteMs = (nowMs / 60000) * 60000;
    int64_t prevMinuteMs = currentMinuteMs - 60000;

    return getPriceAtTimestamp(currency1, currency2, prevMinuteMs, numerator, denominator);
}

uint16_t ExchangePriceProvider::getPriceAtTimestamp(
    const std::string& currency1,
    const std::string& currency2,
    int64_t timestampMs,
    int64_t& numerator,
    int64_t& denominator)
{
    // If timestampMs is 0, use current time (previous finished minute)
    if (timestampMs == 0)
    {
        auto now = std::chrono::system_clock::now();
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        int64_t currentMinuteMs = (nowMs / 60000) * 60000;
        timestampMs = currentMinuteMs - 60000;  // Previous finished minute

        // Log the actual time being used
        time_t t = timestampMs / 1000;
        struct tm tm_buf = {};
#ifdef _WIN32
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        OM_LOG_DEBUG() << "[" << _name << "] Using current price timestamp: "
                       << (tm_buf.tm_year + 1900) << "-"
                       << std::setw(2) << std::setfill('0') << (tm_buf.tm_mon + 1) << "-"
                       << std::setw(2) << std::setfill('0') << tm_buf.tm_mday << " "
                       << std::setw(2) << std::setfill('0') << tm_buf.tm_hour << ":"
                       << std::setw(2) << std::setfill('0') << tm_buf.tm_min << ":"
                       << std::setw(2) << std::setfill('0') << tm_buf.tm_sec << " UTC";
    }

    // Convert currencies to uppercase
    std::string base = currency1;
    std::string quote = currency2;
    std::transform(base.begin(), base.end(), base.begin(), ::toupper);
    std::transform(quote.begin(), quote.end(), quote.begin(), ::toupper);

    std::string symbol = formatSymbol(base, quote);

    // Calculate the 1-minute candle that closed before this timestamp
    // Floor to minute boundary
    int64_t candleCloseMs = (timestampMs / 60000) * 60000;
    int64_t candleOpenMs = candleCloseMs - 60000;

    std::string cacheKey = symbol + "@" + std::to_string(candleCloseMs);

    // Check cache
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _cache.find(cacheKey);
        if (it != _cache.end())
        {
            auto elapsed = std::chrono::steady_clock::now() - it->second.fetchTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < _cacheTtl)
            {
                numerator = it->second.numerator;
                denominator = it->second.denominator;
                OM_LOG_DEBUG() << "[" << _name << "] Cache hit: " << cacheKey;
                return RETURN_NO_ERROR;
            }
        }
    }

    // Build URL for kline/candlestick data
    std::string url = buildKlineUrl(symbol, candleOpenMs, candleCloseMs, 1);

    OM_LOG_INFO() << "[" << _name << "] Fetching kline: " << url;

    enforceRateLimit();

    std::string response;
    uint16_t httpResult = httpGet(url, response);
    if (httpResult != RETURN_NO_ERROR)
    {
        return httpResult;
    }

    std::string closePriceString;
    uint16_t parseResult = parseKlineResponse(response, closePriceString);
    if (parseResult != RETURN_NO_ERROR)
    {
        return parseResult;
    }

    if (!priceStringToRational(closePriceString, numerator, denominator))
    {
        OM_LOG_ERROR() << "Failed to convert price stirng " << closePriceString << " to rational number";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    // Update cache
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        CacheEntry entry;
        entry.numerator = numerator;
        entry.denominator = denominator;
        entry.candleTimestampMs = candleCloseMs;
        entry.fetchTime = std::chrono::steady_clock::now();
        _cache[cacheKey] = entry;
    }

    OM_LOG_DEBUG() << "[" << _name << "] Price: " << closePriceString
                   << " (" << numerator << "/" << denominator << ")";

    return RETURN_NO_ERROR;
}

void ExchangePriceProvider::enforceRateLimit()
{
    std::lock_guard<std::mutex> lock(_rateLimitMutex);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - _lastRequestTime).count();

    int64_t delayMs = static_cast<int64_t>(_rateLimitDelay * 1000);

    if (elapsed < delayMs)
    {
        int64_t sleepMs = delayMs - elapsed;
        OM_LOG_DEBUG() << "[" << _name << "] Rate limit: sleeping " << sleepMs << "ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }

    _lastRequestTime = std::chrono::steady_clock::now();
}

uint16_t ExchangePriceProvider::httpGet(const std::string& url, std::string& response)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to initialize curl";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            std::string* str = static_cast<std::string*>(userdata);
            str->append(static_cast<char*>(ptr), size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // Add exchange-specific API key header if available
    struct curl_slist* headers = nullptr;
    std::string apiHeader = getApiKeyHeader();
    if (!apiHeader.empty())
    {
        headers = curl_slist_append(headers, apiHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);

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

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (httpCode != 200)
    {
        OM_LOG_ERROR() << "[" << _name << "] HTTP " << httpCode << ": " << response;
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }

    return RETURN_NO_ERROR;
}

std::string ExchangePriceProvider::getApiKeyHeader() const
{
    return "";
}

} // namespace oracle
