#include "doge_service.h"
#include "scrypt.h"
#include "om_common/logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <thread>

namespace oracle
{

DogeService::DogeService(const std::string& rHostname, uint16_t hostPort) :
    BaseOracleService(
        rHostname,
        hostPort,
        "Doge",
        DOGE_ORACLE_QUERY_SIZE,
        DOGE_ORACLE_REPLY_SIZE)
{
    
}

DogeService::~DogeService()
{
    printStatistics();
}

static bool verifyHashVsTarget(const std::array<uint8_t, 32>& hash, const std::array<uint8_t, 32>& target)
{
    // Start from the most significant byte (the end of the array).
    for (int i = 31; i >= 0; --i)
    {
        if (hash[i] < target[i])
            return true; // hash is smaller
        if (hash[i] > target[i])
            return false; // hash is larger
    }
    return true; // exactly equal
}

// ============================================================================
// BaseOracleService Implementation
uint16_t DogeService::processInterfaceQuery(
    const std::vector<uint8_t>& queryPayload,
    std::vector<uint8_t>& replyPayload)
{
    // Parse the query
    if (queryPayload.size() < DOGE_ORACLE_QUERY_SIZE)
    {
        OM_LOG_ERROR() << "[Doge] Invalid query size: " << queryPayload.size();
        return RETURN_ERROR_INVALID_ARG;
    }

    // Prepare the output struct
    replyPayload.resize(sizeof(DogeShareValidation::OracleReply));

    // Provide typed access to payload of query and reply
    const auto* query = reinterpret_cast<const DogeShareValidation::OracleQuery*>(queryPayload.data());
    auto* reply = reinterpret_cast<DogeShareValidation::OracleReply*>(replyPayload.data());

    // Compute hash
    std::array<uint8_t, 32> hash;
    scrypt_1024_1_1_256(reinterpret_cast<char*>(&query), reinterpret_cast<char*>(hash.data()));

    // Compare hash with target difficulty
    const std::array<uint8_t, 32>& target =
        *reinterpret_cast<const std::array<uint8_t, 32>*>(&query->target);
    reply->isValid = verifyHashVsTarget(hash, target);

    OM_LOG_DEBUG() << "  Result: doge share is " << (reply->isValid ? "valid" : "invalid");

    return 0;
}


}