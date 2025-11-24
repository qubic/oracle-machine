#include "request_handler.h"
#include "logger.h"

#include "qpi_adapter.h"

#include <cstring>
#include <iostream>

namespace oracle
{

RequestHandler::RequestHandler(
    std::map<uint32_t, std::unique_ptr<InterfaceClient>>& clients) :
    _interfaceClients(clients)
{
}

std::vector<uint8_t> RequestHandler::handle(
    const RequestResponseHeader& header,
    const uint8_t* payload,
    int payloadSize)
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

    // Parse the query
    OracleMachineQuery query;
    memcpy(&query, payload, sizeof(query));

    // Get any additional query data after the OracleMachineQuery
    const uint8_t* queryData = payload + sizeof(OracleMachineQuery);
    int queryDataSize = payloadSize - sizeof(OracleMachineQuery);

    return handleQuery(query, queryData, queryDataSize);
}

std::vector<uint8_t> RequestHandler::handleQuery(
    const OracleMachineQuery& query,
    const uint8_t* queryData,
    int queryDataSize)
{
    OM_LOG_DEBUG() << "OracleMachineQuery:"
              << " - oracleInterfaceIndex: " << query.oracleInterfaceIndex
              << " - oracleQueryId: " << query.oracleQueryId
              << " - timeoutInSeconds: " << query.timeoutInSeconds;
    
    // Route to appropriate oracle interface handler
    if (query.oracleInterfaceIndex == OI::Price::oracleInterfaceIndex)
    {
        return handlePriceQuery(query, queryData, queryDataSize);
    }
    
    // Unknown oracle interface
    OM_LOG_ERROR() << "Unsupported oracle interface index: " << query.oracleInterfaceIndex;
    return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
}

std::vector<uint8_t> RequestHandler::handlePriceQuery(
    const OracleMachineQuery& query,
    const uint8_t* queryData,
    int queryDataSize)
{
    // Find interface client for this interface type
    auto it = _interfaceClients.find(query.oracleInterfaceIndex);
    if (it == _interfaceClients.end())
    {
        OM_LOG_ERROR() << "Interface client not found for index: " << query.oracleInterfaceIndex;
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
    }
    
    OM_LOG_DEBUG() << "Routing query to InterfaceClient[" << query.oracleInterfaceIndex << "]";
    
    // Send full query to interface client
    // For Price interface: queryData contains Price::OracleQuery (104 bytes)
    // Reply will be Price::OracleReply (16 bytes)
    // TODO: consider mutiple requests, currently for simplicity only single request is supported
    // and it is a blocking wait for the reply. In the future we may want to support async requests.
    std::vector<uint8_t> replyData(16);  // Price::OracleReply size
    
    // TODO: timeout need to recalculated base in time received in OM?
    int timeout_ms = query.timeoutInSeconds * 1000;
    if (!it->second->query(
            queryData, queryDataSize,
            replyData.data(), replyData.size(),
            timeout_ms))
    {
        OM_LOG_ERROR() << "Query to InterfaceClient[" << query.oracleInterfaceIndex << "] failed";
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_TIMEOUT);
    }
    
    OM_LOG_DEBUG() << "InterfaceClient[" << query.oracleInterfaceIndex << "] query successful";
    
    // Return success response with reply data
    return makeResponse(
        query.oracleQueryId,
        ORACLE_FLAG_REPLY_RECEIVED,
        replyData.data(),
        replyData.size());
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