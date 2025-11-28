#include "request_handler.h"
#include "config.h"
#include "logger.h"

#include "qpi_adapter.h"

#include <cstring>
#include <iostream>

namespace oracle
{

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
                   << " - timeoutInSeconds: " << query.timeoutInSeconds;

    struct Price
    {
        // uint32_t oracleInterfaceIndex;
        struct OracleQuery // size limited by tx size
        {
            uint8_t oracleId[32];  // a source for getting the information, e.g. coingecko ->
                                   // string-like similar to asset-name, m256i (string of 32 bytes)
            uint8_t timestamp[8];  // timestamp of response value, required for supporting
                                   // subscription because it is set by the scheduler (if not
                                   // provided compile error if subscription is tried)
            uint8_t currency1[32]; // Type how to reference currencies is unclear -> enum-like,
                                   // string-like similar to asset-name, m256i
            uint8_t currency2[32];
        } query;
        struct OracleReply // size limited by tx size
        {
            uint64_t numerator;    // at query.timestamp, currency1 = currency2 * numerator / denominator
            uint64_t denominator;
        } reply;

    } priceData;
    memcpy(
        &priceData.query,
        &completePacket[0] + sizeof(RequestResponseHeader) + sizeof(OracleMachineQuery),
        sizeof(priceData.query));
    OM_LOG_DEBUG() << "priceData:"
                   << " - oracleId: " << priceData.query.oracleId
                   << " - currency1: " << priceData.query.currency1
                   << " - currency2: " << priceData.query.currency2;

    // Find interface client for this interface type
    auto it = _interfaceClients.find(query.oracleInterfaceIndex);
    if (it == _interfaceClients.end())
    {
        OM_LOG_ERROR() << "Interface client not found for index: " << query.oracleInterfaceIndex;
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
    }

    OM_LOG_DEBUG() << "Routing query to InterfaceClient[" << query.oracleInterfaceIndex << "]";

    // Send full query to interface client
    std::vector<uint8_t> replyData;

    // TODO: timeout need to recalculated base in time received in OM ?
    int timeout_ms = query.timeoutInSeconds * 1000;
    if (!it->second->query(
            (uint8_t*)&completePacket[0], completePacket.size(), replyData, timeout_ms))
    {
        OM_LOG_ERROR() << "Query to InterfaceClient[" << query.oracleInterfaceIndex << "] failed";
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_TIMEOUT);
    }

    // Verify reply

    // Expected reply size is at least OracleMachineReply
    if (replyData.size() < sizeof(OracleMachineReply))
    {
        OM_LOG_ERROR() << "InterfaceClient[" << query.oracleInterfaceIndex
                       << "] reply size too small: " << replyData.size();
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
    }

    // Make sure reply ID matches query ID
    OracleMachineReply reply;
    memcpy(&reply, replyData.data() + sizeof(RequestResponseHeader), sizeof(OracleMachineReply));
    if (reply.oracleQueryId != query.oracleQueryId)
    {
        OM_LOG_ERROR() << "InterfaceClient[" << query.oracleInterfaceIndex
                       << "] reply ID mismatch: " << reply.oracleQueryId << " (expected "
                       << query.oracleQueryId << ")";
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ARG);
    }

    memcpy(
        &priceData.reply,
        replyData.data() + sizeof(RequestResponseHeader) + sizeof(OracleMachineReply),
        sizeof(priceData.reply));
    OM_LOG_DEBUG() << "priceData:"
                   << " - numerator: " << priceData.reply.numerator
                   << " - denominator: " << priceData.reply.denominator;

    OM_LOG_DEBUG() << "InterfaceClient[" << query.oracleInterfaceIndex << "] query successful";

    // Return success response with reply data
    return makeResponse(
        query.oracleQueryId, ORACLE_FLAG_REPLY_RECEIVED, replyData.data(), replyData.size());
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