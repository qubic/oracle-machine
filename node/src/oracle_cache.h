#pragma once

#include <oracle_core/core_om_network_messages.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace oracle
{
// Wrap around, so that we can change later if neccessary
struct CacheKey
{
    // Use raw data so that we don't include the padding if any
    unsigned long long oracleQueryId;
    unsigned int oracleInterfaceIndex;

    bool operator==(const CacheKey& other) const
    {
        return oracleQueryId == other.oracleQueryId &&
               oracleInterfaceIndex == other.oracleInterfaceIndex;
    }
};

// Hash function for unordered_map
struct CacheKeyHash
{
    size_t operator()(const CacheKey& key) const
    {
        return std::hash<unsigned long long>()(key.oracleQueryId) ^
               ((unsigned long long)std::hash<unsigned int>()(key.oracleInterfaceIndex) << 1);
    }
};

struct CacheEntry
{
    std::vector<unsigned char> payloadData; // Interface-specific payload bytes
                                             // (e.g., numerator+denominator for Price)
    // Error flags from OracleMachineReply
    // TODO: depend on this error flag, should we re-update cached data ?
    unsigned short errorFlag;
    std::chrono::steady_clock::time_point timeStamp; // When cached
    // Stats
    uint32_t hitCount;
    CacheEntry() : errorFlag(0), hitCount(0) {}
};

class OracleCache
{
public:
    // Configuration
    struct Config
    {
        bool enabled;
        unsigned int ttlSeconds;          // Cache TTL (10 seconds default)
        unsigned int errorTtlSeconds;     // Error cache TTL (5 seconds default)
        size_t maxEntries;                // Max cache size
        unsigned int cleanUpIntervalSec; // Cleanup every 60 seconds
        Config()
        {
            enabled = true;
            ttlSeconds = 10;          // Cache TTL (10 seconds default)
            errorTtlSeconds = 5;      // Error cache TTL (5 seconds default)
            maxEntries = 100000;      // Max cache size
            cleanUpIntervalSec = 60; // Cleanup every 60 seconds
        }
    };

    // Constructor
    explicit OracleCache(const Config& config = Config());

    // Destructor
    ~OracleCache();

    struct CachedData
    {
        std::vector<unsigned char> payload; // Interface-specific payload
        unsigned short errorFlag;         // Error flags
    };

    // Lookup cached reply
    bool lookup(const OracleMachineQuery& query, CachedData& rely);

    // Store reply in cache
    void store(
        const OracleMachineQuery& query,
        const unsigned char* payload,
        size_t payload_size,
        unsigned short errorFlag);

    // Clear entire cache
    void clear();

    // Clear cache for specific interface
    void cleanInterface(unsigned int interfaceIndewx);

    // Get statistics
    struct Statistics
    {
        unsigned long long totalRequests;
        unsigned long long cacheHits;
        unsigned long long cacheMisses;
        size_t current_entries;

        double hitRate() const
        {
            if (totalRequests == 0)
                return 0.0;
            return static_cast<double>(cacheHits) / totalRequests;
        }
    };

    Statistics getStats() const;

    // Print statistics
    void printStats() const;

private:
    // Create cache key from query
    CacheKey createKey(const OracleMachineQuery& query) const;

    // Check if entry is still valid (not expired)
    bool isValid(const CacheEntry& entry) const;

    // Cleanup expired entries
    void cleanupExpired();

    // Remove oldest entry
    void removeOldest();

    // Configuration
    Config _config;

    // Cache storage
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> _cacheStorage;

    // Lock
    mutable std::mutex _mutex;

    // Statistics
    mutable unsigned long long _totalRequests;
    mutable unsigned long long _cacheHits;
    mutable unsigned long long _cacheMisses;

    // Last cleanup time
    std::chrono::steady_clock::time_point _lastCleanupTime;
};

} // namespace oracle
