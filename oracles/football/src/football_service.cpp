#include "football_service.h"
#include "om_common/logger.h"

#include <algorithm>
#include <cstring>

namespace oracle
{

FootballService::FootballService(const std::string& rHostname, uint16_t hostPort) :
    BaseOracleService(
        rHostname,
        hostPort,
        "Football",
        FOOTBALL_ORACLE_QUERY_SIZE,
        FOOTBALL_ORACLE_REPLY_SIZE)
{
    // Register default providers
    registerProvider("mock", std::make_shared<MockFootballProvider>());

    // Register TheSportsDB provider (no authentication required!)
    registerProvider("thesportsdb", std::make_shared<TheSportsDBProvider>());
}

FootballService::~FootballService()
{
    printStatistics();
}

void FootballService::registerProvider(
    const std::string& oracleId,
    std::shared_ptr<FootballProvider> provider)
{
    std::lock_guard<std::mutex> lock(_providersMutex);

    std::string lowerOracleId = oracleId;
    std::transform(lowerOracleId.begin(), lowerOracleId.end(), lowerOracleId.begin(), ::tolower);

    _providers[lowerOracleId] = provider;

    OM_LOG_INFO() << "[Football] Registered provider: " << oracleId << " (" << provider->getName()
                  << ")";
}

std::string FootballService::bytesToString(const char* data, size_t maxLen)
{
    // Find null terminator or end of buffer
    size_t len = 0;
    while (len < maxLen && data[len] != '\0')
    {
        len++;
    }
    return std::string(data, len);
}

uint16_t FootballService::processInterfaceQuery(
    const std::vector<uint8_t>& queryPayload,
    std::vector<uint8_t>& replyPayload)
{
    // Parse the query
    if (queryPayload.size() < FOOTBALL_ORACLE_QUERY_SIZE)
    {
        OM_LOG_ERROR() << "[Football] Invalid query size: " << queryPayload.size();
        return RETURN_ERROR_INVALID_ARG;
    }

    Football::OracleQuery query;
    std::memcpy(&query, queryPayload.data(), sizeof(Football::OracleQuery));

    // Extract oracle ID
    std::string oracleId = bytesToString((const char*)query.oracle.m256i_i8, 32);

    OM_LOG_DEBUG() << "  Query: oracle=" << oracleId 
                   << ", matchId=" << query.matchId
                   << ", leagueId=" << query.leagueId
                   << ", season=" << query.season;

    // Look for provider
    std::transform(oracleId.begin(), oracleId.end(), oracleId.begin(), ::tolower);

    std::shared_ptr<FootballProvider> provider;
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
        OM_LOG_ERROR() << "[Football] Unknown oracle provider: " << oracleId;
        return RETURN_ERROR_INVALID_ORACLE;
    }

    // Get match data
    Football::OracleReply reply;
    std::memset(&reply, 0, sizeof(reply));

    uint16_t returnValue = provider->getMatchData(
        query.matchId,
        query.leagueId,
        query.season,
        reply.homeTeamId,
        reply.awayTeamId,
        reply.homeScore,
        reply.awayScore,
        reply.status,
        reply.elapsedMinutes);

    if (returnValue != RETURN_NO_ERROR)
    {
        return returnValue;
    }

    OM_LOG_DEBUG() << "  Result: match " << query.matchId
                   << " (" << reply.homeTeamId << " vs " << reply.awayTeamId << ") "
                   << reply.homeScore << "-" << reply.awayScore
                   << " status=" << (int)reply.status;

    // Build the reply
    replyPayload.resize(sizeof(Football::OracleReply));
    std::memcpy(replyPayload.data(), &reply, sizeof(Football::OracleReply));

    return RETURN_NO_ERROR;
}

} // namespace oracle
