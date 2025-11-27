#include "oracle_machine_node.h"
#include "config.h"
#include "node_connection.h"
#include "request_handler.h"
#include "logger.h"
#include "interface_client.h"

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

class OracleMachineNodeImpl
{
public:
    OracleMachineNodeImpl()
    {
        _running.store(false);
        _shutdownRequested.store(false);
    }
    ~OracleMachineNodeImpl() { stop(); }

    // Initialize the oracle machine
    bool initialize()
    {
        OM_LOG_INFO() << "Initializing Oracle Machine...";

        // Setup signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        _handler = std::make_unique<RequestHandler>(_interfaceClients);

        // Connection to nodes
        _nodeConnectionServer = std::make_unique<NodeConnection>(OC_SERVER_BIND, OM_SERVER_PORT);
        _nodeConnectionServer->setHandler(
            [this](const RequestResponseHeader& header, const uint8_t* payload, int size) {
                return _handler->handle(header, payload, size);
            });

        setupInterfaceClients();

        return true;
    }

    // Start the oracle machine
    bool start()
    {
        // Already running
        if (_running.load())
            return true;

        OM_LOG_INFO() << "Starting Oracle Machine...";

        // Start Node Connection Server
        if (!_nodeConnectionServer->start())
        {
            OM_LOG_ERROR() << "Failed to start node connection";
            return false;
        }
        
        // Start all InterfaceClients
        for (auto& pair : _interfaceClients)
        {
            pair.second->start();
        }

        _running.store(true);
        _shutdownRequested.store(false);

        OM_LOG_INFO() << "Oracle Machine started listening at port " << OM_SERVER_PORT;

        return true;
    }

    // Stop the oracle machine
    void stop()
    {
        if (!_running.load())
            return;

        OM_LOG_INFO() << "Stopping Oracle Machine...";

        _shutdownRequested.store(true);

        // Stop the Node Server (closes listening socket and client sockets)
        // This unblocks any NodeConnection threads waiting on recv()
        if (_nodeConnectionServer)
        {
            _nodeConnectionServer->stop();
        }

        // Unblock any InterfaceClients waiting on network I/O
        // This breaks the deadlock if a fetch() is stuck in recv()
        for (auto& pair : _interfaceClients)
        {
            pair.second->stop();
        }

        _running.store(false);
        OM_LOG_INFO() << "Oracle Machine stopped.";
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

    int setupInterfaceClients()
    {
        for (size_t i = 0; i < ORACLE_INTERFACES_COUNT; i++)
        {
            const auto& endpoint = INTERFACE_ENDPOINTS[i];

            auto client = std::make_unique<InterfaceClient>(
                endpoint.interfaceIndex, endpoint.serviceHost, endpoint.servicePort);

            OM_LOG_INFO() << "  - " << endpoint.interfaceIndex << " (" << endpoint.interfaceName << ") at " << endpoint.serviceHost
                      << ":" << endpoint.servicePort;

            _interfaceClients[endpoint.interfaceIndex] = std::move(client);
        }
        return 0;
    }

private:
    std::atomic<bool> _running;
    std::atomic<bool> _shutdownRequested;

    std::unique_ptr<NodeConnection> _nodeConnectionServer;
    std::map<uint32_t, std::unique_ptr<InterfaceClient>> _interfaceClients;
    std::unique_ptr<RequestHandler> _handler;
};

OracleMachineNode::OracleMachineNode()
{
    _impl = new OracleMachineNodeImpl();
}
OracleMachineNode::~OracleMachineNode()
{
    delete _impl;
    _impl = nullptr;
}

// Initialize the oracle machine
bool OracleMachineNode::initialize()
{
    return _impl->initialize();
}

// Start the oracle machine
bool OracleMachineNode::start()
{
    return _impl->start();
}

// Stop the oracle machine
void OracleMachineNode::stop()
{
    _impl->stop();
}

// Check if running
bool OracleMachineNode::isRunning() const
{
    return _impl->isRunning();
}

// Wait for shutdown signal
void OracleMachineNode::waitForShutdown()
{
    _impl->waitForShutdown();
}

} // namespace oracle