#pragma once

#include <atomic>
#include <string>

#include "network/session.h"
#include "network/tcp_client.h"
#include "oracle_data.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace oracle
{

/**
 * OracleClient - Client for fetching data from Oracle services.
 * 
 * Uses JSON protocol over TCP.
 * Each OracleClient allows one request at a time (mutex protected).
 */
class OracleClient
{
public:
    OracleClient(
        const std::string& id,
        const std::string& name,
        const std::string& host,
        uint16_t port);
    ~OracleClient() = default;

    // Connect to oracle service
    bool connect();

    // Disconnect from oracle service
    void disconnect();

    // Check if connected
    bool isConnected() const;

    // Fetch data from oracle (uses JSON protocol)
    bool fetch(OracleData& data);

    // Ping oracle for health check (uses JSON protocol)
    bool ping();

    // Get oracle info
    const std::string& getID() const { return _id; }
    const std::string& getName() const { return _name; }
    const std::string& getHost() const { return _host; }
    uint16_t get_port() const { return _port; }

private:
    // Send JSON message via Session
    bool sendJSONMessage(Session& session, const std::string& json);

    // Receive JSON response via Session
    std::string receiveJSONMessage(Session& session);

    std::string _id;
    std::string _name;
    std::string _host;
    uint16_t _port;
    
    TcpClient _tcpClient;
    std::unique_ptr<Session> _session;
    
    std::atomic<bool> _connected;
    uint32_t _requestID;

    // Mutex for single request at a time
    std::mutex _mutex;
};

} // namespace oracle