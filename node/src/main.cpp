#include "logger/logger.h"
#include "oracle_machine_node.h"

#include <iostream>

void printHelp()
{
    std::cout << "./oracle_machine_node OPTIONAL: --config [CONFIG FILE] --log [LOG FILE]" << std::endl;
}

int main(int argc, char* argv[])
{
    std::string configFile;
    std::string logFile = "om_log.txt";
    for (int i = 1; i < argc; ++i) 
    {
        std::string arg = argv[i];
        if (arg == "--config")
        {
            configFile = argv[++i];
        }
        if (arg == "--log")
        {
            configFile = argv[++i];
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

    Logger::init(true, logFile.c_str());
    Logger::setLevel(Logger::Level::DEBUG);

    OM_LOG_INFO() <<  "=== Oracle Machine Node DRAFT ===";
    OM_LOG_INFO() <<  std::endl;

    oracle::OracleMachineNode machine;

    if (!machine.initialize())
    {
        std::cerr << "Failed to initialize Oracle Machine Node";
        return 1;
    }

    if (!machine.start())
    {
        std::cerr << "Failed to start Oracle Machine Node";
        return 1;
    }

    OM_LOG_INFO() <<  std::endl;
    OM_LOG_INFO() <<  "Press Ctrl+C to stop";

    // Wait for shutdown signal
    machine.waitForShutdown();

    return 0;
}