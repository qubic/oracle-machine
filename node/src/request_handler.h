#pragma once

#include "network_messages/header.h"
#include "interface_client.h"
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
    RequestHandler(std::map<uint32_t, std::unique_ptr<InterfaceClient>>& clients);
    ~RequestHandler() = default;

    // Handle a request and return response payload (OracleMachineReply + data)
    std::vector<uint8_t>
    handle(const RequestResponseHeader& header, const uint8_t* payload, int payloadSize);

private:
    // Build response with data
    std::vector<uint8_t>
    makeResponse(uint64_t query_id, uint16_t errorFlags, const uint8_t* data, int data_size);

    // Build error reply
    std::vector<uint8_t> makeErrorResponse(uint64_t queryID, uint16_t errorFlags);

    std::map<uint32_t, std::unique_ptr<InterfaceClient>>& _interfaceClients;
};

} // namespace oracle