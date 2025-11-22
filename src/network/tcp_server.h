#pragma once

#include "session.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#include <Winsock2.h>
#include <Ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace oracle
{

class TcpServer
{
public:
    // Callback types
    using ConnectionFilter = std::function<bool(const ::sockaddr_in&)>; // filter by client address
    using SessionHandler =
        std::function<void(Session& session)>; // handle session, main process logic

    TcpServer(const std::string& bind_address, uint16_t port);
    ~TcpServer();

    // Set callbacks
    void setConnectionFilter(ConnectionFilter filter);
    void setSessionHandler(SessionHandler handler);

    // Server control
    bool start();
    void stop();
    bool isRunning() const { return _running; }

    void handleSession(Session& session);

private:
    void acceptLoop();
    void clientThread(int client_fd, const std::string& client_ip);
    void removeClientFD(int client_fd);
    void cleanupFinishedThreads(); 

    std::string _bindAddress;
    uint16_t _port;
    int _serverFD;

    std::atomic<bool> _running;
    std::atomic<bool> _stopRequested;

    std::thread _acceptThread;
    std::vector<std::thread> _clientThreads;

    std::vector<int> _activeClientFDs;
    std::mutex _clientFDsMutex;

    std::mutex _threadsMutex;

    // Callbacks
    ConnectionFilter _connectionFilter;
    SessionHandler _sessionHandler;
};

} // namespace oracle