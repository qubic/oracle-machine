#pragma once

#include "network/session.h"
#include "network/tcp_client.h"
#include "oracle_data.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace oracle
{

using FetchCallback = std::function<void(bool success, const OracleData& data)>;
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
    ~OracleClient();

    // Start the request processing thread
    bool start();

    // Stop the request processing thread and clear pending requests
    void stop();

    // Connect to oracle service (done automatically if needed)
    bool connect();

    // Check if connected
    bool isConnected() const;

    // Check if the worker thread is running
    bool isRunning() const { return _running.load(); }

    // Request shutdown flag
    void requestShutdown() { _shutdownRequested.store(true); }

    // ========== Fetch Methods ==========

    // Synchronous fetch - blocks until result is available or timeout
    // Returns false if request fails or times out
    bool fetch(OracleData& data, int timeout_ms = 30000);

    // Asynchronous fetch - returns immediately, calls callback when done
    // Callback is executed on the worker thread
    void fetchAsync(FetchCallback callback);

    // Get number of pending requests in queue
    size_t getPendingRequestCount() const;

    // ========== Info Methods ==========
    const std::string& getID() const { return _id; }
    const std::string& getName() const { return _name; }
    const std::string& getHost() const { return _host; }
    uint16_t get_port() const { return _port; }

private:
    // Internal request structure
    struct FetchRequest
    {
        uint32_t requestID;
        FetchCallback callback;
        std::chrono::steady_clock::time_point enqueueTime;
    };

    // Disconnect from oracle service
    void disconnect();

    // Force shutdown (unblocks any pending operations)
    void forceShutdown();

    // Worker thread function
    void workerThread();

    // Process a single fetch request
    bool processFetchRequest(const FetchRequest& request, OracleData& data);

    // Send JSON message via Session
    bool sendJSONMessage(Session& session, const std::string& json);

    // Receive JSON response via Session
    std::string receiveJSONMessage(Session& session);

    // Parse JSON response into OracleData
    bool parseResponse(const std::string& response, OracleData& data);

    // Oracle info
    std::string _id;
    std::string _name;
    std::string _host;
    uint16_t _port;

    // Network components
    TcpClient _tcpClient;
    std::unique_ptr<Session> _session;
    std::atomic<bool> _connected;

    // Request tracking
    std::atomic<uint32_t> _requestID;

    // Request queue and worker thread
    std::queue<FetchRequest> _requestQueue;
    mutable std::mutex _queueMutex;
    std::condition_variable _queueCondition;
    std::thread _workerThread;
    std::atomic<bool> _running;
    std::atomic<bool> _shutdownRequested;

    // Connection mutex (separate from queue mutex)
    std::mutex _connectionMutex;
};

} // namespace oracle