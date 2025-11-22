#include "tcp_server.h"

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

#include <algorithm>
#include <cstring>
#include <iostream>

namespace oracle
{

TcpServer::TcpServer(const std::string& bind_address, uint16_t port) :
    _bindAddress(bind_address), _port(port), _serverFD(-1), _running(false), _stopRequested(false)
{
#ifdef _MSC_VER
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 0), &wsa_data);
#endif
}

TcpServer::~TcpServer()
{
    stop();
#ifdef _MSC_VER
    WSACleanup();
#endif
}

void TcpServer::setConnectionFilter(ConnectionFilter filter)
{
    _connectionFilter = std::move(filter);
}

void TcpServer::setSessionHandler(SessionHandler handler)
{
    _sessionHandler = std::move(handler);
}

bool TcpServer::start()
{
    if (_running)
        return true;

    // Create socket
    _serverFD = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverFD < 0)
    {
        std::cerr << "Failed to create server socket" << std::endl;
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(_serverFD, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

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

    if (bind(_serverFD, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Failed to bind to " << _bindAddress << ":" << _port << std::endl;
        close(_serverFD);
        _serverFD = -1;
        return false;
    }

    // Listen
    if (listen(_serverFD, 10) < 0)
    {
        std::cerr << "Failed to listen" << std::endl;
        close(_serverFD);
        _serverFD = -1;
        return false;
    }

    _running = true;
    _stopRequested = false;

    // Laucnh accept thread and handle this connection by different thread
    _acceptThread = std::thread(&TcpServer::acceptLoop, this);

    std::cout << "TCP Server listening on " << _bindAddress << ":" << _port << std::endl;
    return true;
}

void TcpServer::cleanupFinishedThreads()
{
    std::lock_guard<std::mutex> lock(_threadsMutex);
    auto it = _clientThreads.begin();
    while (it != _clientThreads.end())
    {
        if (!it->joinable())  // Thread finished
            it = _clientThreads.erase(it);
        else
            ++it;
    }
}

// Order of shutdown:
// 1. Close server socket first
// 2. Join accept thread
// 3. Shutdown client sockets (but don't close!)
// 4. Join all client threads (they close their own sockets)
void TcpServer::stop()
{
    if (!_running)
        return;

    std::cout << "Initiating server shutdown..." << std::endl;

    _stopRequested = true;

    // Close server socket first to stop accepting new connections
    if (_serverFD >= 0)
    {
        std::cout << "Closing server socket..." << std::endl;
        shutdown(_serverFD, SHUT_RDWR);
        close(_serverFD);
        _serverFD = -1;
    }

    // Wait for accept thread to exit
    if (_acceptThread.joinable())
    {
        std::cout << "Waiting for accept thread..." << std::endl;
        _acceptThread.join();
    }

    // Close all active client sockets to unblock recv() calls
    {
        std::lock_guard<std::mutex> lock(_clientFDsMutex);
        std::cout << "Closing " << _activeClientFDs.size() << " active connections..." << std::endl;
        for (int client_fd : _activeClientFDs)
        {
            if (client_fd >= 0)
            {
                shutdown(client_fd, SHUT_RDWR);
            }
        }
    }

    // Wait for all client threads cleanup
    {
        std::lock_guard<std::mutex> lock(_threadsMutex);
        std::cout << "Waiting for " << _clientThreads.size() << " client threads..." << std::endl;
        for (auto& t : _clientThreads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        _clientThreads.clear();
    }

    {
        std::lock_guard<std::mutex> lock(_clientFDsMutex);
        _activeClientFDs.clear();
    }

    _running = false;

    std::cout << "Server shutdown complete" << std::endl;
}

void TcpServer::acceptLoop()
{
    while (!_stopRequested)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(_serverFD, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0)
        {
            if (_stopRequested)
                break;
            continue;
        }

        // Get client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        // Check connection filter if set
        if (_connectionFilter && !_connectionFilter(client_addr))
        {
            std::cerr << "Connection rejected from " << client_ip << " (filtered)" << std::endl;
            close(client_fd);
            continue;
        }

        std::cout << "Client connected from " << client_ip << std::endl;

        // Add this client fd to active list so that we can know which to close on shutdown
        // This will prevent the case that the server is stopped but some client threads are still
        {
            std::lock_guard<std::mutex> lock(_clientFDsMutex);
            _activeClientFDs.push_back(client_fd);
        }

        // Cleanup finished threads
        cleanupFinishedThreads();

        // Handle each connected client in new thread
        _clientThreads.emplace_back(&TcpServer::clientThread, this, client_fd, client_ip);
    }
}

void TcpServer::clientThread(int client_fd, const std::string& client_ip)
{
    // Create Session object
    Session session(client_fd, client_ip);

    // Handle the session (virtual method - can be overridden)
    handleSession(session);

    // Cleanup - remove from active list
    removeClientFD(client_fd);

    // Close the socket
    close(client_fd);

    std::cout << "Client disconnected from " << client_ip << std::endl;
}

void TcpServer::handleSession(Session& session)
{
    // Default implementation: call the session handler if set
    if (_sessionHandler)
    {
        _sessionHandler(session);
    }
}

void TcpServer::removeClientFD(int client_fd)
{
    std::lock_guard<std::mutex> lock(_clientFDsMutex);
    auto it = std::find(_activeClientFDs.begin(), _activeClientFDs.end(), client_fd);
    if (it != _activeClientFDs.end())
    {
        _activeClientFDs.erase(it);
    }
}

} // namespace oracle