#pragma once

#include <atomic>
#include <memory>
#include <vector>

namespace oracle
{

class OracleMachineNodeImpl;
class OracleMachineNode
{
public:
    OracleMachineNode();
    ~OracleMachineNode();

    // Initialize the oracle machine
    bool initialize();

    // Start the oracle machine
    bool start();

    // Stop the oracle machine
    void stop();

    // Check if running
    bool isRunning() const;

    // Wait for shutdown signal
    void waitForShutdown();

private:
    OracleMachineNodeImpl* _impl;
};

} // namespace oracle