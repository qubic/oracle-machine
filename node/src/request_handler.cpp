#include "request_handler.h"
#include "om_common/config.h"
#include "om_common/logger.h"
#include "oracle_cache.h"

#include "om_common/qpi_adapter.h"

#include <cstring>
#include <iostream>

namespace oracle
{

// TODO: move this into OracleMachine
static OracleCache g_cache;

RequestHandler::RequestHandler(std::map<uint32_t, std::unique_ptr<InterfaceClient>>& clients) :
    _interfaceClients(clients)
{
}

std::vector<uint8_t>
RequestHandler::handle(const RequestResponseHeader& header, const uint8_t* payload, int payloadSize)
{
    // Verify this is an OracleMachineQuery
    if (header.type() != OracleMachineQuery::type)
    {
        OM_LOG_ERROR() << "RequestHandler: Unknown request type " << (int)header.type();
        return std::vector<uint8_t>(); // Empty response for unknown types
    }

    // Check minimum payload size for OracleMachineQuery
    if (payloadSize < (int)sizeof(OracleMachineQuery))
    {
        OM_LOG_ERROR() << "RequestHandler: Payload size mismatched " << payloadSize
                       << " (expected at least " << sizeof(OracleMachineQuery) << ")";
        return std::vector<uint8_t>();
    }

    // TODO: remove payloadSize because size already contained in RequestResponseHeader
    std::vector<uint8_t> completePacket(sizeof(RequestResponseHeader) + payloadSize);

    // Copy header (8 bytes)
    memcpy(completePacket.data(), &header, sizeof(RequestResponseHeader));

    // Copy entire payload (OracleMachineQuery + Price::OracleQuery = 120 bytes)
    memcpy(completePacket.data() + sizeof(RequestResponseHeader), payload, payloadSize);

    OracleMachineQuery query;
    memcpy(&query, &completePacket[0] + sizeof(RequestResponseHeader), sizeof(OracleMachineQuery));

    // Get any additional query data after the OracleMachineQuery
    OM_LOG_DEBUG() << "OracleMachineQuery:"
                   << " - header.type: " << header.type() << " - header.size: " << header.size()
                   << " - query.dejavu: " << header.dejavu();
    OM_LOG_DEBUG() << "OracleMachineQuery:"
                   << " - oracleInterfaceIndex: " << query.oracleInterfaceIndex
                   << " - oracleQueryId: " << query.oracleQueryId
                   << " - timeoutInMilliSeconds: " << query.timeoutInMilliseconds;

    // Check if the OracleMachineQuery
    OracleCache::CachedData cached_data;
    if (g_cache.lookup(query, cached_data))
    {
        OM_LOG_DEBUG() << "Cache HIT - get data from cache.";
        // TODO: check error flag ?
        return makeResponse(
            query.oracleQueryId,
            cached_data.errorFlag,
            cached_data.payload.data(),
            cached_data.payload.size());
    }
    else // miss cache
    {
        // Send full query to interface client and get full package {RequestRespondHeader +
        // OracleReply + payload}
        std::vector<uint8_t> replyFullData;

        OM_LOG_DEBUG() << "Cache MISS - get data from service.";

        // Find interface client for this interface type
        auto it = _interfaceClients.find(query.oracleInterfaceIndex);
        if (it == _interfaceClients.end())
        {
            OM_LOG_ERROR() << "Interface client not found for index: "
                           << query.oracleInterfaceIndex;
            return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
        }

        // TODO: timeout need to recalculated base in time received in OM ?
        int timeout_ms = query.timeoutInMilliseconds;
        if (!it->second->query(
                (uint8_t*)&completePacket[0], completePacket.size(), replyFullData, timeout_ms))
        {
            OM_LOG_ERROR() << "Query to InterfaceClient[" << query.oracleInterfaceIndex
                           << "] failed";
            return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_TIMEOUT);
        }

        // Expected reply size is at least OracleMachineReply
        if (replyFullData.size() < sizeof(OracleMachineReply) + sizeof(RequestResponseHeader))
        {
            OM_LOG_ERROR() << "InterfaceClient[" << query.oracleInterfaceIndex
                           << "] reply size too small: " << replyFullData.size();
            return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
        }

        // Make sure reply ID matches query ID
        OracleMachineReply reply;
        memcpy(
            &reply,
            replyFullData.data() + sizeof(RequestResponseHeader),
            sizeof(OracleMachineReply));
        if (reply.oracleQueryId != query.oracleQueryId)
        {
            OM_LOG_ERROR() << "InterfaceClient[" << query.oracleInterfaceIndex
                           << "] reply ID mismatch: " << reply.oracleQueryId << " (expected "
                           << query.oracleQueryId << ")";
            return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
        }

        // Prepare payload
        std::vector<uint8_t> replyPayload(
            replyFullData.size() - sizeof(RequestResponseHeader) - sizeof(OracleMachineReply));
        memcpy(
            replyPayload.data(),
            replyFullData.data() + sizeof(RequestResponseHeader) + sizeof(OracleMachineReply),
            replyPayload.size());

        // Cache the payload
        // TODO: verify reply.oracleMachineErrorFlags
        g_cache.store(
            query, replyPayload.data(), replyPayload.size(), reply.oracleMachineErrorFlags);

        // Return success response with reply data
        return makeResponse(
            query.oracleQueryId,
            ORACLE_FLAG_REPLY_RECEIVED,
            replyPayload.data(),
            replyPayload.size());
    }
}

std::vector<uint8_t> RequestHandler::makeResponse(
    uint64_t query_id,
    uint16_t error_flags,
    const uint8_t* data,
    int data_size)
{
    // Build OracleMachineReply + data
    std::vector<uint8_t> result(sizeof(OracleMachineReply) + data_size);

    OracleMachineReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.oracleQueryId = query_id;
    reply.oracleMachineErrorFlags = error_flags;

    memcpy(result.data(), &reply, sizeof(reply));

    if (data && data_size > 0)
    {
        memcpy(result.data() + sizeof(reply), data, data_size);
    }

    return result;
}

std::vector<uint8_t> RequestHandler::makeErrorResponse(uint64_t queryID, uint16_t errorFlags)
{
    return makeResponse(queryID, errorFlags, nullptr, 0);
}

} // namespace oracle