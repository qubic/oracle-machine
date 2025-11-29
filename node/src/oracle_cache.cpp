#include "oracle_cache.h"

#include "logger.h"

namespace oracle
{

OracleCache::OracleCache(const Config& config) :
    _config(config),
    _totalRequests(0),
    _cacheHits(0),
    _cacheMisses(0),
    _lastCleanupTime(std::chrono::steady_clock::now())
{
    if (_config.enabled)
    {
        OM_LOG_INFO() << "[Cache] Initialized with TTL=" << _config.ttlSeconds
                      << "s, MaxEntries=" << _config.maxEntries;
    }
    else
    {
        OM_LOG_INFO() << "[Cache] Disabled";
    }
}

OracleCache::~OracleCache()
{
    printStats();
}

bool OracleCache::lookup(const OracleMachineQuery& query, CachedData& data)
{
    std::lock_guard<std::mutex> lock(_mutex);

    _totalRequests++;

    // Check if cache is enabled
    if (!_config.enabled)
    {
        _cacheMisses++;
        return false;
    }

    // Periodic cleanup check
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _lastCleanupTime).count();
    if (elapsed >= _config.cleanUpIntervalSec)
    {
        cleanupExpired();
        _lastCleanupTime = now;
    }

    // Create cache key
    CacheKey key = createKey(query);

    // Lookup in cache
    auto it = _cacheStorage.find(key);
    if (it == _cacheStorage.end())
    {
        // Not found
        _cacheMisses++;
        return false;
    }

    // Check if expired
    if (!isValid(it->second))
    {
        // Expired - remove and return miss
        _cacheStorage.erase(it);
        _cacheMisses++;
        return false;
    }

    // Cache hit!
    _cacheHits++;
    it->second.hitCount++;

    OM_LOG_DEBUG() << "[Cache] HIT - QueryId=" << query.oracleQueryId
                   << ", Interface=" << query.oracleInterfaceIndex
                   << ", Hits=" << it->second.hitCount
                   << ", PayloadSize=" << it->second.payloadData.size() << " bytes";

    // Get cached data
    data.payload = it->second.payloadData;
    data.errorFlag = it->second.errorFlag;

    return true;
}

void OracleCache::store(
    const OracleMachineQuery& query,
    const unsigned char* payload,
    size_t payload_size,
    unsigned short error_flags)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Check if cache is enabled
    if (!_config.enabled)
    {
        return;
    }

    // Don't cache error replies
    if (error_flags != 0)
    {
        OM_LOG_DEBUG() << "[Cache] Not caching error reply - QueryId=" << query.oracleQueryId
                       << ", ErrorFlags=" << error_flags;
        return;
    }

    // Check if cache is full
    if (_cacheStorage.size() >= _config.maxEntries)
    {
        OM_LOG_DEBUG() << "[Cache] Full - remove oldest entires";
        removeOldest();
    }

    // Create cache key
    CacheKey key = createKey(query);

    // Create cache entry
    CacheEntry entry;
    entry.payloadData.assign(payload, payload + payload_size);
    entry.errorFlag = error_flags;
    entry.timeStamp = std::chrono::steady_clock::now();
    entry.hitCount = 0;

    // Store in cache
    _cacheStorage[key] = std::move(entry);

    OM_LOG_DEBUG() << "[Cache] STORED - QueryId=" << query.oracleQueryId
                   << ", Interface=" << query.oracleInterfaceIndex
                   << ", PayloadSize=" << payload_size << " bytes"
                   << ", CacheSize=" << _cacheStorage.size();
}

void OracleCache::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _cacheStorage.clear();
    OM_LOG_DEBUG() << "[Cache] Cleared all entries";
}

void OracleCache::cleanInterface(unsigned int interface_index)
{
    std::lock_guard<std::mutex> lock(_mutex);

    size_t removed = 0;
    for (auto it = _cacheStorage.begin(); it != _cacheStorage.end();)
    {
        if (it->first.oracleInterfaceIndex == interface_index)
        {
            it = _cacheStorage.erase(it);
            removed++;
        }
        else
        {
            ++it;
        }
    }

    OM_LOG_DEBUG() << "[Cache] Cleared interface " << interface_index << " - Removed " << removed
                   << " entries";
}

OracleCache::Statistics OracleCache::getStats() const
{
    std::lock_guard<std::mutex> lock(_mutex);

    Statistics stats;
    stats.totalRequests = _totalRequests;
    stats.cacheHits = _cacheHits;
    stats.cacheMisses = _cacheMisses;
    stats.current_entries = _cacheStorage.size();

    return stats;
}

void OracleCache::printStats() const
{
    auto stats = getStats();

    OM_LOG_DEBUG() << "\n============================================================" << std::endl;
    OM_LOG_DEBUG() << "Cache Statistics" << std::endl;
    OM_LOG_DEBUG() << "============================================================" << std::endl;
    OM_LOG_DEBUG() << "Total requests:    " << stats.totalRequests << std::endl;
    OM_LOG_DEBUG() << "Cache hits:        " << stats.cacheHits << std::endl;
    OM_LOG_DEBUG() << "Cache misses:      " << stats.cacheMisses << std::endl;
    OM_LOG_DEBUG() << "Hit rate:          " << (stats.hitRate() * 100.0) << "%" << std::endl;
    OM_LOG_DEBUG() << "Current entries:   " << stats.current_entries << std::endl;
    OM_LOG_DEBUG() << "============================================================\n" << std::endl;
}

CacheKey OracleCache::createKey(const OracleMachineQuery& query) const
{
    CacheKey key;
    key.oracleQueryId = query.oracleQueryId;
    key.oracleInterfaceIndex = query.oracleInterfaceIndex;
    return key;
}

bool OracleCache::isValid(const CacheEntry& entry) const
{
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - entry.timeStamp).count();
    return age < _config.ttlSeconds;
}

void OracleCache::cleanupExpired()
{
    size_t removed = 0;
    for (auto it = _cacheStorage.begin(); it != _cacheStorage.end();)
    {
        if (!isValid(it->second))
        {
            it = _cacheStorage.erase(it);
            removed++;
        }
        else
        {
            ++it;
        }
    }

    if (removed > 0)
    {
        OM_LOG_DEBUG() << "[Cache] Cleanup removed " << removed << " expired entries";
    }
}

void OracleCache::removeOldest()
{
    // Find entry with oldest timestamp (LRU)
    auto oldest = _cacheStorage.begin();
    for (auto it = _cacheStorage.begin(); it != _cacheStorage.end(); ++it)
    {
        if (it->second.timeStamp < oldest->second.timeStamp)
        {
            oldest = it;
        }
    }

    if (oldest != _cacheStorage.end())
    {
        OM_LOG_DEBUG() << "[Cache] Oldest entry - QueryId=" << oldest->first.oracleQueryId;
        _cacheStorage.erase(oldest);
    }
}

} // namespace oracle