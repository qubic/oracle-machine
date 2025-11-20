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
#include "oracle_client.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

namespace oracle
{

#ifdef _MSC_VER
static bool set_timeout(int socket, int opt_name, unsigned long milliseconds)
{
    DWORD tv = milliseconds;
    if (setsockopt(socket, SOL_SOCKET, opt_name, (const char*)&tv, sizeof(tv)) != 0)
    {
        return false;
    }
    return true;
}
#else
static bool set_timeout(int socket, int opt_name, unsigned long milliseconds)
{
    struct timeval tv;
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    if (setsockopt(socket, SOL_SOCKET, opt_name, (const char*)&tv, sizeof(tv)) != 0)
    {
        return false;
    }
    return true;
}
#endif

OracleClient::OracleClient(
    const std::string& id,
    const std::string& name,
    const std::string& host,
    uint16_t port) :
    _id(id), _name(name), _host(host), _port(port), _socketFd(-1), _connected(false), _requestID(0)
{
#ifdef _MSC_VER
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 0), &wsa_data);
#endif
}

OracleClient::~OracleClient()
{
    disconnect();
#ifdef _MSC_VER
    WSACleanup();
#endif
}

bool OracleClient::connect()
{
    if (_connected)
    {
        return true;
    }

    // Create socket
    _socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_socketFd < 0)
    {
        std::cerr << "[" << _id << "] Failed to create socket" << std::endl;
        return false;
    }

    // Set timeouts
    if (!set_timeout(_socketFd, SO_RCVTIMEO, ORACLE_READ_TIMEOUT_MS))
    {
        close(_socketFd);
        _socketFd = -1;
        return false;
    }
    if (!set_timeout(_socketFd, SO_SNDTIMEO, ORACLE_READ_TIMEOUT_MS))
    {
        close(_socketFd);
        _socketFd = -1;
        return false;
    }

    // Setup address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);

    if (inet_pton(AF_INET, _host.c_str(), &addr.sin_addr) <= 0)
    {
        std::cerr << "[" << _id << "] Failed to resolve host: " << _host << std::endl;
        close(_socketFd);
        _socketFd = -1;
        return false;
    }

    // Connect
    if (::connect(_socketFd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "[" << _id << "] Failed to connect to " << _host << ":" << _port << std::endl;
        close(_socketFd);
        _socketFd = -1;
        return false;
    }

    _connected = true;
    std::cout << "[" << _id << "] Connected to " << _host << ":" << _port << std::endl;
    return true;
}

void OracleClient::disconnect()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_socketFd >= 0)
    {
        shutdown(_socketFd, SHUT_RDWR);
        close(_socketFd);
        _socketFd = -1;
    }
    _connected = false;
}

bool OracleClient::isConnected() const
{
    return _connected;
}

int OracleClient::receiveData(uint8_t* buffer, int sz)
{
    int total_received = 0;
    while (sz > 0)
    {
        int received = recv(_socketFd, (char*)buffer + total_received, sz, 0);
        if (received <= 0)
        {
            break;
        }
        total_received += received;
        sz -= received;
    }
    return total_received;
}

bool OracleClient::sendJSONMessage(const std::string& json)
{
    if (!_connected)
        return false;

    // Send JSON with newline delimiter
    std::string message = json + "\n";
    int total_sent = 0;
    int msg_size = (int)message.size();

    while (total_sent < msg_size)
    {
        int sent = send(_socketFd, message.c_str() + total_sent, msg_size - total_sent, 0);
        if (sent <= 0)
        {
            return false;
        }
        total_sent += sent;
    }

    return true;
}

std::string OracleClient::receiveJSONMessage()
{
    // Read until newline
    std::string result;
    char buffer[4096];

    while (true)
    {
        int received = recv(_socketFd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0)
        {
            break;
        }
        buffer[received] = '\0';
        result += buffer;

        // Check for newline (message delimiter)
        if (result.find('\n') != std::string::npos)
        {
            break;
        }
    }

    // Remove trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
    {
        result.pop_back();
    }

    return result;
}

// Simple JSON parsing helpers
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

    // Build JSON fetch request
    std::ostringstream oss;
    oss << "{\"type\":\"fetch\",\"request_id\":" << (++_requestID) << ",\"oracle_id\":\"" << _id
        << "\"}";

    if (!sendJSONMessage(oss.str()))
    {
        disconnect();
        return false;
    }

    // Receive JSON response
    std::string response = receiveJSONMessage();
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

    // Build JSON ping request
    std::ostringstream oss;
    oss << "{\"type\":\"ping\",\"request_id\":" << (++_requestID) << "}";

    if (!sendJSONMessage(oss.str()))
    {
        disconnect();
        return false;
    }

    // Receive JSON response
    std::string response = receiveJSONMessage();
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
