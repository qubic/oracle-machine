#pragma once

#include <cstdint>
#include <string>

// This default config will be used in case of no external config file provided
namespace ConfigDefaults
{
// Server settings
constexpr uint16_t SERVER_PORT = 31841;
constexpr const char* SERVER_BIND_ADDRESS = "0.0.0.0";

// Default whitelist nodes
static const std::string NODES("10.29.1.22");  // Comma-separated

// Default interface endpoints
struct InterfaceDefault
{
    uint32_t index;
    const char* name;
    const char* host;
    uint16_t port;
};

constexpr InterfaceDefault INTERFACES[] = {
    {0, "Price", "0.0.0.0", 9001},
    {1, "Mock", "0.0.0.0", 9002},
    {2, "Doge", "0.0.0.0", 9003},
    {3, "ReadEvmLog", "0.0.0.0", 9005},
    {4, "ReadQubicLog", "0.0.0.0", 9006},
    // Add more interfaces here
};
constexpr uint32_t INTERFACE_COUNT = sizeof(INTERFACES) / sizeof(INTERFACES[0]);
} // namespace ConfigDefaults