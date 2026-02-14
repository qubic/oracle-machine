#include "football_service.h"
#include "om_common/config.h"
#include "om_common/logger.h"

#include <csignal>
#include <iostream>
#include <memory>

std::unique_ptr<oracle::FootballService> g_service;

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        std::cout << "\nReceived shutdown signal. Stopping service..." << std::endl;
        if (g_service)
        {
            g_service->stop();
        }
    }
}

int main(int argc, char* argv[])
{
    // Parse command line arguments
    std::string logFile;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--log" && i + 1 < argc)
        {
            logFile = argv[++i];
        }
    }

    // Initialize logger
    if (!logFile.empty())
    {
        initLogger("football_service", logFile);
    }
    else
    {
        initLogger("football_service");
    }

    // Load configuration from environment
    std::string host = getEnvOrDefault("FOOTBALL_SERVICE_HOST", "0.0.0.0");
    uint16_t port = std::stoi(getEnvOrDefault("FOOTBALL_SERVICE_PORT", "31844"));

    std::cout << "========================================" << std::endl;
    std::cout << "  Football Oracle Service" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Host: " << host << std::endl;
    std::cout << "  Port: " << port << std::endl;
    std::cout << "========================================" << std::endl;

    // Create service
    g_service = std::make_unique<oracle::FootballService>(host, port);

    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Start service
    OM_LOG_INFO() << "Starting Football Oracle Service on " << host << ":" << port;
    if (g_service->start() != 0)
    {
        OM_LOG_ERROR() << "Failed to start service";
        return 1;
    }

    std::cout << "\nFootball Oracle Service running." << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    // Wait for shutdown signal
    g_service->waitForShutdown();

    std::cout << "\nShutting down..." << std::endl;
    g_service->stop();
    g_service.reset();

    OM_LOG_INFO() << "Football Oracle Service stopped";
    return 0;
}
