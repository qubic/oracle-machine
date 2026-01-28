#include "mock_service.h"
#include "om_common/config.h"
#include <om_common/logger.h>

#include <iostream>
#include <csignal>
#include <atomic>

using namespace oracle;

void printHelp()
{
    std::cout << "./mock_oracle_service OPTIONAL: --config [CONFIG FILE] --log [LOG FILE]" << std::endl;
}

int main(int argc, char* argv[])
{
    std::string logFile = "mock_service.txt";
    for (int i = 1; i < argc; ++i) 
    {
        std::string arg = argv[i];
        if (arg == "--log")
        {
            logFile = argv[++i];
        }
        else if (arg == "--help")
        {
            printHelp();
            return 0;
        }
        else
        {
            printHelp();
            return 1;
        }
    }

    Config::instance().loadFromEnv();

    Logger::init(true, logFile.c_str());
    Logger::setLevel(Logger::Level::DEBUG);

    // Create price service
    // Price have interface index = 0. 
    // TODO: move this number
    auto mockInterface = Config::instance().findInterface(MOCK_ORACLE_INTERFACE_INDEX);
    if (mockInterface == nullptr)
    {
        OM_LOG_ERROR() << "Can not get mock interface from config. Exit.";
        return 1;
    }
    auto mockService = std::make_shared<MockService>(mockInterface->serviceHost, mockInterface->servicePort);

    // Start the service
    mockService->start();

    // Wait for shutdown signal
    mockService->waitForShutdown();

    return 0;
}