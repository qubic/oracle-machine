
#include "node_connection.h"
#include "config.h"
#include "oracle_core/core_om_network_messages.h"
#include "network/tcp_server.h"
#include "network/session.h"

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

    for (size_t i = 0; i < NODE_COUNT; i++)
    {
        if (ip[0] == NODE_LIST[i][0] && ip[1] == NODE_LIST[i][1] && ip[2] == NODE_LIST[i][2] &&
            ip[3] == NODE_LIST[i][3])
        {
            return true;
        }
    }
    return false;
}

NodeConnection::NodeConnection(const std::string& bind_address, uint16_t port)
{
    _tcpServer = std::make_unique<TcpServer>(bind_address, port);
    
    // Set connection filter to validate allowed IPs
    _tcpServer->setConnectionFilter([](const ::sockaddr_in& addr) {
        return isNodeIPAllowed(addr);
    });

    // Set session handler to our protocol handling method
    // this->handleSession(session) funcion will be called for each session inside the tcp server
    _tcpServer->setSessionHandler([this](Session& session) {
        this->handleSession(session);
    });
}

NodeConnection::~NodeConnection()
{
    stop();
}

void NodeConnection::setHandler(std::function<std::vector<uint8_t>(const RequestResponseHeader&, const uint8_t*, int)> handler)
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
        return false;
    }

    // Send payload
    if (payload_size > 0 && payload != nullptr)
    {
        return session.sendData(payload, payload_size);
    }

    return true;
}

// Main node's oracle message handling
void NodeConnection::handleSession(Session& session)
{
    uint8_t buffer[0xFFFF];

    std::cout << "Handling Oracle protocol session from " << session.getRemoteIP() << std::endl;

    while (session.isActive())
    {
        // Receive header
        RequestResponseHeader header;
        int received = session.receiveExact((uint8_t*)&header, sizeof(header));
        if (received != sizeof(header))
        {
            std::cout << "Connection closed or error. Received header size " << received
                      << std::endl;
            break;
        }

        // Validate header
        unsigned int packet_size = header.size();
        if (packet_size > sizeof(buffer) || packet_size < sizeof(header))
        {
            std::cerr << "Invalid packet size: " << packet_size << std::endl;
            break;
        }

        // Check if this is an OracleMachineQuery (type 190)
        if (header.type() != OracleMachineQuery::type)
        {
            // Skip this packet - not what we're expecting
            int payload_size = packet_size - sizeof(header);
            if (payload_size > 0)
            {
                received = session.receiveExact(buffer, payload_size);
                if (received != payload_size)
                {
                    std::cerr << "Failed to receive payload while skipping (connection lost)"
                              << std::endl;
                    break;
                }
            }
            std::cerr << "Unexpected message type: " << (int)header.type() << std::endl;
            continue;
        }

        // Receive payload
        int payload_size = packet_size - sizeof(header);
        if (payload_size > 0)
        {
            received = session.receiveExact(buffer, payload_size);
            if (received != payload_size)
            {
                std::cerr << "Failed to receive payload" << std::endl;
                break;
            }
        }

        // Handle request with user-provided handler
        if (_handler)
        {
            // Get response
            std::vector<uint8_t> response = _handler(header, buffer, payload_size);

            std::cout << "Sending response with OracleMachineReply to " 
                      << session.getRemoteIP() << std::endl;

            // Send response using Session abstraction
            sendResponseToNode(session, OracleMachineReply::type, response.data(), response.size());
        }
    }

    std::cout << "Session finished for " << session.getRemoteIP() << std::endl;
}

} // namespace oracle