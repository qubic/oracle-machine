#include "logger.h"
#include "oracle_machine_node.h"

#include <iostream>

int main(int argc, char* argv[])
{
    Logger::init(true, "om_log.txt");
    Logger::setLevel(Logger::Level::DEBUG);

    std::cout << "=== Oracle Machine Node DRAFT ===" << std::endl;
    std::cout << std::endl;

    oracle::OracleMachineNode machine;

    if (!machine.initialize())
    {
        std::cerr << "Failed to initialize Oracle Machine Node" << std::endl;
        return 1;
    }

    if (!machine.start())
    {
        std::cerr << "Failed to start Oracle Machine Node" << std::endl;
        return 1;
    }

    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << std::endl;

    // Wait for shutdown signal
    machine.waitForShutdown();

    return 0;
}