#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace oracle
{

/**
 * Football Provider Interface
 * 
 * Provides match data for football/soccer matches from various sources.
 */
class FootballProvider
{
public:
    FootballProvider(const std::string& name) : _name(name) {}
    virtual ~FootballProvider() = default;

    /**
     * Get match data
     * 
     * @param matchId Match identifier
     * @param leagueId League identifier
     * @param season Season year
     * @param homeTeamId Output: home team ID
     * @param awayTeamId Output: away team ID
     * @param homeScore Output: home team score (-1 if not started)
     * @param awayScore Output: away team score (-1 if not started)
     * @param status Output: match status (0-4)
     * @param elapsedMinutes Output: elapsed minutes
     * @return RETURN_NO_ERROR on success, error code otherwise
     */
    virtual uint16_t getMatchData(
        uint32_t matchId,
        uint32_t leagueId,
        uint32_t season,
        uint32_t& homeTeamId,
        uint32_t& awayTeamId,
        int32_t& homeScore,
        int32_t& awayScore,
        uint8_t& status,
        uint8_t& elapsedMinutes) = 0;

    const std::string& getName() const { return _name; }

protected:
    std::string _name;
};

/**
 * Mock Football Provider (for testing)
 */
class MockFootballProvider : public FootballProvider
{
public:
    MockFootballProvider();

    uint16_t getMatchData(
        uint32_t matchId,
        uint32_t leagueId,
        uint32_t season,
        uint32_t& homeTeamId,
        uint32_t& awayTeamId,
        int32_t& homeScore,
        int32_t& awayScore,
        uint8_t& status,
        uint8_t& elapsedMinutes) override;

    // Set mock match data
    void setMatchData(
        uint32_t matchId,
        uint32_t homeTeamId,
        uint32_t awayTeamId,
        int32_t homeScore,
        int32_t awayScore,
        uint8_t status,
        uint8_t elapsedMinutes);

private:
    struct MatchData
    {
        uint32_t homeTeamId;
        uint32_t awayTeamId;
        int32_t homeScore;
        int32_t awayScore;
        uint8_t status;
        uint8_t elapsedMinutes;
    };

    std::map<uint32_t, MatchData> _matches;
    std::mutex _mutex;
};

/**
 * API-Football Provider
 * 
 * Uses api-football.com free tier API
 */
class ApiFootballProvider : public FootballProvider
{
public:
    ApiFootballProvider(const std::string& apiKey = "");

    uint16_t getMatchData(
        uint32_t matchId,
        uint32_t leagueId,
        uint32_t season,
        uint32_t& homeTeamId,
        uint32_t& awayTeamId,
        int32_t& homeScore,
        int32_t& awayScore,
        uint8_t& status,
        uint8_t& elapsedMinutes) override;

private:
    struct CacheEntry
    {
        uint32_t homeTeamId;
        uint32_t awayTeamId;
        int32_t homeScore;
        int32_t awayScore;
        uint8_t status;
        uint8_t elapsedMinutes;
        time_t timestamp;
    };

    std::string _apiKey;
    std::map<uint32_t, CacheEntry> _cache;
    std::mutex _cacheMutex;
    std::mutex _rateLimitMutex;
    time_t _lastRequestTime;

    static constexpr int CACHE_TTL = 60;            // 60 seconds
    static constexpr double RATE_LIMIT_DELAY = 2.0; // 2 seconds between requests

    uint16_t fetchFromAPI(
        uint32_t matchId,
        uint32_t leagueId,
        uint32_t season,
        uint32_t& homeTeamId,
        uint32_t& awayTeamId,
        int32_t& homeScore,
        int32_t& awayScore,
        uint8_t& status,
        uint8_t& elapsedMinutes);

    uint8_t parseMatchStatus(const std::string& statusShort, const std::string& statusLong);
};

} // namespace oracle
