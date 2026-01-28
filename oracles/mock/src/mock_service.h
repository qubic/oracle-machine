#pragma once

#include "base_oracle_service.h"

#include "om_common/config.h"
#include "om_common/qpi_adapter.h"

#define ORACLE_INTERFACE_INDEX MOCK_ORACLE_INTERFACE_INDEX
#include "oracle_interfaces/Mock.h"
#undef ORACLE_INTERFACE_INDEX

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace oracle
{
// Mock Interface Constants
constexpr size_t MOCK_ORACLE_QUERY_SIZE = sizeof(Mock::OracleQuery);
constexpr size_t MOCK_ORACLE_REPLY_SIZE = sizeof(Mock::OracleReply);

static std::string getTimeStampString(const QPI::DateAndTime& rQpiDateTime); 

// This class implement the logic handle connection and using the mock provider
class MockService : public BaseOracleService
{
public:
    MockService(const std::string& rHostname, uint16_t hostPort);
    ~MockService() override;

protected:
    // Implement BaseOracleService abstract methods
    uint16_t processInterfaceQuery(
        const std::vector<uint8_t>& queryPayload,
        std::vector<uint8_t>& replyPayload) override;

private:
    std::mutex _providersMutex;

    // Helper: convert bytes to null-terminated string
    static std::string bytesToString(const char* data, size_t maxLen);
};

}