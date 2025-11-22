#include "oracle_machine.h"
#include "config.h"
#include "node_connection.h"
#include "oracle_client.h"
#include "request_handler.h"

#include <csignal>
#include <iostream>
#include <thread>

namespace oracle
{

static std::atomic<bool> gSignalStop(false);

static void signal_handler(int signum)
{
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    if (!gSignalStop.load())
    {
        gSignalStop.store(true);
    }
}

class OracleMachineImpl
{
public:
    OracleMachineImpl()
    {
        _running.store(false);
        _shutdownRequested.store(false);
    }
    ~OracleMachineImpl() { stop(); }

    // Initialize the oracle machine
    bool initialize()
    {
        std::cout << "Initializing Oracle Machine..." << std::endl;

        // Setup signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        _handler = std::make_unique<RequestHandler>(_oracleClients);

        // Connection to nodes
        _nodeConnectionServer = std::make_unique<NodeConnection>(OC_SERVER_BIND, OM_SERVER_PORT);
        _nodeConnectionServer->setHandler(
            [this](const RequestResponseHeader& header, const uint8_t* payload, int size) {
                return _handler->handle(header, payload, size);
            });

        setupOracles();

        return true;
    }

    // Start the oracle machine
    bool start()
    {
        // Already running
        if (_running.load())
            return true;

        std::cout << "Starting Oracle Machine..." << std::endl;

        if (!_nodeConnectionServer->start())
        {
            std::cerr << "Failed to start node connection" << std::endl;
            return false;
        }

        _running.store(true);
        _shutdownRequested.store(false);

        std::cout << "Oracle Machine started!" << std::endl;
        std::cout << "Nodes can connect to port " << OM_SERVER_PORT << std::endl;

        return true;
    }

    // Stop the oracle machine
    void stop()
    {
        if (!_running.load())
            return;

        std::cout << "Stopping Oracle Machine..." << std::endl;

        _shutdownRequested.store(true);

        _nodeConnectionServer->stop();

        for (auto& c : _oracleClients)
        {
            c.second->disconnect();
        }

        _running.store(false);

        std::cout << "Oracle Machine stopped" << std::endl;
    }

    // Check if running
    bool isRunning() const { return _running.load(); }

    // Wait for shutdown signal
    void waitForShutdown()
    {
        while (_running.load() && !_shutdownRequested.load() && !gSignalStop.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    int setupOracles()
    {
        for (size_t i = 0; i < ORACLE_COUNT; i++)
        {
            const auto& endpoint = ORACLE_LIST[i];

            auto client = std::make_unique<OracleClient>(
                endpoint.id, endpoint.name, endpoint.host, endpoint.port);

            std::cout << "  - " << endpoint.name << " (" << endpoint.id << ") at " << endpoint.host
                      << ":" << endpoint.port << std::endl;

            _oracleClients[endpoint.id] = std::move(client);
        }
        return 0;
    }

private:
    std::atomic<bool> _running;
    std::atomic<bool> _shutdownRequested;

    std::unique_ptr<NodeConnection> _nodeConnectionServer;
    std::map<std::string, std::unique_ptr<OracleClient>> _oracleClients;
    std::unique_ptr<RequestHandler> _handler;
};

OracleMachine::OracleMachine()
{
    _impl = new OracleMachineImpl();
}
OracleMachine::~OracleMachine()
{
    delete _impl;
    _impl = nullptr;
}

// Initialize the oracle machine
bool OracleMachine::initialize()
{
    return _impl->initialize();
}

// Start the oracle machine
bool OracleMachine::start()
{
    return _impl->start();
}

// Stop the oracle machine
void OracleMachine::stop()
{
    _impl->stop();
}

// Check if running
bool OracleMachine::isRunning() const
{
    return _impl->isRunning();
}

// Wait for shutdown signal
void OracleMachine::waitForShutdown()
{
    _impl->waitForShutdown();
}

} // namespace oracle