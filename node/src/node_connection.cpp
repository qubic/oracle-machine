
#include "node_connection.h"
#include "om_common/config.h"
#include "om_common/logger.h"
#include "om_network/tcp_server.h"
#include "oracle_core/core_om_network_messages.h"

#ifdef _MSC_VER
#include <Winsock2.h>
#include <Ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <cstring>
#include <iostream>

namespace oracle
{

// Check if IP is in the allowed list
static bool isNodeIPAllowed(const ::sockaddr_in& addr)
{
    uint8_t ip[4];
    uint32_t ip_addr = ntohl(addr.sin_addr.s_addr);
    ip[0] = (ip_addr >> 24) & 0xFF;
    ip[1] = (ip_addr >> 16) & 0xFF;
    ip[2] = (ip_addr >> 8) & 0xFF;
    ip[3] = ip_addr & 0xFF;

    for (size_t i = 0; i < Config::instance().nodeCount(); i++)
    {
        auto acceptNode = Config::instance().nodes()[i];
        if (ip[0] == acceptNode.ip[0] && ip[1] == acceptNode.ip[1] && ip[2] == acceptNode.ip[2] &&
            ip[3] == acceptNode.ip[3])
        {
            return true;
        }
    }
    return false;
}

NodeConnection::NodeConnection(const std::string& bind_address, uint16_t port)
{
    _tcpServer = std::make_unique<TcpServer>(bind_address, port, TIME_OUT_MS);

    // Set connection filter to validate allowed IPs
    _tcpServer->setConnectionFilter(
        [](const ::sockaddr_in& addr) { return isNodeIPAllowed(addr); });

    // Set session handler to our protocol handling method
    // this->handleSession(session) funcion will be called for each session inside the tcp server
    _tcpServer->setSessionHandler([this](Session& session) { this->handleSession(session); });
}

NodeConnection::~NodeConnection()
{
    stop();
}

void NodeConnection::setHandler(
    std::function<std::vector<uint8_t>(const RequestResponseHeader&, const uint8_t*, int)> handler)
{
    _handler = std::move(handler);
}

bool NodeConnection::start()
{
    return _tcpServer->start();
}

void NodeConnection::stop()
{
    _tcpServer->stop();
}

bool NodeConnection::isRunning() const
{
    return _tcpServer->isRunning();
}

bool NodeConnection::sendResponseToNode(
    Session& session,
    uint8_t type,
    const uint8_t* payload,
    int payload_size)
{
    // TODO: check if we need to combine the header and payload into a single buffer for sending

    RequestResponseHeader header;
    header.checkAndSetSize(sizeof(RequestResponseHeader) + payload_size);
    header.setType(type);
    header.randomizeDejavu();

    // Send header
    if (!session.sendData((const uint8_t*)&header, sizeof(header)))
    {
        OM_LOG_WARNING() << "Respond to node " << session.getRemoteIP() << " - FAILED (header)";
        return false;
    }

    // Send payload
    if (payload_size > 0 && payload != nullptr)
    {
        if (!session.sendData(payload, payload_size))
        {
            OM_LOG_WARNING() << "Respond to node " << session.getRemoteIP() << " - FAILED (payload)";
            return false;
        }
    }

    OM_LOG_DEBUG() << "Respond to node " << session.getRemoteIP();

    return true;
}

// Main node's oracle message handling
void NodeConnection::handleSession(Session& session)
{
    std::vector<uint8_t> buffer(0xFFFF);

    while (session.isActive())
    {
        // Receive header
        RequestResponseHeader header;

        // This will block until header is received, or TIMEOUT occurs (handled by Session)
        int received = session.receiveExact((uint8_t*)&header, sizeof(header));
        while (!received)
        {
            // No data received -> try again if session is still active
            if (!session.isActive())
                break;
            received = session.receiveExact((uint8_t*)&header, sizeof(header));
        }

        if (received != sizeof(header))
        {
            // If 0, clean disconnect. If -1 or partial, error/timeout.
            if (received < 0 || (received > 0 && received < (int)sizeof(header)))
            {
                OM_LOG_ERROR() << "Node connection error or timeout (IP: " << session.getRemoteIP()
                               << ")";
            }
            break;
        }

        // Validate Size
        unsigned int packet_size = header.size();
        if (packet_size > buffer.size() || packet_size < sizeof(header))
        {
            OM_LOG_ERROR() << "Invalid packet size: " << packet_size;
            break;
        }

        // Receive Payload - consume payload for whaterver packet
        int payload_size = (int)packet_size - sizeof(header);
        if (payload_size > 0)
        {
            // recv remaining bytes
            received = session.receiveExact(buffer.data(), payload_size);
            if (received != payload_size)
            {
                OM_LOG_ERROR() << "Failed to receive payload";
                break;
            }
        }

        // Check if this is an OracleMachineQuery and do further process
        if (header.type() != OracleMachineQuery::type())
        {
            OM_LOG_DEBUG() << "Received unexpetected message type. " << (int)header.type();
            // Skipping unknown message types
            continue;
        }

        // Process Request
        if (_handler)
        {
            // This calls RequestHandler -> OracleClient::fetch
            // Since OracleClient now has timeouts, this line won't block forever!
            std::vector<uint8_t> response = _handler(header, buffer.data(), payload_size);

            // Send Response
            if (!response.empty())
            {
                sendResponseToNode(
                    session, OracleMachineReply::type(), response.data(), response.size());
            }
        }

        // TODO: sleep or yield to avoid busy loop ?
    }

    OM_LOG_DEBUG() << "Node Session finished: " << session.getRemoteIP();
}

} // namespace oracle