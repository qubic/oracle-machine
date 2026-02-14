#pragma once

#include "base_oracle_service.h"
#include "football_provider.h"
#include "om_common/config.h"
#include "om_common/qpi_adapter.h"

#define ORACLE_INTERFACE_INDEX FOOTBALL_ORACLE_INTERFACE_INDEX
#include "oracle_interfaces/Football.h"
#undef ORACLE_INTERFACE_INDEX

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace oracle
{

// Football Interface Constants
constexpr size_t FOOTBALL_ORACLE_QUERY_SIZE = sizeof(Football::OracleQuery);
constexpr size_t FOOTBALL_ORACLE_REPLY_SIZE = sizeof(Football::OracleReply);

/**
 * Football Service
 * 
 * Provides football match data through various providers.
 */
class FootballService : public BaseOracleService
{
public:
    FootballService(const std::string& rHostname, uint16_t hostPort);
    ~FootballService() override;

    // Register a football provider
    void registerProvider(const std::string& oracleId, std::shared_ptr<FootballProvider> provider);

protected:
    // Implement BaseOracleService abstract methods
    uint16_t processInterfaceQuery(
        const std::vector<uint8_t>& queryPayload,
        std::vector<uint8_t>& replyPayload) override;

private:
    std::map<std::string, std::shared_ptr<FootballProvider>> _providers;
    std::mutex _providersMutex;

    // Helper: convert bytes to null-terminated string
    static std::string bytesToString(const char* data, size_t maxLen);
};

} // namespace oracle
