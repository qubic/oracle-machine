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
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

namespace oracle
{

OracleClient::OracleClient(
    const std::string& id,
    const std::string& name,
    const std::string& host,
    uint16_t port) :
    _id(id), _name(name), _host(host), _port(port), _connected(false), _requestID(0)
{
    // TcpClient handles socket initialization (Windows/Linux)
}

bool OracleClient::connect()
{
    if (_connected)
    {
        return true;
    }

    // Use TcpClient to create connection
    _session = _tcpClient.connect(_host, _port);

    if (!_session)
    {
        std::cerr << "[" << _id << "] Failed to connect to " << _host << ":" << _port << std::endl;
        return false;
    }

    // TODO: Set timeouts on the session if needed
    // This could be added to Session class:
    // _session->setTimeout(ORACLE_READ_TIMEOUT_MS);

    _connected = true;
    std::cout << "[" << _id << "] Connected to " << _host << ":" << _port << std::endl;
    return true;
}

void OracleClient::disconnect()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_session)
    {
        _session->close();
        _session.reset();
    }

    _connected = false;
}

bool OracleClient::isConnected() const
{
    return _connected && _session && _session->isActive();
}

bool OracleClient::sendJSONMessage(Session& session, const std::string& json)
{
    // Send JSON with newline delimiter
    std::string message = json + "\n";

    return session.sendData((const uint8_t*)message.c_str(), message.size());
}

std::string OracleClient::receiveJSONMessage(Session& session)
{
    // Read until newline
    std::string result;
    uint8_t buffer[4096];

    while (session.isActive())
    {
        int received = session.receive(buffer, sizeof(buffer) - 1);
        if (received <= 0)
        {
            break;
        }

        buffer[received] = '\0';
        result.append((char*)buffer, received);

        // Check for newline (message delimiter)
        if (result.find('\n') != std::string::npos)
        {
            break;
        }
    }

    // Remove trailing newline/carriage return
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
    {
        result.pop_back();
    }

    return result;
}

// Simple JSON parsing helpers (unchanged)
static std::string extract_json_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos)
        return "";

    pos = json.find('"', pos);
    if (pos == std::string::npos)
        return "";

    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos)
        return "";

    return json.substr(pos + 1, end - pos - 1);
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

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    {
        pos++;
    }

    // Find end of number
    size_t end = pos;
    while (end < json.size() && (isdigit(json[end]) || json[end] == '.' || json[end] == '-' ||
                                 json[end] == 'e' || json[end] == 'E' || json[end] == '+'))
    {
        end++;
    }

    if (end == pos)
        return 0.0;

    return std::stod(json.substr(pos, end - pos));
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

bool OracleClient::fetch(OracleData& data)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_connected && !connect())
    {
        return false;
    }

    if (!_session || !_session->isActive())
    {
        disconnect();
        return false;
    }

    // Build JSON fetch request
    std::ostringstream oss;
    oss << "{\"type\":\"fetch\",\"request_id\":" << (++_requestID) << ",\"oracle_id\":\"" << _id
        << "\"}";

    // Send via Session
    if (!sendJSONMessage(*_session, oss.str()))
    {
        disconnect();
        return false;
    }

    // Receive via Session
    std::string response = receiveJSONMessage(*_session);
    if (response.empty())
    {
        disconnect();
        return false;
    }

    // Parse JSON response
    std::string status = extract_json_string(response, "status");
    if (status != "ok")
    {
        std::cerr << "[" << _id << "] Fetch failed: " << extract_json_string(response, "error")
                  << std::endl;
        return false;
    }

    // Extract data
    data.oracleId = _id;
    data.value = extract_json_number(response, "value");
    data.timestamp = extract_json_int(response, "timestamp");
    data.valid = extract_json_bool(response, "valid");

    if (data.timestamp == 0)
    {
        // Use current time if not provided
        data.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    }

    if (!extract_json_bool(response, "valid"))
    {
        // If 'valid' field not present, assume valid
        data.valid = true;
    }

    return true;
}

bool OracleClient::ping()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_connected && !connect())
    {
        return false;
    }

    if (!_session || !_session->isActive())
    {
        disconnect();
        return false;
    }

    // Build JSON ping request
    std::ostringstream oss;
    oss << "{\"type\":\"ping\",\"request_id\":" << (++_requestID) << "}";

    // Send via Session
    if (!sendJSONMessage(*_session, oss.str()))
    {
        disconnect();
        return false;
    }

    // Receive via Session
    std::string response = receiveJSONMessage(*_session);
    if (response.empty())
    {
        disconnect();
        return false;
    }

    // Check for pong
    std::string type = extract_json_string(response, "type");
    return (type == "pong");
}

} // namespace oracle
