#include "om_common/config.h"
#include "om_common/default_config.h"
#include "om_common/logger.h"

#include <fstream>
#include <iostream>
#include <sstream>

static std::array<uint8_t, 4> parseIpAddress(const std::string& ipStr)
{
    std::array<uint8_t, 4> ip = {0, 0, 0, 0};
    std::istringstream iss(ipStr);
    std::string octet;
    int i = 0;

    while (std::getline(iss, octet, '.') && i < 4)
    {
        ip[i++] = static_cast<uint8_t>(std::stoi(octet));
    }

    if (i != 4)
    {
        throw std::runtime_error("Invalid IP address format: " + ipStr);
    }

    return ip;
}
// Parse comma-separated IPs
std::vector<NodeEndpoint> parseNodes(const std::string& nodesStr)
{
    std::vector<NodeEndpoint> nodes;
    std::istringstream iss(nodesStr);
    std::string ip;

    while (std::getline(iss, ip, ','))
    {
        // Trim whitespace
        ip.erase(0, ip.find_first_not_of(" \t"));
        ip.erase(ip.find_last_not_of(" \t") + 1);

        if (!ip.empty())
        {
            NodeEndpoint node;
            node.ip = parseIpAddress(ip);
            nodes.push_back(node);
        }
    }
    return nodes;
}

void Config::loadFromEnv()
{
     std::cout << "[Config] Loading from environment (with defaults fallback)" << std::endl;
    int defaultCheck = 0;

    // Clear sources map
    _sources.clear();

    // get string from ENV or default
    auto getStr = [this](const char* envName, const char* defaultVal, std::string& target)
    {
        const char* val = std::getenv(envName);
        target = val ? val : defaultVal; 
        setSource(&target, val ? ConfigSource::Env : ConfigSource::Default);
    };

    // get uint16 from ENV or default
    auto getUint16 = [this](const char* envName, uint16_t defaultVal, uint16_t& target)
    {
        const char* val = std::getenv(envName);
        target = val ? static_cast<uint16_t>(std::atoi(val)) : defaultVal;
        setSource(&target, val ? ConfigSource::Env : ConfigSource::Default);
    };

    //  get int from ENV or default
    auto getInt = [this](const char* envName, int defaultVal, int& target)
    {
        const char* val = std::getenv(envName);
        target = val ? std::atoi(val) : defaultVal;
        setSource(&target, val ? ConfigSource::Env : ConfigSource::Default);
    };

    // Server port
    getUint16(ConfigEnv::OM_SERVER_PORT, ConfigDefaults::SERVER_PORT, _serverPort);

    // Nodes
    std::string nodesStr;
    getStr(ConfigEnv::QUBIC_NODES, ConfigDefaults::NODES.c_str(), nodesStr);
    _nodes = parseNodes(nodesStr);
    setSource(&_nodes, getSource(nodesStr));

    // Interfaces.
    _interfaces.clear();
    //_interfaces.resize(MAX_ORACLE_INTERFACES_SUPPORT);

    // Price interface (index 0)
    {
        InterfaceEndpoint interfaceEndpoint;

        interfaceEndpoint.interfaceIndex = PRICE_ORACLE_INTERFACE_INDEX;
        interfaceEndpoint.interfaceName = "Price";
        getStr(ConfigEnv::PRICE_SERVICE_HOST, ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].host, interfaceEndpoint.serviceHost);
        getUint16(ConfigEnv::PRICE_SERVICE_PORT, ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].port, interfaceEndpoint.servicePort);
        _interfaces.push_back(interfaceEndpoint);
    }

    // Mock interface (index 1)
    {
        InterfaceEndpoint interfaceEndpoint;
        interfaceEndpoint.interfaceIndex = MOCK_ORACLE_INTERFACE_INDEX;
        interfaceEndpoint.interfaceName = "Mock";
        getStr(ConfigEnv::MOCK_SERVICE_HOST, ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].host, interfaceEndpoint.serviceHost);
        getUint16(ConfigEnv::MOCK_SERVICE_PORT, ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].port, interfaceEndpoint.servicePort);
        _interfaces.push_back(interfaceEndpoint);
    }

    // Doge share validation interface (index 2)
    {
        InterfaceEndpoint interfaceEndpoint;
        interfaceEndpoint.interfaceIndex = DOGE_ORACLE_INTERFACE_INDEX;
        interfaceEndpoint.interfaceName = "Doge";
        getStr(
            ConfigEnv::DOGE_SERVICE_HOST,
            ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].host,
            interfaceEndpoint.serviceHost);
        getUint16(
            ConfigEnv::DOGE_SERVICE_PORT,
            ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].port,
            interfaceEndpoint.servicePort);
        _interfaces.push_back(interfaceEndpoint);
    }

    // Read EVM log interface (index 3) - generic single-log EVM read
    {
        InterfaceEndpoint interfaceEndpoint;
        interfaceEndpoint.interfaceIndex = READ_EVM_LOG_INTERFACE_INDEX;
        interfaceEndpoint.interfaceName = "ReadEvmLog";
        getStr(
            ConfigEnv::READ_EVM_LOG_SERVICE_HOST,
            ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].host,
            interfaceEndpoint.serviceHost);
        getUint16(
            ConfigEnv::READ_EVM_LOG_SERVICE_PORT,
            ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].port,
            interfaceEndpoint.servicePort);
        _interfaces.push_back(interfaceEndpoint);
    }

    // Generic read-qubic-log interface slot (index 4)
    {
        InterfaceEndpoint interfaceEndpoint;
        interfaceEndpoint.interfaceIndex = READ_QUBIC_LOG_INTERFACE_INDEX;
        interfaceEndpoint.interfaceName = "ReadQubicLog";
        getStr(
            ConfigEnv::READ_QUBIC_LOG_SERVICE_HOST,
            ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].host,
            interfaceEndpoint.serviceHost);
        getUint16(
            ConfigEnv::READ_QUBIC_LOG_SERVICE_PORT,
            ConfigDefaults::INTERFACES[interfaceEndpoint.interfaceIndex].port,
            interfaceEndpoint.servicePort);
        _interfaces.push_back(interfaceEndpoint);
    }

    print();

}


void Config::print() const
{
   std::cout << "[Config] Server port: " << _serverPort
              << " " << sourceTag(_serverPort) << std::endl;

    std::cout << "[Config] Nodes (" << _nodes.size() << ") "
              << sourceTag(_nodes) << ":" << std::endl;
    for (const auto& node : _nodes)
    {
        std::cout << "  - " << node.ipString() << std::endl;
    }

    std::cout << "[Config] Interfaces (" << _interfaces.size() << "):" << std::endl;
    for (const auto& iface : _interfaces)
    {
        std::cout << "  - [" << iface.interfaceIndex << "] " << iface.interfaceIndex << std::endl;
        std::cout << "      host: " << iface.serviceHost << " " << sourceTag(iface.serviceHost) << std::endl;
        std::cout << "      port: " << iface.servicePort << " " << sourceTag(iface.servicePort) << std::endl;
    }
}