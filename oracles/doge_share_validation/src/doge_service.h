#pragma once

#include "base_oracle_service.h"

#include "om_common/config.h"
#include "om_common/qpi_adapter.h"

#define ORACLE_INTERFACE_INDEX DOGE_ORACLE_INTERFACE_INDEX
#include "oracle_interfaces/DogeShareValidation.h"
#undef ORACLE_INTERFACE_INDEX

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace oracle
{
// DogeShareValidation Interface Constants
constexpr size_t DOGE_ORACLE_QUERY_SIZE = sizeof(DogeShareValidation::OracleQuery);
constexpr size_t DOGE_ORACLE_REPLY_SIZE = sizeof(DogeShareValidation::OracleReply);

// This class implement the logic handle connection and using the doge provider
class DogeService : public BaseOracleService
{
public:
    DogeService(const std::string& rHostname, uint16_t hostPort);
    ~DogeService() override;

protected:
    // Implement BaseOracleService abstract methods
    uint16_t processInterfaceQuery(
        const std::vector<uint8_t>& queryPayload,
        std::vector<uint8_t>& replyPayload) override;

private:
    std::mutex _providersMutex;
};

}