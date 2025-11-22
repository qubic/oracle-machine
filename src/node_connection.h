#pragma once
#include "network_messages/header.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace oracle
{

class TcpServer;
class Session;
class NodeConnection
{
public:
    NodeConnection(const std::string& bind_address, uint16_t port);
    ~NodeConnection();

    // Set request handler. This will be called for each incoming request, process it and return
    // response if needed. Handler receives header and payload, returns response payload
    void setHandler(std::function<std::vector<uint8_t>(const RequestResponseHeader&, const uint8_t*, int)> handler);

    // Start the server
    bool start();

    // Stop the server
    void stop();

    // Check if running
    bool isRunning() const;

private:
    // Handle a client session (connect, disconnect ...)
    void handleSession(Session& session);

    bool sendResponseToNode(Session& session, uint8_t type, const uint8_t* payload, int payload_size);

    std::unique_ptr<TcpServer> _tcpServer;
    std::function<std::vector<uint8_t>(const RequestResponseHeader&, const uint8_t*, int)> _handler;
};

} // namespace oracle
