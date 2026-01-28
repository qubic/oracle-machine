#include "mock_service.h"
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

MockService::MockService(const std::string& rHostname, uint16_t hostPort) :
    BaseOracleService(
        rHostname,
        hostPort,
        "Mock",
        MOCK_ORACLE_QUERY_SIZE,
        MOCK_ORACLE_REPLY_SIZE)
{
    
}

MockService::~MockService()
{
    printStatistics();
}

// ============================================================================
// BaseOracleService Implementation
uint16_t MockService::processInterfaceQuery(
    const std::vector<uint8_t>& queryPayload,
    std::vector<uint8_t>& replyPayload)
{
    // Parse the querry
    if (queryPayload.size() < MOCK_ORACLE_QUERY_SIZE)
    {
        OM_LOG_ERROR() << "[Mock] Invalid query size: " << queryPayload.size();
        return 1; // Parse error
    }

    Mock::OracleQuery query;
    std::memcpy(&query, queryPayload.data(), sizeof(Mock::OracleQuery));

    // Extract strings
    uint64_t value = query.value;

    // Get mock value
    Mock::OracleReply reply;
    reply.echoedValue = value;
    reply.doubledValue = 2 * value;
    OM_LOG_DEBUG() << "  Result: value =" << value 
        << ", reply = (" << reply.echoedValue << ", " << reply.doubledValue << ")";

    // Build the reply
    replyPayload.resize(sizeof(Mock::OracleReply));
    std::memcpy(replyPayload.data(), &reply, sizeof(Mock::OracleReply));

    return 0;
}


}