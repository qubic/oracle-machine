#include "oracle_machine.h"
#include "config.h"
#include "node_connection.h"
#include "oracle_client.h"
#include "request_handler.h"
#include "logger.h"

#include <csignal>
#include <iostream>
#include <thread>

namespace oracle
{

static std::atomic<bool> gSignalStop(false);

static void signal_handler(int signum)
{
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
        LOG_INFO() << "Initializing Oracle Machine...";

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

        LOG_INFO() << "Starting Oracle Machine...";

        // Start Node Connection Server
        if (!_nodeConnectionServer->start())
        {
            LOG_ERROR() << "Failed to start node connection";
            return false;
        }
        
        // Start all OracleClients
        for (auto& pair : _oracleClients)
        {
            pair.second->start();
        }

        _running.store(true);
        _shutdownRequested.store(false);

        LOG_INFO() << "Oracle Machine started listening at port " << OM_SERVER_PORT;

        return true;
    }

    // Stop the oracle machine
    void stop()
    {
        if (!_running.load())
            return;

        LOG_INFO() << "Stopping Oracle Machine...";

        _shutdownRequested.store(true);

        // Stop the Node Server (closes listening socket and client sockets)
        // This unblocks any NodeConnection threads waiting on recv()
        if (_nodeConnectionServer)
        {
            _nodeConnectionServer->stop();
        }

        // Unblock any OracleClients waiting on network I/O
        // This breaks the deadlock if a fetch() is stuck in recv()
        for (auto& pair : _oracleClients)
        {
            pair.second->stop();
        }

        _running.store(false);
        LOG_INFO() << "Oracle Machine stopped.";
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
        stop();
    }

    int setupOracles()
    {
        for (size_t i = 0; i < ORACLE_COUNT; i++)
        {
            const auto& endpoint = ORACLE_LIST[i];

            auto client = std::make_unique<OracleClient>(
                endpoint.id, endpoint.name, endpoint.host, endpoint.port);

            LOG_INFO() << "  - " << endpoint.name << " (" << endpoint.id << ") at " << endpoint.host
                      << ":" << endpoint.port;

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