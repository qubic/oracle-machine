#pragma once

#include "network_messages/header.h"
#include "oracle_core/core_om_network_messages.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oracle
{

constexpr size_t ORACLE_MACHINE_QUERY_SIZE = sizeof(OracleMachineQuery);
constexpr size_t ORACLE_MACHINE_REPLY_SIZE = sizeof(OracleMachineReply);
constexpr size_t REQUEST_RESPONSE_HEADER_SIZE = sizeof(RequestResponseHeader);

struct QueryPacket
{
    RequestResponseHeader header;
    OracleMachineQuery query;
    std::vector<uint8_t> payload; // Interface-specific query payload

    QueryPacket() = default;
};

struct ReplyPacket
{
    RequestResponseHeader header;
    OracleMachineReply reply;
    std::vector<uint8_t> payload; // Interface-specific reply payload

    ReplyPacket() { reply.oracleMachineErrorFlags = ORACLE_FLAG_REPLY_PENDING; }
};

class TcpServer;
class Session;
// ============================================================================
// Base Oracle Service
// ============================================================================

/**
 * Abstract base class for Oracle Machine services.
 *
 * Handles protocol-level operations (header, query, reply parsing).
 * Concrete services (Price, Weather, etc.) implement interface-specific logic.
 *
 * Usage:
 *   1. Inherit from BaseOracleService
 *   2. Implement processInterfaceQuery()
 *   3. Use start for startting the service
 *   4. Use waitForShutdown for stopping with signal
 */
class BaseOracleService
{
public:
    BaseOracleService(
        const std::string& hostName,
        const uint16_t hostPort,
        const std::string& serviceName,
        size_t interfaceQuerySize,
        size_t interfaceReplySize);

    virtual ~BaseOracleService();

    // Start the service
    int start();

    // Stop
    int stop();

    void waitForShutdown();

    // Service info
    const std::string& getServiceName() const { return _serviceName; }
    size_t getQueryPacketSize() const { return _queryPacketSize; }
    size_t getReplyPacketSize() const { return _replyPacketSize; }

    // Statistics
    struct Statistics
    {
        uint64_t totalQueries = 0;
        uint64_t successful = 0;
        uint64_t failed = 0;
    };

    Statistics getStatistics() const;
    void printStatistics() const;

protected:
    /**
     * Parse and process a query.
     *
     * @param queryPayload Interface-specific query payload
     * @param replyPayload Output buffer for reply payload

// Some portable functions from QPI
uint64_t timestampToUnixSeconds(const QPI::DateAndTime& timestamp);
     * @return error_flags (0 for success, non-zero for error)
     *
     * This method should:
     * 1. Parse the query payload
     * 2. Process the query (fetch data, compute result, etc.)
     * 3. Build the reply payload
     *
     **/
    virtual uint16_t processInterfaceQuery(
        const std::vector<uint8_t>& queryPayload,
        std::vector<uint8_t>& replyPayload) = 0;

private:
    // Session handler (call this from TcpServer::setSessionHandler)
    void handleSession(Session& session);

    // Protocol parsing
    bool parseQueryPacket(const uint8_t* data, size_t size, QueryPacket& packet);
    std::vector<uint8_t> buildReplyPacket(
        const QueryPacket& query,
        uint16_t errorFlags,
        const uint8_t* payload,
        size_t payloadSize);

    // Process single query
    bool processQuery(const QueryPacket& query, ReplyPacket& reply);

    // Service configuration
    std::string _serviceName;
    size_t _interfaceQuerySize;
    size_t _interfaceReplySize;
    size_t _queryPacketSize;
    size_t _replyPacketSize;

    // Statistics
    std::atomic<uint64_t> _totalQueries{0};
    std::atomic<uint64_t> _successful{0};
    std::atomic<uint64_t> _failed{0};

    // Network related
    std::unique_ptr<TcpServer> _server;
};


} // namespace oracle