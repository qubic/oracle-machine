#include "request_handler.h"
#include "oracle_client.h"
#include <cstring>
#include <iostream>

namespace oracle
{

RequestHandler::RequestHandler(std::map<std::string, std::unique_ptr<OracleClient>>& clients) :
    _clients(clients)
{
}

std::vector<uint8_t> RequestHandler::handle(
    const RequestResponseHeader& header,
    const uint8_t* payload,
    int payload_size)
{
    // Verify this is an OracleMachineQuery
    if (header.type() != OracleMachineQuery::type)
    {
        return makeErrorResponse(0, ORACLE_QUERY_STATUS_UNKNOWN);
    }

    // Check minimum payload size for OracleMachineQuery
    if (payload_size < (int)sizeof(OracleMachineQuery))
    {
        return makeErrorResponse(0, ORACLE_QUERY_STATUS_UNKNOWN);
    }

    // Parse the query
    OracleMachineQuery query;
    memcpy(&query, payload, sizeof(query));

    // Get any additional query data after the OracleMachineQuery struct
    const uint8_t* query_data = payload + sizeof(OracleMachineQuery);
    int query_data_size = payload_size - sizeof(OracleMachineQuery);

    return handleQuery(query, query_data, query_data_size);
}

// TODO: verify the return value and error handling
std::vector<uint8_t> RequestHandler::handleQuery(
    const OracleMachineQuery& query,
    const uint8_t* query_data,
    int query_data_size)
{
    std::cout << "OracleMachineQuery:"
              << " - oracleInterfaceIndex: " << query.oracleInterfaceIndex
              << " - type: " << query.type << " - oracleQueryId: " << query.oracleQueryId
              << " - timeoutInSeconds: " << query.timeoutInSeconds << std::endl;

    // Find oracle by interface index or by oracle_id in query data
    std::string oracleID;

    // If oracle_id was passed as query data, use that
    if (query_data_size > 0 && query_data_size <= 32)
    {
        char idBuffer[33];
        memset(idBuffer, 0, sizeof(idBuffer));
        memcpy(idBuffer, query_data, query_data_size);
        oracleID = idBuffer;
    }
    else
    {
        // Get oracle_id by interface index
        unsigned int index = 0;
        for (const auto& pair : _clients)
        {
            if (index == query.oracleInterfaceIndex)
            {
                oracleID = pair.first;
                break;
            }
            index++;
        }
    }

    if (oracleID.empty())
    {
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ORACLE);
    }

    // Find the client
    auto it = _clients.find(oracleID);
    if (it == _clients.end())
    {
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ORACLE);
    }

    // TODO: hack around, the OracleClient fetch data will take a long time here when pressing Ctrl+C
    //return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_INVALID_ORACLE);

    // Fetch data from Oracle
    std::cout << "[" << oracleID << "] fetching data ..." << std::endl;
    OracleData data;
    if (!it->second->fetch(data))
    {
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_ORACLE_UNAVAIL);
    }

    if (!data.valid)
    {
        return makeErrorResponse(query.oracleQueryId, ORACLE_FLAG_ORACLE_UNAVAIL);
    }

    std::cout << "[" << oracleID << "] Fetched on-demand: value=" << data.value
              << ", timestamp=" << data.timestamp << std::endl;

    // Build reply data: oracle_id (32 bytes) + value (8 bytes) + timestamp (8 bytes)
    uint8_t reply_data[48];
    memset(reply_data, 0, sizeof(reply_data));

    // Copy oracle_id (32 bytes)
    strncpy((char*)reply_data, data.oracleId.c_str(), 31);

    // Copy value (8 bytes, double)
    memcpy(reply_data + 32, &data.value, sizeof(double));

    // Copy timestamp (8 bytes, int64_t)
    memcpy(reply_data + 40, &data.timestamp, sizeof(int64_t));

    return makeResponse(
        query.oracleQueryId, ORACLE_FLAG_REPLY_RECEIVED, reply_data, sizeof(reply_data));
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

    if (data_size > 0 && data != nullptr)
    {
        memcpy(result.data() + sizeof(OracleMachineReply), data, data_size);
    }

    return result;
}

std::vector<uint8_t> RequestHandler::makeErrorResponse(uint64_t query_id, uint16_t error_flags)
{
    return makeResponse(query_id, error_flags, nullptr, 0);
}

} // namespace oracle