#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace oracle
{

/**
 * Session represents a TCP connection endpoint.
 * * RAII Compliant:
 * - This class strictly owns the socket file descriptor.
 * - It is Move-Constructible and Move-Assignable.
 * - It is NOT Copyable.
 * - Destructor automatically closes the connection.
 */
class Session
{
public:
    // Main constructor (takes ownership of socket_fd)
    Session(int socket_fd, const std::string& remote_ip, uint16_t remote_port);

    // Destructor (closes socket)
    ~Session();

    // Non-copyable (Prevent double-close of same FD)
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Allow Move (Transfer ownership)
    Session(Session&& other) noexcept;
    Session& operator=(Session&& other) noexcept;

    // Set read/write timeout in milliseconds
    // Returns false if setting socket option failed
    bool setTimeout(uint32_t timeout_ms);

    // Enable TCP Keep-Alive packets
    void setKeepAlive(bool enable, int idleSec = 60, int intervalSec = 10, int count = 3);

    // Get connection information
    const std::string& getRemoteIP() const { return _remoteIP; }
    uint16_t getRemotePort() const { return _remotePort; }

    // Send raw data
    bool sendData(const uint8_t* data, int size);

    // Receive exactly sz bytes (blocks until all bytes received or error/timeout)
    int receiveExact(uint8_t* buffer, int sz);

    // Receive up to sz bytes (returns actual bytes received)
    int receive(uint8_t* buffer, int sz);

    // Check if session is still active
    bool isActive() const { return _active.load(); }

    // Manually close the session
    void close();

    // Force shutdown (thread-safe, unblocks recv)
    void forceShutdown();

    // Get statistics
    uint64_t getBytesSent() const { return _bytesSent; }
    uint64_t getBytesReceived() const { return _bytesReceived; }

private:
    int _socketFD;
    std::string _remoteIP;
    uint16_t _remotePort;
    std::atomic<bool> _active;

    uint64_t _bytesSent;
    uint64_t _bytesReceived;
};

} // namespace oracle