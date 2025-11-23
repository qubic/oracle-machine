#pragma once

#include "network_messages/header.h"
#include "oracle_client.h"
#include "oracle_core/core_om_network_messages.h"
#include "oracle_data.h"

#include <memory>
#include <string>
#include <vector>

namespace oracle
{

class RequestHandler
{
public:
    RequestHandler(std::map<std::string, std::unique_ptr<OracleClient>>& clients);
    ~RequestHandler() = default;

    // Handle a request and return response payload (OracleMachineReply + data)
    std::vector<uint8_t>
    handle(const RequestResponseHeader& header, const uint8_t* payload, int payload_size);

private:
    // Handle query based on interface index
    std::vector<uint8_t>
    handleQuery(const OracleMachineQuery& query, const uint8_t* query_data, int query_data_size);

    // Build response with data
    std::vector<uint8_t>
    makeResponse(uint64_t query_id, uint16_t error_flags, const uint8_t* data, int data_size);

    // Build error reply
    std::vector<uint8_t> makeErrorResponse(uint64_t query_id, uint16_t error_flags);

    std::map<std::string, std::unique_ptr<OracleClient>>& _clients;
};

} // namespace oracle