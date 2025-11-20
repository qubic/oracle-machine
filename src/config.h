#pragma once

#include <cstdint>

static constexpr uint16_t OM_SERVER_PORT = 31841;
static constexpr char OC_SERVER_BIND[] = "10.29.1.16";
static constexpr uint64_t ORACLE_READ_TIMEOUT_MS = 5000;

constexpr uint8_t NODE_LIST[][4] = {
    {10, 29, 1, 22},
};

struct OracleEndpoint
{
    const char* id;
    const char* name;
    const char* host;
    uint16_t port;
    int fetchIntervalMs;
};

constexpr OracleEndpoint ORACLE_LIST[] = {
    {"price",   "Price Oracle",   "192.168.1.20", 9001, 5000},
    {"weather", "Weather Oracle", "192.168.1.21", 9002, 60000},
    {"random",  "Random Oracle",  "localhost",    9003, 1000},
};

constexpr uint32_t ORACLE_COUNT = sizeof(ORACLE_LIST) / sizeof(ORACLE_LIST[0]);
constexpr uint32_t NODE_COUNT = sizeof(NODE_LIST) / sizeof(NODE_LIST[0]);

