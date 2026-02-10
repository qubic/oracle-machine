#include "om_network/tcp_server.h"
#include "om_common/logger.h"

#ifdef _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
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

TcpServer::TcpServer(const std::string& bind_address, uint16_t port, int timeoutMs) :
    _bindAddress(bind_address),
    _port(port),
    _timeoutMs(timeoutMs),
    _serverFD(-1),
    _running(false),
    _stopRequested(false)
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
        OM_LOG_ERROR() << "Failed to create server socket" ;
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
        OM_LOG_ERROR() << "Failed to bind to " << _bindAddress << ":" << _port ;
#ifdef _MSC_VER
        closesocket(_serverFD);
#else
        ::close(_serverFD);
#endif
        _serverFD = -1;
        return false;
    }

    // Listen
    if (listen(_serverFD, 10) < 0)
    {
        OM_LOG_ERROR() << "Failed to listen" ;
#ifdef _MSC_VER
        closesocket(_serverFD);
#else
        ::close(_serverFD);
#endif
        _serverFD = -1;
        return false;
    }

    _running = true;
    _stopRequested = false;

    // Laucnh accept thread and handle this connection by different thread
    _acceptThread = std::thread(&TcpServer::acceptLoop, this);

    OM_LOG_INFO() << "TCP Server listening on " << _bindAddress << ":" << _port ;
    return true;
}

void TcpServer::cleanupFinishedThreads()
{
    for (auto it = _clientThreads.begin(); it != _clientThreads.end(); ++it)
    {
        OM_LOG_INFO() << "  _clientThreads item " << it->get_id();
    }

    std::vector<std::thread> threadsToJoin;
    {
        std::lock_guard<std::mutex> lock(_threadsMutex);
        auto clientIt = _clientThreads.begin();
        while (clientIt != _clientThreads.end())
        {
            auto finishedIt = _finishedThreadIds.find(clientIt->get_id());
            if (finishedIt != _finishedThreadIds.end())
            {
                _finishedThreadIds.erase(finishedIt);
                threadsToJoin.push_back(std::move(*clientIt));
                clientIt = _clientThreads.erase(clientIt);
            }
            else
            {
                ++clientIt;
            }
        }
    }

    for (auto it = threadsToJoin.begin(); it != threadsToJoin.end(); ++it)
    {
        OM_LOG_INFO() << "  threadsToJoin item " << it->get_id();
    }
    for (auto it = _finishedThreadIds.begin(); it != _finishedThreadIds.end(); ++it)
    {
        OM_LOG_INFO() << "  _finishedThreadIds item " << *it;
    }
    for (auto it = _clientThreads.begin(); it != _clientThreads.end(); ++it)
    {
        OM_LOG_INFO() << "  _clientThreads item " << it->get_id();
    }

    OM_LOG_INFO() << "cleanup: before join";
    for (auto& t : threadsToJoin)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    OM_LOG_INFO() << "cleanup: after join";
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

    OM_LOG_INFO() << "Initiating server shutdown..." ;

    _stopRequested = true;

    // Close server socket first to stop accepting new connections
    if (_serverFD >= 0)
    {
        OM_LOG_INFO() << "Closing server socket..." ;
#ifdef _MSC_VER
        closesocket(_serverFD);
#else
        ::close(_serverFD);
#endif
        _serverFD = -1;
    }

    // Wait for accept thread to exit
    if (_acceptThread.joinable())
    {
        OM_LOG_INFO() << "Waiting for accept thread..." ;
        _acceptThread.join();
    }

    // Close all active client sockets to unblock recv() calls
    {
        std::lock_guard<std::mutex> lock(_clientFDsMutex);
        OM_LOG_INFO() << "Closing " << _activeClientFDs.size() << " active connections..." ;
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
        OM_LOG_INFO() << "Waiting for " << _clientThreads.size() << " client threads..." ;
        for (auto& t : _clientThreads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(_threadsMutex);
        _clientThreads.clear();
        _finishedThreadIds.clear();
    }

    {
        std::lock_guard<std::mutex> lock(_clientFDsMutex);
        _activeClientFDs.clear();
    }

    _running = false;

    OM_LOG_INFO() << "Server shutdown complete" ;
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
            OM_LOG_ERROR() << "Connection rejected from " << client_ip << " (filtered)" ;
#ifdef _MSC_VER
            closesocket(client_fd);
#else
            ::close(client_fd);
#endif
            continue;
        }

        OM_LOG_INFO() << "Client connected from " << client_ip ;

        // Add this client fd to active list so that we can know which to close on shutdown
        // This will prevent the case that the server is stopped but some client threads are still
        {
            std::lock_guard<std::mutex> lock(_clientFDsMutex);
            _activeClientFDs.push_back(client_fd);
        }

        OM_LOG_INFO() << "cleanup before new thread for " << client_ip;

        // Cleanup finished threads
        cleanupFinishedThreads();


        OM_LOG_INFO() << "start new thread for " << client_ip;

        // Handle each connected client in new thread
        _clientThreads.emplace_back(&TcpServer::clientThread, this, client_fd, client_ip);
    }
}

void TcpServer::clientThread(int clientFd, std::string clientIP)
{
     auto startTime = std::chrono::steady_clock::now();
     
    // Create Session object (It takes ownership of client_fd)
    Session session(clientFd, clientIP, 0);

    // Set the timeout per operation
    session.setTimeout(_timeoutMs); 
    
    // Enable TCP Keep-Alive. Currently using system defaults.(~2 hours)
    session.setKeepAlive(true);

    // Handle the session
    handleSession(session);

    // Log session duration
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::steady_clock::now() - startTime).count();

    auto bytesSent = session.getBytesSent();
    auto bytesReceived = session.getBytesReceived();

    if (bytesSent == 0 && bytesReceived == 0)
    {
        OM_LOG_WARNING() << "Session " << clientIP << " ZOMBIE: duration=" << duration 
                        << "s (no data exchanged)";
    }
    else
    {
        OM_LOG_INFO() << "Session " << clientIP << " duration: " << duration << "s, "
                    << "sent=" << bytesSent << ", recv=" << bytesReceived;
    }

    // Mark this thread as finished for cleanup
    {
        std::lock_guard<std::mutex> lock(_threadsMutex);
        _finishedThreadIds.insert(std::this_thread::get_id());
    }
    OM_LOG_INFO() << "Added " << std::this_thread::get_id()  << " to _finishedThreadIds, IP " << clientIP;
    for (auto i : _finishedThreadIds)
    {
        OM_LOG_INFO() << "-> _finishedThreadIds item " << i;
    }


    // Cleanup - remove from active list
    removeClientFD(clientFd);

    OM_LOG_INFO() << "Client disconnected from " << clientIP ;
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