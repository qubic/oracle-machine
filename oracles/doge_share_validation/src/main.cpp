#include "doge_service.h"
#include "om_common/config.h"
#include <om_common/logger.h>

#include <iostream>
#include <csignal>
#include <atomic>

using namespace oracle;

void printHelp()
{
    std::cout << "./doge_oracle_service OPTIONAL: --config [CONFIG FILE] --log [LOG FILE]" << std::endl;
}

int main(int argc, char* argv[])
{
    std::string logFile = "doge_service.txt";
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

    // Create service
    auto dogeInterface = Config::instance().findInterface(DOGE_ORACLE_INTERFACE_INDEX);
    if (dogeInterface == nullptr)
    {
        OM_LOG_ERROR() << "Can not get doge interface from config. Exit.";
        return 1;
    }
    auto dogeService = std::make_shared<DogeService>(dogeInterface->serviceHost, dogeInterface->servicePort);

    // Start the service
    dogeService->start();

    // Wait for shutdown signal
    dogeService->waitForShutdown();

    return 0;
}