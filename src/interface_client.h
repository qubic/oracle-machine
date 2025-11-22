#pragma once

#include "network/session.h"
#include "network/tcp_client.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace oracle
{

// Query result structure
struct InterfaceQueryResult
{
    std::vector<uint8_t> replyData;
    bool valid;

    InterfaceQueryResult() : valid(false) {}
};

// Callback type for async queries
using QueryCallback = std::function<void(bool success, const InterfaceQueryResult& result)>;

/**
 * InterfaceClient - One client per interface type with worker thread
 *
 * Based on oracle_client architecture with worker thread and request queue.
 * Maintains ONE connection to an interface aggregator service.
 * The aggregator service handles routing to multiple providers internally.
 *
 * Uses binary protocol (sends/receives raw bytes of interface-specific structures).
 *
 * Example:
 *   InterfaceClient client(0, "localhost", 9001);
 *   client.start();  // Start worker thread
 *   client.query(priceQueryData, 104, replyData, 16, 5000);
 *   client.stop();   // Stop worker thread
 */
class InterfaceClient
{
public:
    /**
     * Constructor
     *
     * @param interfaceIndex Interface type (0 = Price, 1 = Weather, etc.)
     * @param host Host of the aggregator service
     * @param port Port of the aggregator service
     */
    InterfaceClient(uint32_t interfaceIndex, const std::string& host, uint16_t port);

    ~InterfaceClient();

    // Start the request processing thread
    bool start();

    // Stop the request processing thread and clear pending requests
    void stop();

    // Connect to aggregator service (done automatically if needed)
    bool connect();

    // Check if connected
    bool isConnected() const;

    // Check if the worker thread is running
    bool isRunning() const { return _running.load(); }

    // Request shutdown flag
    void requestShutdown() { _shutdownRequested.store(true); }

    /**
     * Synchronous query - blocks until result is available or timeout
     *
     * @param queryData Interface-specific query data (e.g., Price::OracleQuery, 104 bytes)
     * @param querySize Size of query data
     * @param replyData Buffer for interface-specific reply (e.g., Price::OracleReply, 16 bytes)
     * @param replySize Expected size of reply
     * @param timeout_ms Timeout in milliseconds
     * @return true if successful, false otherwise
     */
    bool query(
        const uint8_t* queryData,
        size_t querySize,
        uint8_t* replyData,
        size_t replySize,
        int timeout_ms);

    /**
     * Asynchronous query - returns immediately, calls callback when done
     * Callback is executed on the worker thread
     */
    void queryAsync(
        const uint8_t* queryData,
        size_t querySize,
        size_t replySize,
        QueryCallback callback);

    // Get number of pending requests in queue
    size_t getPendingRequestCount() const;

    // ========== Info Methods ==========
    uint32_t getInterfaceIndex() const { return _interfaceIndex; }
    const std::string& getHost() const { return _host; }
    uint16_t getPort() const { return _port; }

    // Statistics
    size_t getTotalQueries() const { return _totalQueries.load(); }

private:
    // Internal request structure (like oracle_client's FetchRequest)
    struct QueryRequest
    {
        uint32_t requestID;
        std::vector<uint8_t> queryData;
        size_t replySize;
        QueryCallback callback;
        std::chrono::steady_clock::time_point enqueueTime;
    };

    // Disconnect from aggregator service
    void disconnect();

    // Force shutdown (unblocks any pending operations)
    void forceShutdown();

    // Worker thread function 
    void workerThread();

    // Process a single query request
    bool processQueryRequest(const QueryRequest& request, InterfaceQueryResult& result);

    // Interface info
    uint32_t _interfaceIndex;
    std::string _host;
    uint16_t _port;

    // Network components
    TcpClient _tcpClient;
    std::unique_ptr<Session> _session;
    std::atomic<bool> _connected;

    // Request tracking
    std::atomic<uint32_t> _requestID;

    // Request queue and worker thread
    std::queue<QueryRequest> _requestQueue;
    mutable std::mutex _queueMutex;
    std::condition_variable _queueCondition;
    std::thread _workerThread;
    std::atomic<bool> _running;
    std::atomic<bool> _shutdownRequested;

    // Connection mutex
    std::mutex _connectionMutex;

    // Statistics
    std::atomic<size_t> _totalQueries;
};

} // namespace oracle