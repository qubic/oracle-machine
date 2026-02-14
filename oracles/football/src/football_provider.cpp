#include "football_provider.h"
#include "om_common/logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>
#include <thread>

// For HTTP requests (using libcurl)
#include <curl/curl.h>

namespace oracle
{

// ============================================================================
// Mock Football Provider

MockFootballProvider::MockFootballProvider() : FootballProvider("MockFootballProvider")
{
    // Set some default mock matches
    // Match 1: Manchester United vs Liverpool (Premier League)
    setMatchData(1001, 33, 40, 2, 1, 2, 90); // Finished: Man Utd 2-1 Liverpool

    // Match 2: Real Madrid vs Barcelona (La Liga)
    setMatchData(1002, 541, 529, 3, 3, 2, 90); // Finished: Real Madrid 3-3 Barcelona

    // Match 3: Bayern Munich vs Borussia Dortmund (Bundesliga)
    setMatchData(1003, 157, 165, -1, -1, 0, 0); // Not started

    OM_LOG_INFO() << "[" << _name << "] Initialized with default mock matches";
}

uint16_t MockFootballProvider::getMatchData(
    uint32_t matchId,
    uint32_t leagueId,
    uint32_t season,
    uint32_t& homeTeamId,
    uint32_t& awayTeamId,
    int32_t& homeScore,
    int32_t& awayScore,
    uint8_t& status,
    uint8_t& elapsedMinutes)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _matches.find(matchId);
    if (it != _matches.end())
    {
        homeTeamId = it->second.homeTeamId;
        awayTeamId = it->second.awayTeamId;
        homeScore = it->second.homeScore;
        awayScore = it->second.awayScore;
        status = it->second.status;
        elapsedMinutes = it->second.elapsedMinutes;

        OM_LOG_DEBUG() << "[" << _name << "] Match found: " << matchId 
                       << " (" << homeTeamId << " vs " << awayTeamId << ") "
                       << homeScore << "-" << awayScore;
        return RETURN_NO_ERROR;
    }

    OM_LOG_ERROR() << "[" << _name << "] Match not found: " << matchId;
    return RETURN_ERROR_INVALID_ARG;
}

void MockFootballProvider::setMatchData(
    uint32_t matchId,
    uint32_t homeTeamId,
    uint32_t awayTeamId,
    int32_t homeScore,
    int32_t awayScore,
    uint8_t status,
    uint8_t elapsedMinutes)
{
    std::lock_guard<std::mutex> lock(_mutex);
    MatchData data;
    data.homeTeamId = homeTeamId;
    data.awayTeamId = awayTeamId;
    data.homeScore = homeScore;
    data.awayScore = awayScore;
    data.status = status;
    data.elapsedMinutes = elapsedMinutes;
    _matches[matchId] = data;

    OM_LOG_DEBUG() << "[" << _name << "] Set match data: " << matchId 
                   << " (" << homeTeamId << " vs " << awayTeamId << ") "
                   << homeScore << "-" << awayScore;
}

// ============================================================================
// API-Football Provider

ApiFootballProvider::ApiFootballProvider(const std::string& apiKey) :
    FootballProvider("ApiFootball"), _apiKey(apiKey), _lastRequestTime(0)
{
    if (!_apiKey.empty())
    {
        OM_LOG_INFO() << "[" << _name << "] Configured with API key";
    }
    else
    {
        OM_LOG_INFO() << "[" << _name << "] Using free tier (no API key)";
    }
}

uint16_t ApiFootballProvider::getMatchData(
    uint32_t matchId,
    uint32_t leagueId,
    uint32_t season,
    uint32_t& homeTeamId,
    uint32_t& awayTeamId,
    int32_t& homeScore,
    int32_t& awayScore,
    uint8_t& status,
    uint8_t& elapsedMinutes)
{
    // Check cache
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _cache.find(matchId);
        if (it != _cache.end())
        {
            time_t now = time(nullptr);
            if (now - it->second.timestamp < CACHE_TTL)
            {
                homeTeamId = it->second.homeTeamId;
                awayTeamId = it->second.awayTeamId;
                homeScore = it->second.homeScore;
                awayScore = it->second.awayScore;
                status = it->second.status;
                elapsedMinutes = it->second.elapsedMinutes;

                OM_LOG_DEBUG() << "[" << _name << "] Cache hit: match " << matchId;
                return RETURN_NO_ERROR;
            }
        }
    }

    // Cache miss - fetch from API
    OM_LOG_DEBUG() << "[" << _name << "] Cache miss - fetching match " << matchId;

    auto returnCode = fetchFromAPI(
        matchId, leagueId, season,
        homeTeamId, awayTeamId, homeScore, awayScore, status, elapsedMinutes);

    if (returnCode == RETURN_NO_ERROR)
    {
        // Update cache
        CacheEntry entry;
        entry.homeTeamId = homeTeamId;
        entry.awayTeamId = awayTeamId;
        entry.homeScore = homeScore;
        entry.awayScore = awayScore;
        entry.status = status;
        entry.elapsedMinutes = elapsedMinutes;
        entry.timestamp = time(nullptr);

        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cache[matchId] = entry;
    }

    return returnCode;
}

uint8_t ApiFootballProvider::parseMatchStatus(
    const std::string& statusShort,
    const std::string& statusLong)
{
    // API-Football status codes:
    // TBD, NS = Not Started
    // 1H, HT, 2H, ET, BT, P, SUSP, INT, LIVE = In Progress
    // FT, AET, PEN = Finished
    // PST, CANC, ABD = Postponed/Cancelled

    if (statusShort == "TBD" || statusShort == "NS")
        return 0; // NOT_STARTED
    
    if (statusShort == "1H" || statusShort == "HT" || statusShort == "2H" ||
        statusShort == "ET" || statusShort == "BT" || statusShort == "P" ||
        statusShort == "SUSP" || statusShort == "INT" || statusShort == "LIVE")
        return 1; // IN_PROGRESS
    
    if (statusShort == "FT" || statusShort == "AET" || statusShort == "PEN")
        return 2; // FINISHED
    
    if (statusShort == "PST")
        return 3; // POSTPONED
    
    if (statusShort == "CANC" || statusShort == "ABD")
        return 4; // CANCELLED

    return 0; // Default to NOT_STARTED
}

uint16_t ApiFootballProvider::fetchFromAPI(
    uint32_t matchId,
    uint32_t leagueId,
    uint32_t season,
    uint32_t& homeTeamId,
    uint32_t& awayTeamId,
    int32_t& homeScore,
    int32_t& awayScore,
    uint8_t& status,
    uint8_t& elapsedMinutes)
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
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(sleepTime * 1000)));
        }

        _lastRequestTime = time(nullptr);
    }

    // Build URL - using fixtures endpoint with match ID
    std::ostringstream urlStream;
    urlStream << "https://v3.football.api-sports.io/fixtures?id=" << matchId;
    std::string url = urlStream.str();

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

    // Add API key header
    struct curl_slist* headers = nullptr;
    if (!_apiKey.empty())
    {
        std::string headerValue = "x-apisports-key: " + _apiKey;
        headers = curl_slist_append(headers, headerValue.c_str());
    }
    else
    {
        // Free tier requires header even without key
        headers = curl_slist_append(headers, "x-apisports-key: ");
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

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

    // Parse JSON response (simple parsing for specific fields)
    // Example response structure:
    // {"response":[{"fixture":{"id":123},"teams":{"home":{"id":33},"away":{"id":40}},
    //               "goals":{"home":2,"away":1},"score":{"fulltime":{"home":2,"away":1}},
    //               "fixture":{"status":{"short":"FT","elapsed":90}}}]}

    // Extract home team ID
    size_t homeTeamPos = response.find("\"home\":{\"id\":");
    if (homeTeamPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to parse home team ID";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }
    homeTeamPos += 13; // Length of "\"home\":{\"id\":"
    size_t homeTeamEnd = response.find_first_of(",}", homeTeamPos);
    homeTeamId = std::stoul(response.substr(homeTeamPos, homeTeamEnd - homeTeamPos));

    // Extract away team ID
    size_t awayTeamPos = response.find("\"away\":{\"id\":", homeTeamEnd);
    if (awayTeamPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to parse away team ID";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }
    awayTeamPos += 13;
    size_t awayTeamEnd = response.find_first_of(",}", awayTeamPos);
    awayTeamId = std::stoul(response.substr(awayTeamPos, awayTeamEnd - awayTeamPos));

    // Extract scores (goals section)
    size_t goalsPos = response.find("\"goals\":{\"home\":");
    if (goalsPos != std::string::npos)
    {
        goalsPos += 16;
        size_t goalsEnd = response.find_first_of(",}", goalsPos);
        std::string homeScoreStr = response.substr(goalsPos, goalsEnd - goalsPos);
        homeScore = (homeScoreStr == "null") ? -1 : std::stoi(homeScoreStr);

        size_t awayGoalsPos = response.find("\"away\":", goalsEnd);
        if (awayGoalsPos != std::string::npos)
        {
            awayGoalsPos += 7;
            size_t awayGoalsEnd = response.find_first_of(",}", awayGoalsPos);
            std::string awayScoreStr = response.substr(awayGoalsPos, awayGoalsEnd - awayGoalsPos);
            awayScore = (awayScoreStr == "null") ? -1 : std::stoi(awayScoreStr);
        }
    }
    else
    {
        homeScore = -1;
        awayScore = -1;
    }

    // Extract status
    size_t statusPos = response.find("\"status\":{\"short\":\"");
    if (statusPos != std::string::npos)
    {
        statusPos += 20;
        size_t statusEnd = response.find("\"", statusPos);
        std::string statusShort = response.substr(statusPos, statusEnd - statusPos);
        status = parseMatchStatus(statusShort, "");
    }
    else
    {
        status = 0; // Default to NOT_STARTED
    }

    // Extract elapsed minutes
    size_t elapsedPos = response.find("\"elapsed\":");
    if (elapsedPos != std::string::npos)
    {
        elapsedPos += 10;
        size_t elapsedEnd = response.find_first_of(",}", elapsedPos);
        std::string elapsedStr = response.substr(elapsedPos, elapsedEnd - elapsedPos);
        elapsedMinutes = (elapsedStr == "null") ? 0 : std::stoi(elapsedStr);
    }
    else
    {
        elapsedMinutes = 0;
    }

    OM_LOG_DEBUG() << "[" << _name << "] Match data fetched: " << matchId
                   << " (" << homeTeamId << " vs " << awayTeamId << ") "
                   << homeScore << "-" << awayScore
                   << " status=" << (int)status << " elapsed=" << (int)elapsedMinutes;

    return RETURN_NO_ERROR;
}

} // namespace oracle
