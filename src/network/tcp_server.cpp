#include "tcp_server.h"
#include "logger.h"

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
#include <sys/poll.h>
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
        LOG_ERROR() << "Failed to create server socket" ;
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(_serverFD, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

#ifndef _MSC_VER
    // SO_REUSEPORT helps on Linux/Unix systems for faster rebinding
    setsockopt(_serverFD, SOL_SOCKET, SO_REUSEPORT, (const char*)&opt, sizeof(opt));
#endif

    // Set SO_LINGER to close immediately without TIME_WAIT
    linger sl;
    sl.l_onoff = 1;  // Enable linger
    sl.l_linger = 0; // Timeout = 0 (close immediately, send RST)
    setsockopt(_serverFD, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));

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
        LOG_ERROR() << "Failed to bind to " << _bindAddress << ":" << _port ;
        close(_serverFD);
        _serverFD = -1;
        return false;
    }

    // Listen
    if (listen(_serverFD, 10) < 0)
    {
        LOG_ERROR() << "Failed to listen" ;
        close(_serverFD);
        _serverFD = -1;
        return false;
    }

    _running = true;
    _stopRequested = false;

    // Laucnh accept thread and handle this connection by different thread
    _acceptThread = std::thread(&TcpServer::acceptLoop, this);

    LOG_INFO() << "TCP Server listening on " << _bindAddress << ":" << _port ;
    return true;
}

void TcpServer::cleanupFinishedThreads()
{
    std::lock_guard<std::mutex> lock(_threadsMutex);
    auto it = _clientThreads.begin();
    while (it != _clientThreads.end())
    {
        if (it->joinable())
        {
            it->join();
            it = _clientThreads.erase(it);
        }
        else
        {
            ++it;
        }
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

    LOG_INFO() << "Initiating server shutdown..." ;

    _stopRequested = true;

    // Close server socket first to stop accepting new connections
    if (_serverFD >= 0)
    {
        LOG_INFO() << "Closing server socket..." ;
        close(_serverFD);
        _serverFD = -1;
    }

    // Wait for accept thread to exit
    if (_acceptThread.joinable())
    {
        LOG_INFO() << "Waiting for accept thread..." ;
        _acceptThread.join();
    }

    // Close all active client sockets to unblock recv() calls
    {
        std::lock_guard<std::mutex> lock(_clientFDsMutex);
        LOG_INFO() << "Closing " << _activeClientFDs.size() << " active connections..." ;
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
        LOG_INFO() << "Waiting for " << _clientThreads.size() << " client threads..." ;
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

    LOG_INFO() << "Server shutdown complete" ;
}

void TcpServer::acceptLoop()
{
    while (!_stopRequested)
    {
#ifdef _MSC_VER
        // Windows: select()
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(_serverFD, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;

        int result = select(_serverFD + 1, &readfds, NULL, NULL, &timeout);
#else
        // Linux: poll()
        pollfd pfd;
        pfd.fd = _serverFD;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int result = poll(&pfd, 1, 500); // 500ms
#endif

        if (result < 0)
        {
            // Error
            continue;
        }
        else if (result == 0)
        {
            // Timeout - check _stopRequested and continue
            continue;
        }

        // Socket is ready for accept
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(_serverFD, (sockaddr*)&client_addr, &client_len);
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
            LOG_ERROR() << "Connection rejected from " << client_ip << " (filtered)" ;
            close(client_fd);
            continue;
        }

        LOG_INFO() << "Client connected from " << client_ip ;

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
    // Create Session object (It takes ownership of client_fd)
    Session session(client_fd, client_ip, 0);

    // The thread will now wait forever for the Node to send data.
    session.setTimeout(0); 
    
    // Enable TCP Keep-Alive. Currently using system defaults.(~2 hours)
    session.setKeepAlive(true);

    // Handle the session
    handleSession(session);

    // Cleanup - remove from active list
    removeClientFD(client_fd);

    LOG_INFO() << "Client disconnected from " << client_ip ;
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