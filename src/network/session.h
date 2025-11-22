#pragma once

#include <cstdint>
#include <string>
#include <memory>

namespace oracle
{

/**
 * Session represents a TCP connection endpoint, mainly incharge of data sending and receiving.
 * - Server-side: accepted client connections
 * - Client-side: outgoing connections to servers. Implement later
 */
class Session
{
public:
    // Server-side constructor (called when accepting connections)
    Session(int socket_fd, const std::string& remote_ip);

    ~Session();

    // Get connection information
    const std::string& getRemoteIP() const { return _remoteIP; }
    uint16_t getRemotePort() const { return _remotePort; }
    
    // Send raw data
    bool sendData(const uint8_t* data, int size);
    
    // Receive exactly sz bytes
    int receiveExact(uint8_t* buffer, int sz);
    
    // Receive up to sz bytes (returns actual bytes received)
    int receive(uint8_t* buffer, int sz);
    
    // Check if session is still active
    bool isActive() const { return _active; }
    
    // Manually close the session
    void stop();
    
    // Get statistics
    uint64_t getBytesSent() const { return _bytesSent; }
    uint64_t getBytesReceived() const { return _bytesReceived; }

private:
    int _socketFD;
    std::string _remoteIP;
    uint16_t _remotePort;
    bool _active;
    bool _clientSocket;  // true if client owns socket and should clean up
    
    uint64_t _bytesSent;
    uint64_t _bytesReceived;
};

} // namespace oracle