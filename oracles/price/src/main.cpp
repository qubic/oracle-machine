#include "price_service.h"
#include "config.h"
#include <logger/logger.h>

#include <iostream>
#include <csignal>
#include <atomic>

using namespace oracle;

void printHelp()
{
    std::cout << "./price_oracle_service OPTIONAL: --config [CONFIG FILE] --log [LOG FILE]" << std::endl;
}

int main(int argc, char* argv[])
{
    std::string logFile = "price_service.txt";
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
    auto priceInterface = Config::instance().findInterface(0);
    if (priceInterface == nullptr)
    {
        OM_LOG_ERROR() << "Can not get price interface from config. Exit.";
        return 1;
    }
    auto priceService = std::make_shared<PriceService>(priceInterface->serviceHost, priceInterface->servicePort);

    // Start the service
    priceService->start();

    // Wait for shutdown signal
    priceService->waitForShutdown();

    return 0;
}