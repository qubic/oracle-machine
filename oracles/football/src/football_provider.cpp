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
// TheSportsDB Provider

TheSportsDBProvider::TheSportsDBProvider() :
    FootballProvider("TheSportsDB"), _lastRequestTime(0)
{
    OM_LOG_INFO() << "[" << _name << "] Initialized (no authentication required)";
}

uint16_t TheSportsDBProvider::getMatchData(
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

uint8_t TheSportsDBProvider::parseMatchStatus(const std::string& statusStr)
{
    // TheSportsDB status strings:
    // "Not Started", "Match Scheduled" = Not Started
    // "In Play", "First Half", "Second Half", "Halftime" = In Progress
    // "Match Finished", "Full Time" = Finished
    // "Match Postponed" = Postponed
    // "Match Cancelled" = Cancelled

    if (statusStr.find("Not Started") != std::string::npos ||
        statusStr.find("Scheduled") != std::string::npos)
        return 0; // NOT_STARTED
    
    if (statusStr.find("In Play") != std::string::npos ||
        statusStr.find("First Half") != std::string::npos ||
        statusStr.find("Second Half") != std::string::npos ||
        statusStr.find("Halftime") != std::string::npos)
        return 1; // IN_PROGRESS
    
    if (statusStr.find("Finished") != std::string::npos ||
        statusStr.find("Full Time") != std::string::npos)
        return 2; // FINISHED
    
    if (statusStr.find("Postponed") != std::string::npos)
        return 3; // POSTPONED
    
    if (statusStr.find("Cancelled") != std::string::npos)
        return 4; // CANCELLED

    return 0; // Default to NOT_STARTED
}

uint16_t TheSportsDBProvider::fetchFromAPI(
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

    // Build URL - using lookupevent endpoint with event ID
    // TheSportsDB uses public API key "3" for testing/free tier
    std::ostringstream urlStream;
    urlStream << "https://www.thesportsdb.com/api/v1/json/3/lookupevent.php?id=" << matchId;
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

    // Perform request (no authentication needed!)
    CURLcode res = curl_easy_perform(curl);

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
    // {"events":[{"idEvent":"123","idHomeTeam":"133612","idAwayTeam":"133616",
    //             "intHomeScore":"2","intAwayScore":"1","strStatus":"Match Finished"}]}

    // Check if event exists
    if (response.find("\"events\":null") != std::string::npos ||
        response.find("\"events\":[]") != std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Event not found: " << matchId;
        return RETURN_ERROR_INVALID_ARG;
    }

    // Extract home team ID
    size_t homeTeamPos = response.find("\"idHomeTeam\":\"");
    if (homeTeamPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to parse home team ID";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }
    homeTeamPos += 15; // Length of "\"idHomeTeam\":\""
    size_t homeTeamEnd = response.find("\"", homeTeamPos);
    homeTeamId = std::stoul(response.substr(homeTeamPos, homeTeamEnd - homeTeamPos));

    // Extract away team ID
    size_t awayTeamPos = response.find("\"idAwayTeam\":\"");
    if (awayTeamPos == std::string::npos)
    {
        OM_LOG_ERROR() << "[" << _name << "] Failed to parse away team ID";
        return RETURN_ERROR_ORACLE_UNAVAIL;
    }
    awayTeamPos += 15;
    size_t awayTeamEnd = response.find("\"", awayTeamPos);
    awayTeamId = std::stoul(response.substr(awayTeamPos, awayTeamEnd - awayTeamPos));

    // Extract home score
    size_t homeScorePos = response.find("\"intHomeScore\":\"");
    if (homeScorePos != std::string::npos)
    {
        homeScorePos += 17;
        size_t homeScoreEnd = response.find("\"", homeScorePos);
        std::string homeScoreStr = response.substr(homeScorePos, homeScoreEnd - homeScorePos);
        homeScore = homeScoreStr.empty() ? -1 : std::stoi(homeScoreStr);
    }
    else
    {
        homeScore = -1;
    }

    // Extract away score
    size_t awayScorePos = response.find("\"intAwayScore\":\"");
    if (awayScorePos != std::string::npos)
    {
        awayScorePos += 17;
        size_t awayScoreEnd = response.find("\"", awayScorePos);
        std::string awayScoreStr = response.substr(awayScorePos, awayScoreEnd - awayScorePos);
        awayScore = awayScoreStr.empty() ? -1 : std::stoi(awayScoreStr);
    }
    else
    {
        awayScore = -1;
    }

    // Extract status
    size_t statusPos = response.find("\"strStatus\":\"");
    if (statusPos != std::string::npos)
    {
        statusPos += 14;
        size_t statusEnd = response.find("\"", statusPos);
        std::string statusStr = response.substr(statusPos, statusEnd - statusPos);
        status = parseMatchStatus(statusStr);
    }
    else
    {
        status = 0; // Default to NOT_STARTED
    }

    // For finished matches, set elapsed to 90 minutes
    // TheSportsDB doesn't provide real-time elapsed minutes
    if (status == 2) // FINISHED
    {
        elapsedMinutes = 90;
    }
    else if (status == 1) // IN_PROGRESS
    {
        elapsedMinutes = 45; // Estimate
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
