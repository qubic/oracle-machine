#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <string>

// This config will be use
namespace ConfigDefaults
{
// Server settings
constexpr uint16_t SERVER_PORT = 31841;
constexpr const char* SERVER_BIND_ADDRESS = "0.0.0.0";

// Default whitelist nodes
constexpr uint8_t NODES[][4] = {
    {127, 0, 0, 1},
};
constexpr uint32_t NODE_COUNT = sizeof(NODES) / sizeof(NODES[0]);

// Default interface endpoints
struct InterfaceDefault
{
    uint32_t index;
    const char* name;
    const char* host;
    uint16_t port;
    int cacheTTL;
};

constexpr InterfaceDefault INTERFACES[] = {
    {0, "Price", "0.0.0.0", 9001, 30},
    {1, "Weather", "0.0.0.0", 9002, 300},
    // Add more interfaces here
};
constexpr uint32_t INTERFACE_COUNT = sizeof(INTERFACES) / sizeof(INTERFACES[0]);
} // namespace ConfigDefaults

struct ServerConfig
{
    uint16_t port = ConfigDefaults::SERVER_PORT;
    std::string bindAddress = ConfigDefaults::SERVER_BIND_ADDRESS;
};

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
    int cacheTTL;
};

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

    // Load configuration from YAML file
    // Returns true if loaded from file, false if using defaults
    bool load(const std::string& filepath = "");

    // Check if config was loaded from file
    bool isLoadedFromFile() const { return _loadedFromFile; }

    const ServerConfig& server() const { return _server; }
    const std::vector<NodeEndpoint>& nodes() const { return _nodes; }
    const std::vector<InterfaceEndpoint>& interfaces() const { return _interfaces; }

    uint32_t nodeCount() const { return static_cast<uint32_t>(_nodes.size()); }
    uint32_t interfaceCount() const { return static_cast<uint32_t>(_interfaces.size()); }

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

private:
    // Force use the singleton
    Config() { loadDefaults(); }
    // Non copyable
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    void loadDefaults();

    ServerConfig _server;
    std::vector<NodeEndpoint> _nodes;
    std::vector<InterfaceEndpoint> _interfaces;
    bool _loadedFromFile = false;
};


// #define OM_SERVER_PORT           Config::instance().server().port
// #define OC_SERVER_BIND           Config::instance().server().bindAddress.c_str()
// #define NODE_COUNT               Config::instance().nodeCount()
// #define ORACLE_INTERFACES_COUNT  Config::instance().interfaceCount()