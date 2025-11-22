#include "tcp_client.h"

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define close(x) closesocket(x)
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

TcpClient::TcpClient()
    : _socketsInitialized(false)
{
    initializeSockets();
}

TcpClient::~TcpClient()
{
    cleanupSockets();
}

void TcpClient::initializeSockets()
{
#ifdef _MSC_VER
    if (!_socketsInitialized)
    {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 0), &wsa_data);
        _socketsInitialized = true;
    }
#endif
}

void TcpClient::cleanupSockets()
{
#ifdef _MSC_VER
    if (_socketsInitialized)
    {
        WSACleanup();
        _socketsInitialized = false;
    }
#endif
}

void TcpClient::setSessionHandler(SessionHandler handler)
{
    _sessionHandler = std::move(handler);
}

std::unique_ptr<Session> TcpClient::connect(const std::string& server_address, uint16_t port)
{
    // Create socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        std::cerr << "Failed to create client socket" << std::endl;
        return nullptr;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convert IP address
    if (inet_pton(AF_INET, server_address.c_str(), &server_addr.sin_addr) <= 0)
    {
        std::cerr << "Invalid server address: " << server_address << std::endl;
        close(sock_fd);
        return nullptr;
    }

    // Connect to server
    if (::connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Failed to connect to " << server_address << ":" << port << std::endl;
        close(sock_fd);
        return nullptr;
    }

    std::cout << "Connected to " << server_address << ":" << port << std::endl;

    // Create and return Session object
    return std::make_unique<Session>(sock_fd, server_address, port);
}

bool TcpClient::connectAndRun(const std::string& server_address, uint16_t port)
{
    auto session = connect(server_address, port);
    
    if (!session)
    {
        std::cerr << "Failed to connect to " << server_address << ":" << port << std::endl;
        return false;
    }

    std::cout << "Connected to server, starting session handler..." << std::endl;

    // Run session handler
    if (_sessionHandler)
    {
        _sessionHandler(*session);
    }
    else
    {
        std::cerr << "No session handler is set." << std::endl;
    }

    std::cout << "Disconnected from server" << std::endl;

    return true;
}

} // namespace oracle
