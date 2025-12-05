#include "base_oracle_service.h"
#include "om_common/logger.h"
#include "om_network/session.h"
#include "om_network/tcp_server.h"

#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>

namespace oracle
{

std::atomic<bool> gRunning(true);

void signalHandler(int signal)
{
    std::cout << "\n[BaseOracleService] Received signal " << signal << " - shutting down..."
              << std::endl;
    gRunning = false;
}

BaseOracleService::BaseOracleService(
    const std::string& hostName,
    const uint16_t hostPort,
    const std::string& serviceName,
    size_t interfaceQuerySize,
    size_t interfaceReplySize) :
    _serviceName(serviceName),
    _interfaceQuerySize(interfaceQuerySize),
    _interfaceReplySize(interfaceReplySize)
{
    // Calculate packet sizes
    _queryPacketSize =
        REQUEST_RESPONSE_HEADER_SIZE + ORACLE_MACHINE_QUERY_SIZE + interfaceQuerySize;

    _replyPacketSize =
        REQUEST_RESPONSE_HEADER_SIZE + ORACLE_MACHINE_REPLY_SIZE + interfaceReplySize;

    OM_LOG_INFO() << "[" << _serviceName << "] Service initialized";
    OM_LOG_INFO() << "  Query packet size: " << _queryPacketSize << " bytes";
    OM_LOG_INFO() << "  Reply packet size: " << _replyPacketSize << " bytes";

    _server = std::make_unique<TcpServer>(hostName, hostPort);

    // Set session handler
    _server->setSessionHandler([this](Session& session) { handleSession(session); });
}

BaseOracleService::~BaseOracleService() {}

int BaseOracleService::start()
{
    // Setup signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Start server
    std::cout << "[BaseOracleService] Starting TCP server..." << std::endl;

    if (!_server->start())
    {
        std::cerr << "[BaseOracleService] Failed to start server" << std::endl;
        return 1;
    }

    std::cout << "[BaseOracleService] Server started successfully" << std::endl;
    std::cout << "[BaseOracleService] Listening on " << _server->getBindAddress() << ":" << _server->getPort() << std::endl;
    std::cout << "[BaseOracleService] Press Ctrl+C to stop\n" << std::endl;

    return 0;
}

int BaseOracleService::stop()
{
    _server->stop();

    printStatistics();

    std::cout << "[BaseOracleService] Shutdown complete" << std::endl;
    return 0;
}

// Wait for shutdown signal
void BaseOracleService::waitForShutdown()
{
    while (gRunning.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop();
}

void BaseOracleService::handleSession(Session& session)
{
    OM_LOG_INFO() << "\n[" << _serviceName << "] Client connected: " << session.getRemoteIP() << ":"
                  << session.getRemotePort();

    // Allocate receive buffer
    std::vector<uint8_t> buffer(_queryPacketSize);

    while (session.isActive())
    {
        // Receive query packet
        int received = session.receiveExact(buffer.data(), _queryPacketSize);

        if (received <= 0)
        {
            // Connection closed or error
            if (received < 0)
            {
                std::cerr << "[" << _serviceName << "] Receive error";
            }
            break;
        }

        if (received != static_cast<int>(_queryPacketSize))
        {
            std::cerr << "[" << _serviceName << "] Incomplete packet: " << received
                      << " bytes (expected " << _queryPacketSize << ")";
            continue;
        }

        // Parse query packet
        QueryPacket query;
        if (!parseQueryPacket(buffer.data(), received, query))
        {
            std::cerr << "[" << _serviceName << "] Failed to parse query packet";
            continue;
        }

        _totalQueries++;

        OM_LOG_INFO() << "[" << _serviceName
                      << "] Query received - QueryId=" << query.query.oracleQueryId
                      << ", Interface=" << query.query.oracleInterfaceIndex;

        // Process query
        ReplyPacket replyPacket;
        bool success = processQuery(query, replyPacket);

        if (success)
        {
            _successful++;
        }
        else
        {
            _failed++;
        }

        // Build reply packet
        auto replyData = buildReplyPacket(
            query, replyPacket.reply.oracleMachineErrorFlags, replyPacket.payload.data(), replyPacket.payload.size());

        // Send reply
        if (!session.sendData(replyData.data(), replyData.size()))
        {
            std::cerr << "[" << _serviceName << "] Failed to send reply";
            break;
        }

        OM_LOG_INFO() << "[" << _serviceName << "] Reply sent - " << replyData.size()
                      << " bytes, ErrorFlags=" << replyPacket.reply.oracleMachineErrorFlags;
    }

    OM_LOG_INFO() << "[" << _serviceName << "] Client disconnected: " << session.getRemoteIP();
}

bool BaseOracleService::parseQueryPacket(const uint8_t* data, size_t size, QueryPacket& packet)
{
    if (size < _queryPacketSize)
    {
        std::cerr << "[" << _serviceName << "] Packet too small: " << size;
        return false;
    }

    size_t offset = 0;

    // Parse RequestResponseHeader (8 bytes)
    std::memcpy(&packet.header, data + offset, REQUEST_RESPONSE_HEADER_SIZE);
    offset += REQUEST_RESPONSE_HEADER_SIZE;

    // Validate header
    if (packet.header.type() != OracleMachineQuery::type)
    {
        std::cerr << "[" << _serviceName
                  << "] Invalid message type: " << static_cast<int>(packet.header.type());
        return false;
    }

    // Parse OracleMachineQuery (16 bytes)
    std::memcpy(&packet.query, data + offset, ORACLE_MACHINE_QUERY_SIZE);
    offset += ORACLE_MACHINE_QUERY_SIZE;

    // Parse interface-specific payload
    size_t payloadSize = size - offset;
    if (payloadSize != _interfaceQuerySize)
    {
        std::cerr << "[" << _serviceName << "] Invalid payload size: " << payloadSize
                  << " (expected " << _interfaceQuerySize << ")";
        return false;
    }

    packet.payload.resize(payloadSize);
    std::memcpy(packet.payload.data(), data + offset, payloadSize);

    return true;
}

std::vector<uint8_t> BaseOracleService::buildReplyPacket(
    const QueryPacket& query,
    uint16_t errorFlags,
    const uint8_t* payload,
    size_t payloadSize)
{
    std::vector<uint8_t> packet(_replyPacketSize);
    size_t offset = 0;

    // Build RequestResponseHeader
    RequestResponseHeader header;
    header.checkAndSetSize(_replyPacketSize);
    header.setType(OracleMachineReply::type);
    header.setDejavu(0);
    std::memcpy(packet.data() + offset, &header, REQUEST_RESPONSE_HEADER_SIZE);
    offset += REQUEST_RESPONSE_HEADER_SIZE;

    // Build OracleMachineReply
    OracleMachineReply reply;
    reply.oracleQueryId = query.query.oracleQueryId;
    reply.oracleMachineErrorFlags = errorFlags;
    std::memcpy(packet.data() + offset, &reply, ORACLE_MACHINE_REPLY_SIZE);
    offset += ORACLE_MACHINE_REPLY_SIZE;

    // Add interface-specific payload
    if (payloadSize != _interfaceReplySize)
    {
        std::cerr << "[" << _serviceName
                  << "] Warning: Reply payload size mismatch: " << payloadSize << " (expected "
                  << _interfaceReplySize << ")";
    }

    std::memcpy(packet.data() + offset, payload, std::min(payloadSize, _interfaceReplySize));

    return packet;
}

// ============================================================================
// Query Processing
// ============================================================================

bool BaseOracleService::processQuery(const QueryPacket& query, ReplyPacket& reply)
{
    // Prepare reply payload buffer
    reply.payload.resize(_interfaceReplySize);

    // Let the service implementation handle everything
    uint16_t errorFlags = processInterfaceQuery(query.payload, reply.payload);

    reply.reply.oracleMachineErrorFlags = errorFlags;

    return true;
}

// ============================================================================
// Statistics
BaseOracleService::Statistics BaseOracleService::getStatistics() const
{
    Statistics stats;
    stats.totalQueries = _totalQueries.load();
    stats.successful = _successful.load();
    stats.failed = _failed.load();
    return stats;
}

void BaseOracleService::printStatistics() const
{
    auto stats = getStatistics();

    OM_LOG_INFO() << "\n============================================================";
    OM_LOG_INFO() << _serviceName << " Service Statistics";
    OM_LOG_INFO() << "============================================================";
    OM_LOG_INFO() << "Total queries:     " << stats.totalQueries;
    OM_LOG_INFO() << "Successful:        " << stats.successful;
    OM_LOG_INFO() << "Failed:            " << stats.failed;
    if (stats.totalQueries > 0)
    {
        double successRate = 100.0 * stats.successful / stats.totalQueries;
        OM_LOG_INFO() << "Success rate:      " << successRate << "%";
    }
    OM_LOG_INFO() << "============================================================\n";
}

} // namespace oracle