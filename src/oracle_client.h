#pragma once

#include <atomic>
#include <string>

#include "oracle_data.h"

namespace oracle
{

// Currently each OracleClient using TCP socket and JSON protocol and only allows one request at a
// time.
// TODO: consider other protocols and concurrency improvements if needed.
class OracleClient
{
public:
    OracleClient(
        const std::string& id,
        const std::string& name,
        const std::string& host,
        uint16_t port);
    ~OracleClient();

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
    // Receive exactly sz bytes
    int receiveData(uint8_t* buffer, int sz);

    // Send JSON message
    bool sendJSONMessage(const std::string& json);

    // Receive JSON response
    std::string receiveJSONMessage();

    std::string _id;
    std::string _name;
    std::string _host;
    uint16_t _port;
    int _socketFd;
    std::atomic<bool> _connected;
    uint32_t _requestID;

    // Current each OracleClient only process one request at a time
    // TODO: improve concurrency if needed
    std::mutex _mutex;
};

} // namespace oracle