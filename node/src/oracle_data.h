#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace oracle
{

// TODO: replace with actual oracle data structure
struct OracleData
{
    std::string oracleId;
    double value;
    int64_t timestamp;
    std::string metadata; // JSON string
    bool valid;

    OracleData() : value(0), timestamp(0), valid(false) {}
};
}