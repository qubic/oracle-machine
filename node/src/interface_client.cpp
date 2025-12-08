#include "interface_client.h"
#include "network_messages/header.h"
#include "om_common/config.h"
#include "om_common/logger.h"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace oracle
{

// Constants
static const int CONNECT_TIMEOUT_MS = 3000;
static const int IO_TIMEOUT_MS = 5000; // 5 seconds to read/write

InterfaceClient::InterfaceClient(uint32_t interfaceIndex, const std::string& host, uint16_t port) :
    _interfaceIndex(interfaceIndex),
    _host(host),
    _port(port),
    _connected(false),
    _requestID(0),
    _running(false),
    _shutdownRequested(false),
    _totalQueries(0)
{
    OM_LOG_INFO() << "InterfaceClient[" << _interfaceIndex << "] created for " << _host << ":"
                  << _port;
}

InterfaceClient::~InterfaceClient()
{
    stop();
    OM_LOG_INFO() << "InterfaceClient[" << _interfaceIndex << "] destroyed. "
                  << "Total queries: " << _totalQueries.load();
}

bool InterfaceClient::start()
{
    if (_running.load())
    {
        OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex << "] Already running";
        return true;
    }

    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex
                   << "] Starting request processing thread...";

    _running.store(true);
    _shutdownRequested.store(false);

    _workerThread = std::thread(&InterfaceClient::workerThread, this);

    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex
                   << "] Request processing thread started";
    return true;
}

void InterfaceClient::stop()
{
    if (!_running.load())
    {
        return;
    }

    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex
                   << "] Stopping request processing thread...";

    // Stop all running operations
    forceShutdown();

    // Signal worker thread to stop
    _running.store(false);
    _queueCondition.notify_all();

    // Wait for worker thread to finish
    if (_workerThread.joinable())
    {
        _workerThread.join();
    }

    // Clear any pending requests
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        while (!_requestQueue.empty())
        {
            _requestQueue.pop();
        }
    }

    // Disconnect
    disconnect();

    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex
                   << "] Request processing thread stopped";
}

bool InterfaceClient::connect()
{
    std::lock_guard<std::mutex> lock(_connectionMutex);

    // If already connected and active, return true
    if (_session && _session->isActive())
    {
        return true;
    }

    // Reset session if it existed but was inactive
    _session.reset();

    OM_LOG_INFO() << "InterfaceClient[" << _interfaceIndex << "] Connecting to " << _host << ":"
                  << _port << "...";

    // TcpClient::connect returns a unique_ptr<Session> so that this client owns it
    _session = _tcpClient.connect(_host, _port, CONNECT_TIMEOUT_MS);

    if (!_session)
    {
        OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Connection failed.";
        _connected.store(false);
        return false;
    }

    // Set I/O timeouts to prevent hanging
    if (!_session->setTimeout(IO_TIMEOUT_MS))
    {
        OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex
                       << "] Failed to set socket timeouts.";
        _session.reset();
        _connected.store(false);
        return false;
    }

    OM_LOG_INFO() << "InterfaceClient[" << _interfaceIndex << "] Connected.";
    _connected.store(true);
    return true;
}

void InterfaceClient::disconnect()
{
    std::lock_guard<std::mutex> lock(_connectionMutex);
    if (_session)
    {
        _session->close();
        _session.reset();
    }
    _connected.store(false);
}

bool InterfaceClient::isConnected() const
{
    return _session && _session->isActive();
}

void InterfaceClient::forceShutdown()
{
    // Thread-safe force shutdown
    _shutdownRequested.store(true);
    _running.store(false);
    _queueCondition.notify_all();

    if (_session)
    {
        _session->forceShutdown();
    }
}

size_t InterfaceClient::getPendingRequestCount() const
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    return _requestQueue.size();
}

void InterfaceClient::queryAsync(const uint8_t* queryData, size_t querySize, QueryCallback callback)
{
    if (!_running.load())
    {
        OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex
                       << "] Cannot enqueue request: worker thread not running";
        if (callback)
        {
            InterfaceQueryResult emptyResult;
            callback(false, emptyResult);
        }
        return;
    }

    // Create request
    QueryRequest request;
    request.requestID = _requestID.fetch_add(1);
    request.queryData.assign(queryData, queryData + querySize);
    request.callback = callback;
    request.enqueueTime = std::chrono::steady_clock::now();

    // Enqueue the request
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _requestQueue.push(request);
    }

    // Notify worker thread
    _queueCondition.notify_one();

    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex << "] Request #" << request.requestID
                   << " enqueued (Queue size: " << getPendingRequestCount() << ")";
}

bool InterfaceClient::query(
    const uint8_t* queryData,
    size_t querySize,
    std::vector<uint8_t>& replyData,
    int timeout_ms)
{
    if (!_running.load())
    {
        OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex
                       << "] Cannot query: worker thread not running";
        return false;
    }

    // Synchronization primitives for blocking wait
    std::mutex resultMutex;
    std::condition_variable resultCondition;
    bool completed = false;
    bool success = false;
    InterfaceQueryResult fetchedResult;

    // Create callback that signals completion
    auto callback = [&resultMutex, &resultCondition, &completed, &success, &fetchedResult](
                        bool callbackSuccess, const InterfaceQueryResult& result) {
        std::lock_guard<std::mutex> lock(resultMutex);
        fetchedResult = result;
        success = callbackSuccess;
        completed = true;
        resultCondition.notify_one();
    };

    // Enqueue async request
    queryAsync(queryData, querySize, callback);

    // Wait for result with timeout
    std::unique_lock<std::mutex> lock(resultMutex);
    if (timeout_ms > 0)
    {
        bool timedOut = !resultCondition.wait_for(
            lock, std::chrono::milliseconds(timeout_ms), [&completed] { return completed; });

        if (timedOut)
        {
            OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Query timed out after "
                           << timeout_ms << "ms";
            return false;
        }
    }
    else
    {
        // Wait indefinitely
        resultCondition.wait(lock, [&completed] { return completed; });
    }

    // Copy result if successful
    if (success && fetchedResult.valid)
    {
        replyData.resize(fetchedResult.replyData.size());
        // if (fetchedResult.replyData.size() == replySize)
        {
            std::memcpy(
                replyData.data(), fetchedResult.replyData.data(), fetchedResult.replyData.size());
            return true;
        }
        // else
        //{
        //    OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Reply size mismatch: got
        //    "
        //                << fetchedResult.replyData.size() << ", expected " << replySize;
        //}
    }

    return false;
}

void InterfaceClient::workerThread()
{
    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex << "] Worker thread started";

    while (_running.load())
    {
        QueryRequest request;
        bool hasRequest = false;

        // Wait for request or shutdown signal
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _queueCondition.wait(
                lock, [this] { return !_requestQueue.empty() || !_running.load(); });

            if (!_running.load() && _requestQueue.empty())
            {
                break; // Shutdown requested and no more requests
            }

            if (!_requestQueue.empty())
            {
                request = _requestQueue.front();
                _requestQueue.pop();
                hasRequest = true;
            }
        }

        if (!hasRequest)
        {
            continue;
        }

        // Calculate queue time
        auto now = std::chrono::steady_clock::now();
        auto queueTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - request.enqueueTime)
                .count();

        OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex << "] Processing request #"
                       << request.requestID << " (Queued for " << queueTime << "ms)";

        // Process the request
        InterfaceQueryResult result;
        bool success = processQueryRequest(request, result);

        // Invoke callback
        if (request.callback)
        {
            try
            {
                request.callback(success, result);
            }
            catch (const std::exception& e)
            {
                OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex
                               << "] Exception in callback: " << e.what();
            }
        }
    }

    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex << "] Worker thread exiting";
}

bool InterfaceClient::processQueryRequest(const QueryRequest& request, InterfaceQueryResult& result)
{
    _totalQueries.fetch_add(1);

    // Ensure connected
    if (!connect())
    {
        OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Request #"
                       << request.requestID << " failed: cannot connect";
        return false;
    }

    // Send query and receive reply
    {
        std::lock_guard<std::mutex> lock(_connectionMutex);

        if (!_session || !_session->isActive())
        {
            OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Request #"
                           << request.requestID << " failed: session not active";
            return false;
        }

        // Send query data
        if (!_session->sendData(request.queryData.data(), request.queryData.size()))
        {
            OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Request #"
                           << request.requestID << " failed: cannot send";
            _session->close();
            return false;
        }

        // Receive reply
        // Header first
        RequestResponseHeader headerBuffer;
        int headerReceived =
            _session->receiveExact((uint8_t*)&headerBuffer, sizeof(RequestResponseHeader));
        if (headerReceived != sizeof(RequestResponseHeader))
        {
            OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Request #"
                           << request.requestID << " failed: invalid reply header";
            return false;
        }

        result.replyData.resize(headerBuffer.size());
        int received = _session->receive(
            result.replyData.data() + sizeof(RequestResponseHeader), headerBuffer.getPayloadSize());

        if (received != (int)headerBuffer.getPayloadSize())
        {
            if (_session->isActive())
            {
                OM_LOG_ERROR() << "InterfaceClient[" << _interfaceIndex << "] Request #"
                               << request.requestID << " failed: invalid reply size " << received;
            }
            _session->close();
            return false;
        }

        result.valid = true;
    }

    OM_LOG_DEBUG() << "InterfaceClient[" << _interfaceIndex << "] Request #" << request.requestID
                   << " completed successfully";
    return true;
}

} // namespace oracle