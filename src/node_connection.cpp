
#include "node_connection.h"
#include "config.h"
#include "oracle_core/core_om_network_messages.h"

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define close(x) closesocket(x)
#define SHUT_RDWR SD_BOTH
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <iostream>

namespace oracle
{

// Check if IP is in the allowed list
static bool isNodeIPAllowed(const struct sockaddr_in& addr)
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

NodeConnection::NodeConnection(const std::string& bind_address, uint16_t port) :
    _bindAddress(bind_address), _port(port), server_fd_(-1), _running(false), _stopRequested(false)
{
#ifdef _MSC_VER
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 0), &wsa_data);
#endif
}

NodeConnection::~NodeConnection()
{
    stop();
#ifdef _MSC_VER
    WSACleanup();
#endif
}

void NodeConnection::set_handler(request_handler handler)
{
    _handler = std::move(handler);
}

bool NodeConnection::start()
{
    if (_running)
        return true;

    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
    {
        std::cerr << "Failed to create server socket" << std::endl;
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);

    if (_bindAddress == "0.0.0.0")
    {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        inet_pton(AF_INET, _bindAddress.c_str(), &addr.sin_addr);
    }

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Failed to bind to " << _bindAddress << ":" << _port << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(server_fd_, 10) < 0)
    {
        std::cerr << "Failed to listen" << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    _running = true;
    _stopRequested = false;
    _acceptThread = std::thread(&NodeConnection::acceptLoop, this);

    std::cout << "TCP Server listening on " << _bindAddress << ":" << _port << std::endl;
    return true;
}

void NodeConnection::stop()
{
    if (!_running)
        return;

    _stopRequested = true;

    // Close server socket to unblock accept
    if (server_fd_ >= 0)
    {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (_acceptThread.joinable())
    {
        _acceptThread.join();
    }

    // Wait for client threads
    for (auto& t : _clientThreads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    _clientThreads.clear();

    _running = false;
}

void NodeConnection::acceptLoop()
{
    while (!_stopRequested)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0)
        {
            if (_stopRequested)
                break;
            continue;
        }

        // Get client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        // Check if IP is allowed
        if (!isNodeIPAllowed(client_addr)) {
            std::cerr << "Connection rejected from " << client_ip << " (not in allowed list)" << std::endl;
            close(client_fd);
            continue;
        }


        std::cout << "Node connected from " << client_ip << std::endl;

        // Handle in new thread
        _clientThreads.emplace_back(&NodeConnection::handleClient, this, client_fd);
    }
}

int NodeConnection::receiveData(int socket_fd, uint8_t* buffer, int sz)
{
    int total_received = 0;
    while (sz > 0)
    {
        int received = recv(socket_fd, (char*)buffer + total_received, sz, 0);
        if (received <= 0)
        {
            break;
        }
        total_received += received;
        sz -= received;
    }
    return total_received;
}

bool NodeConnection::sendResponse(
    int socket_fd,
    uint8_t type,
    const uint8_t* payload,
    int payload_size)
{
    // Build header
    RequestResponseHeader header;
    header.checkAndSetSize(sizeof(RequestResponseHeader) + payload_size);
    header.setType(type);
    header.randomizeDejavu();

    // Send header
    int sent = send(socket_fd, (const char*)&header, sizeof(header), 0);
    if (sent != sizeof(header))
    {
        return false;
    }

    // Send payload
    if (payload_size > 0 && payload != nullptr)
    {
        int total_sent = 0;
        while (total_sent < payload_size)
        {
            sent = send(socket_fd, (const char*)payload + total_sent, payload_size - total_sent, 0);
            if (sent <= 0)
            {
                return false;
            }
            total_sent += sent;
        }
    }

    return true;
}

void NodeConnection::handleClient(int client_fd)
{
    uint8_t buffer[0xFFFF];

    while (!_stopRequested)
    {
        // Receive header
        RequestResponseHeader header;
        int received = receiveData(client_fd, (uint8_t*)&header, sizeof(header));
        if (received != sizeof(header))
        {
            break; // Connection closed or error
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
                receiveData(client_fd, buffer, payload_size);
            }
            std::cerr << "Unexpected message type: " << (int)header.type() << std::endl;
            continue;
        }

        // Receive payload
        int payload_size = packet_size - sizeof(header);
        if (payload_size > 0)
        {
            received = receiveData(client_fd, buffer, payload_size);
            if (received != payload_size)
            {
                std::cerr << "Failed to receive payload" << std::endl;
                break;
            }
        }

        // Handle request
        if (_handler)
        {
            std::vector<uint8_t> response = _handler(header, buffer, payload_size);
            
            // Send response with OracleMachineReply type (191)
            sendResponse(client_fd, OracleMachineReply::type, response.data(), response.size());
        }
    }

    close(client_fd);
}

} // namespace oracle