#pragma once

#include <cstdint>

static constexpr uint16_t OM_SERVER_PORT = 31841;
static constexpr char OC_SERVER_BIND[] = "10.29.1.16";
static constexpr uint64_t ORACLE_READ_TIMEOUT_MS = 5000;
static constexpr uint64_t MAX_PACKET_SIZE_IN_BYTES = 1024;

constexpr uint8_t NODE_LIST[][4] = {
    {10, 29, 1, 22},
};

struct InterfaceEndpoint
{
    uint32_t interfaceIndex; // 0 = Price, 1 = Weather
    const char* interfaceName;
    const char* serviceHost; // Aggregator service
    uint16_t servicePort;
    int cacheTTL; // Cache time to live in seconds. TODO: cache data so we can don't need to refetch
};

// The id must match the Oracle Interface index defined in oracle_interfaces/Price.h etc.
constexpr InterfaceEndpoint INTERFACE_ENDPOINTS[] = {
    {0, "Price", "0.0.0.0", 9001, 30},    // ONE for all price providers
    {1, "Weather", "192.168.1.21", 9002, 300}, // ONE for all weather providers
};

constexpr uint32_t ORACLE_INTERFACES_COUNT =
    sizeof(INTERFACE_ENDPOINTS) / sizeof(INTERFACE_ENDPOINTS[0]);
constexpr uint32_t NODE_COUNT = sizeof(NODE_LIST) / sizeof(NODE_LIST[0]);
