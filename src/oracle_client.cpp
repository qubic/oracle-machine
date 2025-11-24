#include "oracle_client.h"

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define close(x) closesocket(x)
#define SHUT_RDWR SD_BOTH
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "config.h"
#include "logger.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

namespace oracle
{

// Limits for safety
static const size_t MAX_JSON_RESPONSE_SIZE = 1024 * 64; // 64 KB limit
static const int CONNECT_TIMEOUT_MS = 3000;
static const int IO_TIMEOUT_MS = 5000; // 5 seconds to read/write

static std::string extract_json_string(const std::string& json, const std::string& key);
static double extract_json_number(const std::string& json, const std::string& key);
static int64_t extract_json_int(const std::string& json, const std::string& key);
static bool extract_json_bool(const std::string& json, const std::string& key);

OracleClient::OracleClient(
    const std::string& id,
    const std::string& name,
    const std::string& host,
    uint16_t port) :
    _id(id),
    _name(name),
    _host(host),
    _port(port),
    _connected(false),
    _requestID(0),
    _running(false),
    _shutdownRequested(false)
{
}

OracleClient::~OracleClient()
{
    stop();
}

bool OracleClient::start()
{
    if (_running.load())
    {
        LOG_INFO() << "[" << _id << "] Already running";
        return true;
    }

    LOG_INFO() << "[" << _id << "] Starting request processing thread...";

    _running.store(true);
    _shutdownRequested.store(false);

    _workerThread = std::thread(&OracleClient::workerThread, this);

    LOG_INFO() << "[" << _id << "] Request processing thread started";
    return true;
}

void OracleClient::stop()
{
    if (!_running.load())
    {
        return;
    }

    LOG_INFO() << "[" << _id << "] Stopping request processing thread...";

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

    LOG_INFO() << "[" << _id << "] Request processing thread stopped";
}

bool OracleClient::connect()
{
    std::lock_guard<std::mutex> lock(_connectionMutex);

    // If already connected and active, return true
    if (_session && _session->isActive())
    {
        return true;
    }

    // Reset session if it existed but was inactive
    _session.reset();

    LOG_INFO() << "[" << _id << "] Connecting to " << _host << ":" << _port << "...";

    // TcpClient::connect returns a unique_ptr<Session>
    _session = _tcpClient.connect(_host, _port, CONNECT_TIMEOUT_MS);

    if (!_session)
    {
        LOG_ERROR()<< "[" << _id << "] Connection failed.";
        _connected.store(false);
        return false;
    }

    // Set I/O timeouts to prevent hanging
    if (!_session->setTimeout(IO_TIMEOUT_MS))
    {
        LOG_ERROR()<< "[" << _id << "] Failed to set socket timeouts.";
        _session.reset();
        _connected.store(false);
        return false;
    }

    LOG_INFO() << "[" << _id << "] Connected.";
    _connected.store(true);
    return true;
}

void OracleClient::disconnect()
{
    std::lock_guard<std::mutex> lock(_connectionMutex);
    if (_session)
    {
        _session->close();
        _session.reset();
    }
    _connected.store(false);
}

bool OracleClient::isConnected() const
{
    return _session && _session->isActive();
}

void OracleClient::forceShutdown()
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

size_t OracleClient::getPendingRequestCount() const
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    return _requestQueue.size();
}

void OracleClient::fetchAsync(FetchCallback callback)
{
    if (!_running.load())
    {
        LOG_ERROR()<< "[" << _id << "] Cannot enqueue request: worker thread not running"
                 ;
        if (callback)
        {
            OracleData emptyData;
            emptyData.oracleId = _id;
            callback(false, emptyData);
        }
        return;
    }

    FetchRequest request;
    request.requestID = ++_requestID;
    request.callback = callback;
    request.enqueueTime = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _requestQueue.push(request);
    }

    // Notify worker thread
    _queueCondition.notify_one();

    LOG_INFO() << "[" << _id << "] Request #" << request.requestID
              << " enqueued (Queue size: " << getPendingRequestCount() << ")";
}

bool OracleClient::fetch(OracleData& data, int timeout_ms)
{
    if (!_running.load())
    {
        LOG_ERROR()<< "[" << _id << "] Cannot fetch: worker thread not running";
        return false;
    }

    // Synchronization primitives for blocking wait
    std::mutex resultMutex;
    std::condition_variable resultCondition;
    bool completed = false;
    bool success = false;
    OracleData fetchedData;

    // Create callback that signals completion
    auto callback = [&resultMutex, &resultCondition, &completed, &success, &fetchedData](
                        bool callbackSuccess, const OracleData& d) {
        std::lock_guard<std::mutex> lock(resultMutex);
        fetchedData = d;
        success = callbackSuccess;
        completed = true;
        resultCondition.notify_one();
    };

    // Enqueue async request
    fetchAsync(callback);

    // Wait for result with timeout
    std::unique_lock<std::mutex> lock(resultMutex);
    if (timeout_ms > 0)
    {
        bool timedOut = !resultCondition.wait_for(
            lock, std::chrono::milliseconds(timeout_ms), [&completed] { return completed; });

        if (timedOut)
        {
            LOG_ERROR()<< "[" << _id << "] Fetch request timed out after " << timeout_ms << "ms"
                     ;
            return false;
        }
    }
    else
    {
        // Wait indefinitely
        resultCondition.wait(lock, [&completed] { return completed; });
    }

    // Copy result if successful
    if (success)
    {
        data = fetchedData;
    }

    return success;
}

void OracleClient::workerThread()
{
    LOG_INFO() << "[" << _id << "] Worker thread started";

    while (_running.load())
    {
        FetchRequest request;
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

        LOG_INFO() << "[" << _id << "] Processing request #" << request.requestID << " (Queued for "
                  << queueTime << "ms)";

        // Process the request
        OracleData data;
        bool success = processFetchRequest(request, data);

        // Invoke callback
        if (request.callback)
        {
            try
            {
                request.callback(success, data);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR()<< "[" << _id << "] Exception in callback: " << e.what();
            }
        }
    }

    LOG_INFO() << "[" << _id << "] Worker thread exiting";
}

bool OracleClient::processFetchRequest(const FetchRequest& request, OracleData& data)
{
    // Initialize data
    data.oracleId = _id;
    data.value = 0.0;
    data.timestamp = 0;
    data.valid = false;

    // Ensure connected
    if (!connect())
    {
        LOG_ERROR()<< "[" << _id << "] Request #" << request.requestID << " failed: cannot connect"
                 ;
        return false;
    }

    // Build request JSON
    std::ostringstream oss;
    oss << "{\"type\":\"fetch\",\"request_id\":" << request.requestID << ",\"oracle_id\":\"" << _id
        << "\"}";

    // Send request
    {
        std::lock_guard<std::mutex> lock(_connectionMutex);

        if (!_session || !_session->isActive())
        {
            LOG_ERROR()<< "[" << _id << "] Request #" << request.requestID
                      << " failed: session not active";
            return false;
        }

        if (!sendJSONMessage(*_session, oss.str()))
        {
            LOG_ERROR()<< "[" << _id << "] Request #" << request.requestID << " failed: cannot send"
                     ;
            _session->close();
            return false;
        }

        // Receive response
        std::string response = receiveJSONMessage(*_session);
        if (response.empty())
        {
            if (_session->isActive())
            {
                LOG_ERROR()<< "[" << _id << "] Request #" << request.requestID
                          << " failed: empty response";
            }
            _session->close();
            return false;
        }

        // Parse response
        if (!parseResponse(response, data))
        {
            LOG_ERROR()<< "[" << _id << "] Request #" << request.requestID << " failed: parse error"
                     ;
            return false;
        }
    }

    LOG_INFO() << "[" << _id << "] Request #" << request.requestID << " completed successfully"
             ;
    return true;
}

bool OracleClient::sendJSONMessage(Session& session, const std::string& json)
{
    std::string message = json + "\n";
    return session.sendData((const uint8_t*)message.c_str(), message.size());
}

std::string OracleClient::receiveJSONMessage(Session& session)
{
    std::string result;
    result.reserve(1024);

    uint8_t buffer[1024];

    while (session.isActive())
    {
        // DoS protection
        if (result.size() > MAX_JSON_RESPONSE_SIZE)
        {
            LOG_ERROR()<< "[" << _id << "] Error: Response exceeded max size ("
                      << MAX_JSON_RESPONSE_SIZE << " bytes)";
            return "";
        }

        int received = session.receive(buffer, sizeof(buffer));
        if (received <= 0)
        {
            break;
        }

        // Scan for newline
        bool found_newline = false;
        for (int i = 0; i < received; ++i)
        {
            if (buffer[i] == '\n')
            {
                result.append((char*)buffer, i);
                found_newline = true;
                break;
            }
        }

        if (found_newline)
        {
            break;
        }

        result.append((char*)buffer, received);
    }

    // Clean up CR/LF
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
    {
        result.pop_back();
    }

    return result;
}

bool OracleClient::parseResponse(const std::string& response, OracleData& data)
{
    data.oracleId = _id;
    data.value = extract_json_number(response, "value");
    data.timestamp = extract_json_int(response, "timestamp");
    data.valid = extract_json_bool(response, "valid");

    // Set current timestamp if not provided
    if (data.timestamp == 0)
    {
        data.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    }

    return true;
}

// ============================================================================
// JSON Parsing Helpers
// ============================================================================

static std::string extract_json_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos)
        return "";
    size_t start = json.find('"', pos);
    if (start == std::string::npos)
        return "";
    size_t end = json.find('"', start + 1);
    if (end == std::string::npos)
        return "";
    return json.substr(start + 1, end - start - 1);
}

static double extract_json_number(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return 0.0;
    pos = json.find(':', pos);
    if (pos == std::string::npos)
        return 0.0;

    size_t start = json.find_first_of("-0123456789", pos);
    if (start == std::string::npos)
        return 0.0;

    size_t end = start;
    while (end < json.size() && (isdigit(json[end]) || json[end] == '.' || json[end] == '-'))
        end++;

    try
    {
        return std::stod(json.substr(start, end - start));
    }
    catch (...)
    {
        return 0.0;
    }
}

static int64_t extract_json_int(const std::string& json, const std::string& key)
{
    return (int64_t)extract_json_number(json, key);
}

static bool extract_json_bool(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos)
        return false;
    return json.find("true", pos) != std::string::npos;
}

} // namespace oracle
