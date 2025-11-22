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
    TcpClient();
    ~TcpClient();

    // Connect to server - returns Session object if successful
    std::unique_ptr<Session> connect(
        const std::string& host,
        uint16_t port,
        int timeout_ms = 5000);

private:

    // Helper: Set socket to non-blocking mode
    bool setNonBlocking(int sock_fd);
    
    // Helper: Wait for socket to become writable (connected) with timeout
    bool waitForConnect(int sock_fd, int timeout_ms);
};

} // namespace oracle