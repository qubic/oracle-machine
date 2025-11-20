#include "oracle_machine.h"
#include "logger.h"

#include <iostream>

int main(int argc, char* argv[]) 
{
    Logger::init();
    Logger::init("om_log.txt");             
    Logger::setLevel(Logger::Level::DEBUG);

    std::cout << "=== Oracle Machine DRAFT ===" << std::endl;
    std::cout << std::endl;
    
    oracle::OracleMachine machine;
    
    if (!machine.initialize()) 
    {
        std::cerr << "Failed to initialize Oracle Machine" << std::endl;
        return 1;
    }
    
    if (!machine.start()) 
    {
        std::cerr << "Failed to start Oracle Machine" << std::endl;
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << std::endl;
    
    // Wait for shutdown signal
    machine.waitForShutdown();
    
    // Stop the machine
    machine.stop();

    return 0;
}