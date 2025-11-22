#pragma once

#include "session.h"

#include <functional>
#include <memory>
#include <string>

namespace oracle
{

/**
 * TcpClient - Creates outgoing TCP connections.
 * Returns Session objects representing the connections.
 */
class TcpClient
{
public:
    // Callbacks
    using SessionHandler = std::function<void(Session& session)>;
    using ConnectionHandler = std::function<void(const std::string& remote_ip, uint16_t port)>;

    TcpClient();
    ~TcpClient();

    // Set callbacks
    void setSessionHandler(SessionHandler handler);

    // Connect to server - returns Session object if successful
    std::unique_ptr<Session> connect(const std::string& server_address, uint16_t port);

    // Connect and run session handler (blocking until disconnection)
    bool connectAndRun(const std::string& server_address, uint16_t port);

private:
    void initializeSockets();
    void cleanupSockets();

    SessionHandler _sessionHandler;
    bool _socketsInitialized;
};

} // namespace oracle