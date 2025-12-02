#include "price_service.h"
#include <iostream>
#include <csignal>
#include <atomic>

using namespace oracle;

int main(int argc, char* argv[])
{
    std::cout << "============================================================" << std::endl;
    std::cout << "Price Oracle Service" << std::endl;
    std::cout << "============================================================\n" << std::endl;
    
    // Parse command line arguments
    std::string bindAddress = "0.0.0.0";
    uint16_t port = 9001;
    
    if (argc >= 2)
    {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    
    std::cout << "[Main] Configuration:" << std::endl;
    std::cout << "  Bind address: " << bindAddress << std::endl;
    std::cout << "  Port: " << port << std::endl;
    std::cout << std::endl;

    // Create price service
    auto priceService = std::make_shared<PriceService>(bindAddress, port);

    // Start the service
    priceService->start();

    // Wait for shutdown signal
    priceService->waitForShutdown();

    return 0;
}