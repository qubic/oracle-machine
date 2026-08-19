#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ORACLE INTERFACE INDEX
// =============================================================================
constexpr const uint32_t PRICE_ORACLE_INTERFACE_INDEX = 0;
constexpr const uint32_t MOCK_ORACLE_INTERFACE_INDEX = 1;
constexpr const uint32_t DOGE_ORACLE_INTERFACE_INDEX = 2;
constexpr const uint32_t READ_EVM_LOG_INTERFACE_INDEX = 3;
constexpr const uint32_t READ_QUBIC_LOG_INTERFACE_INDEX = 4;
constexpr const uint32_t MAX_ORACLE_INTERFACES_SUPPORT = 1024;

// ENVIRONMENT VARIABLE NAMES
// =============================================================================

namespace ConfigEnv
{
constexpr const char* OM_SERVER_PORT = "OM_SERVER_PORT";
constexpr const char* QUBIC_NODES = "QUBIC_NODES";

constexpr const char* PRICE_SERVICE_HOST = "PRICE_SERVICE_HOST";
constexpr const char* PRICE_SERVICE_PORT = "PRICE_SERVICE_PORT";

constexpr const char* MOCK_SERVICE_HOST = "MOCK_SERVICE_HOST";
constexpr const char* MOCK_SERVICE_PORT = "MOCK_SERVICE_PORT";

constexpr const char* DOGE_SERVICE_HOST = "DOGE_SERVICE_HOST";
constexpr const char* DOGE_SERVICE_PORT = "DOGE_SERVICE_PORT";

constexpr const char* READ_EVM_LOG_SERVICE_HOST = "READ_EVM_LOG_SERVICE_HOST";
constexpr const char* READ_EVM_LOG_SERVICE_PORT = "READ_EVM_LOG_SERVICE_PORT";

constexpr const char* READ_QUBIC_LOG_SERVICE_HOST = "READ_QUBIC_LOG_SERVICE_HOST";
constexpr const char* READ_QUBIC_LOG_SERVICE_PORT = "READ_QUBIC_LOG_SERVICE_PORT";
} // namespace ConfigEnv

struct NodeEndpoint
{
    std::array<uint8_t, 4> ip;

    std::string ipString() const
    {
        return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) +
               "." + std::to_string(ip[3]);
    }
};

struct InterfaceEndpoint
{
    uint32_t interfaceIndex;
    std::string interfaceName;
    std::string serviceHost;
    uint16_t servicePort;
};

enum class ConfigSource { Default, Env };
// Configuration for Oracle Machine Node
class Config
{
public:
    // Singleton access
    static Config& instance()
    {
        static Config instance;
        return instance;
    }

    // Load configuration from env variable
    // Unset variable will be set as default
    void loadFromEnv();

    const std::vector<NodeEndpoint>& nodes() const { return _nodes; }
    const std::vector<InterfaceEndpoint>& interfaces() const { return _interfaces; }

    uint32_t nodeCount() const { return static_cast<uint32_t>(_nodes.size()); }
    uint32_t interfaceCount() const { return static_cast<uint32_t>(_interfaces.size()); }

    uint16_t getPort() const {return _serverPort;};

    // Find interface by index
    const InterfaceEndpoint* findInterface(uint32_t index) const
    {
        for (const auto& it : _interfaces)
        {
            if (it.interfaceIndex == index)
                return &it;
        }
        return nullptr;
    }

        // Get source by variable address
    template<typename T>
    ConfigSource getSource(const T& var) const
    {
        auto it = _sources.find(static_cast<const void*>(&var));
        return (it != _sources.end()) ? it->second : ConfigSource::Default;
    }

    // Get source tag by variable address
    template<typename T>
    const char* sourceTag(const T& var) const
    {
        return getSource(var) == ConfigSource::Env ? "(env)" : "";
    }


private:
    // Force use the singleton
    Config() { };
    // Non copyable
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    void print() const;
    void setSource(const void* addr, ConfigSource src)
    {
        _sources[addr] = src;
    }

    uint16_t _serverPort;
    std::vector<NodeEndpoint> _nodes;
    std::vector<InterfaceEndpoint> _interfaces;

    // Mainly for debug and tracking
    std::map<const void*, ConfigSource> _sources;
};

// #define OM_SERVER_PORT           Config::instance().server().port
// #define OC_SERVER_BIND           Config::instance().server().bindAddress.c_str()
// #define NODE_COUNT               Config::instance().nodeCount()
// #define ORACLE_INTERFACES_COUNT  Config::instance().interfaceCount()