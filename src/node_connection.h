#pragma once
#include "network_messages/header.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

namespace oracle
{

class NodeConnection
{
public:
    // Handler receives header and payload, returns response payload (header will be added
    // automatically)
    using request_handler =
        std::function<std::vector<uint8_t>(const RequestResponseHeader&, const uint8_t*, int)>;

    NodeConnection(const std::string& bind_address, uint16_t port);
    ~NodeConnection();

    // Set request handler
    void set_handler(request_handler handler);

    // Start the server
    bool start();

    // Stop the server
    void stop();

    // Check if running
    bool isRunning() const { return _running; }

private:
    void acceptLoop();
    void handleClient(int client_fd, const char* client_ip = nullptr);

    // Receive exactly sz bytes, returns actual bytes received
    int receiveData(int socket_fd, uint8_t* buffer, int sz);

    // Send data with header
    bool sendResponse(int socket_fd, uint8_t type, const uint8_t* payload, int payload_size);

    std::string _bindAddress;
    uint16_t _port;
    int server_fd_;
    request_handler _handler;
    std::thread _acceptThread;
    std::vector<std::thread> _clientThreads;
    std::atomic<bool> _running;
    std::atomic<bool> _stopRequested;

    std::vector<int> _activeClientFDs;
    std::mutex _clientFDsMutex;
};

} // namespace oracle

