# Overview: Oracle Machine for Qubic
This is a middleware system that bridges Qubic nodes with external oracle services (price feeds, etc.). It enables ability to access real-world data.

### High-Level Architecture

```
┌─────────────────────────────────┐
│      Qubic Core Nodes           │
│   (Multiple nodes, whitelisted) │
└────────────────┬────────────────┘
                 │ TCP (Port 31841)
                 │ OracleMachineQuery/Reply
                 ▼
┌─────────────────────────────────┐
│     Oracle Machine Node         │
│  ┌───────────────────────────┐  │
│  │     NodeConnection        │  │  ← Qubic node connections
│  │     (TCP Server)          │  │
│  └─────────────┬─────────────┘  │
│                ▼                │
│  ┌───────────────────────────┐  │
│  │     RequestHandler        │  │  ← Routes queries, manages cache
│  │     + OracleCache         │  │
│  └─────────────┬─────────────┘  │
│                ▼                │
│  ┌───────────────────────────┐  │
│  │   InterfaceClient[0..N]   │  │  ← One client per oracle service
│  │   (Worker Threads)        │  │
│  └─────────────┬─────────────┘  │
└────────────────┼────────────────┘
        ┌────────┴────────┐
        ▼                 ▼
   ┌─────────┐      ┌──────────┐
   │ Price   │      │ Mock     │
   │ Service │      │ Service  │
   │ :31842  │      │ :31843   │
   └────┬────┘      └──────────┘
        │
   ┌────┴────┬─────────┐
   ▼         ▼         ▼
 Mock    Binance    (Other)
Provider Provider   Providers
```

# Architecture

## Data Flow

```
1. Qubic Node sends OracleMachineQuery
        ↓
2. NodeConnection receives and validates
        ↓
3. RequestHandler checks cache
        ↓ (cache miss)
4. InterfaceClient forwards to oracle service
        ↓
5. Oracle service fetches data (e.g., from Binance API)
        ↓
6. Response cached and returned to Qubic node
```

## Modules

| Module    | Responsibility |
|-----------|----------------|
| **OracleMachineNode** | Main orchestrator, lifecycle management, signal handling |
| **NodeConnection** | TCP server, IP whitelist validation, session management |
| **RequestHandler** | Query parsing, cache lookup, routing to InterfaceClients |
| **InterfaceClient** | Persistent connection to oracle service, async request queue |
| **OracleCache** | TTL-based caching with automatic cleanup |
| **BaseOracleService** | Abstract base for oracle service implementations |

### OracleMachineNode
**File**: `node/src/oracle_machine_node.cpp`

The main entry point that:
- Initializes all modules
- Sets up signal handlers (SIGINT, SIGTERM)
- Starts NodeConnection and InterfaceClients
- Coordinates graceful shutdown

```cpp
// Lifecycle
OracleMachineNode node;
node.initialize();  // Setup modules
node.start();       // Start TCP server + workers
node.waitForShutdown();  // Block until Ctrl+C
node.stop();        // Graceful cleanup
```

### NodeConnection
**File**: `node/src/node_connection.cpp`

TCP server that:
- Listens on configurable port (default 31841, this should be changed to 21841 if mainet)
- Validates incoming IPs against whitelist
- Creates sessions for accepted connections
- Routes messages to RequestHandler


### RequestHandler
**File**: `node/src/request_handler.cpp`

Query processor that:
- Parses OracleMachineQuery messages
- Checks OracleCache before forwarding
- Routes to appropriate InterfaceClient by `oracleInterfaceIndex`
- Builds OracleMachineReply responses

### InterfaceClient
**File**: `node/src/interface_client.cpp`

Service client that:
- Maintains persistent TCP connection to oracle service
- Uses worker thread with request queue
- Supports sync (`query()`) and async (`queryAsync()`) operations
- Auto-reconnects on connection loss

The `InterfaceClient` uses a producer-consumer pattern for async request processing.

### OracleCache
**File**: `node/src/oracle_cache.cpp`

Caching layer: Improvement later
- Key: `{oracleQueryId, oracleInterfaceIndex}`


## Message Protocol

### RequestResponseHeader (8 bytes)

**File**: `submodules/qubic_core/src/network_messages/header.h`

```
Offset | Size | Field    | Type     | Description
-------|------|----------|----------|----------------------------------
0      | 3    | _size    | uint24   | Total message size (little-endian)
3      | 1    | _type    | uint8    | Message type (190=Query, 191=Reply)
4      | 4    | _dejavu  | uint32   | Deduplication identifier
```

### OracleMachineQuery (16 bytes)

```
Offset | Size | Field                 | Type   | Description
-------|------|-----------------------|--------|---------------------------
0      | 8    | oracleQueryId         | uint64 | Query identifier for reply correlation
8      | 4    | oracleInterfaceIndex  | uint32 | Interface type (0=Price, 1=Mock)
12     | 4    | timeoutInMilliseconds | uint32 | Query timeout
```

**Message Type**: `190`

**File**: `submodules/qubic_core/src/oracle_core/core_om_network_messages.h`

### OracleMachineReply (16 bytes)

```
Offset | Size | Field                   | Type   | Description
-------|------|-------------------------|--------|---------------------------
0      | 8    | oracleQueryId           | uint64 | Matches query ID
8      | 2    | oracleMachineErrorFlags | uint16 | Error/status flags
10     | 2    | _padding0               | uint16 | Reserved
12     | 4    | _padding1               | uint32 | Reserved
```

**Message Type**: `191`

### Price Interface

**Interface Index**: `0`

**Query Payload** (`Price::OracleQuery`):
```
Offset | Size | Field     | Type        | Description
-------|------|-----------|-------------|------------------
0      | 32   | oracle    | m256i (id)  | Provider name
32     | 12   | timestamp | DateAndTime | Query timestamp
44     | 32   | currency1 | m256i (id)  | Base currency
76     | 32   | currency2 | m256i (id)  | Quote currency
```

**Reply Payload** (`Price::OracleReply`):
```
Offset | Size | Field       | Type   | Description
-------|------|-------------|--------|------------------
0      | 8    | numerator   | sint64 | Price numerator
8      | 8    | denominator | sint64 | Price denominator
```

**File**: `submodules/qubic_core/src/oracle_interfaces/Price.h`


# Build Instructions

## Prerequisites

**Ubuntu/Debian**:
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libcurl4-openssl-dev \
    libyaml-cpp-dev \
    ca-certificates
```

**Requirements**:
- CMake >= 3.21
- C++20 compiler
- libcurl (for price service)

### Clone and Initialize

```bash
git clone <repository-url>
cd oracle-machine

# Initialize submodules
git submodule update --init --recursive
```

### Native Build

```bash
# Create build directory
mkdir build && cd build

# Configure (Release)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Or Debug with symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
make -j4

# Binaries are in build/bin/
ls bin/
# oracle_machine_node
# price_oracle_service
```

### Docker Build

```bash
cd docker

# Full build (creates dev, runtime, and final images)
./build_docker.sh --build-all 1 --version latest

# Quick rebuild (reuses dev image)
./build_docker.sh --build-all 0 --version latest
# NOTE: upgrading from a pre-python image requires --build-all 1 once
# (the runtime base gained python3 for the read_evm_log / read_qubic_log services).
```

**Docker images created**:
- `oracle-machine:dev` - Build environment with compilers
- `oracle-machine:rt` - Minimal runtime base
- `oracle-machine:latest` - Final image with binaries

# Configuration

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `OM_SERVER_PORT` | 31841 | Oracle Machine node listening port |
| `QUBIC_NODES` | 10.29.1.22,192.168.1.2 | Comma-separated whitelist of Qubic node IPs |
| `PRICE_SERVICE_HOST` | 0.0.0.0 | Price service host |
| `PRICE_SERVICE_PORT` | 9001 | Price service port |
| `MOCK_SERVICE_HOST` | 0.0.0.0 | Mock service host |
| `MOCK_SERVICE_PORT` | 9002 | Mock service port |

## .env File Setup

```bash
# Copy template
cp example_env .env

# Edit configuration
vim .env
```

**Example .env**:
```bash
OM_SERVER_PORT=31841
QUBIC_NODES=10.29.1.22,127.0.0.1

PRICE_SERVICE_HOST=127.0.0.1
PRICE_SERVICE_PORT=31842

MOCK_SERVICE_HOST=127.0.0.1
MOCK_SERVICE_PORT=31843
```

## Docker Compose Environment

The main different is if you deploy the Docker on the same machine, you need config the SERVICE_HOST  as Docker service name

For Docker deployment, create `docker/docker_local.env`:
```bash
OM_SERVER_PORT=31841
QUBIC_NODES=10.29.1.22,127.0.0.1

# Use Docker service names
PRICE_SERVICE_HOST=price-oracle-service
PRICE_SERVICE_PORT=31842

READ_EVM_LOG_SERVICE_HOST=read-evm-log-service
READ_EVM_LOG_SERVICE_PORT=31845
EVM_RPC_URLS_1=https://ethereum-rpc.publicnode.com,https://eth.llamarpc.com

# NOTE: run your OWN bob (https://github.com/qubic/core-bob) — a shared third-party bob could
# forge log content. The URL must include the /qubic RPC path.
READ_QUBIC_LOG_SERVICE_HOST=read-qubic-log-service
READ_QUBIC_LOG_SERVICE_PORT=31847
BOB_URLS=http://host.docker.internal:40420/qubic
# public fallback for a quick test only (NOT for production oracle duty):
# BOB_URLS=https://bob.qubic.global/qubic,https://bob.qubic.li/qubic

HOST_UID=1000
HOST_GID=1000
```

# Running the System

## Native Execution

**Terminal 1 - Price Oracle Service**:
```bash
source .env
./build/bin/price_oracle_service --log ./logs/price_service.log
```

**Terminal 2 - Oracle Machine Node**:
```bash
source .env
./build/bin/oracle_machine_node --log ./logs/om_node.log
```

## Docker Compose

```bash
cd docker

# Start all services
docker-compose up -d

# View logs
docker-compose logs -f oracle-machine-node
docker-compose logs -f price-oracle-service

# Stop services
docker-compose down
```

# Developing New Oracle Services

## Overview

To add a new oracle service (e.g., Weather), you need:
1. Define interface structures in `qubic_core`
2. Create service implementation inheriting from `BaseOracleService`
3. Implement data providers
4. Add configuration entries
5. Create CMakeLists.txt

## Directory Structure

```
oracles/
├── core/
│   ├── base_oracle_service.h      # Base class
│   └── base_oracle_service.cpp
└── your_service/                   # NEW
    ├── CMakeLists.txt
    └── src/
        ├── main.cpp
        ├── your_service.h
        ├── your_service.cpp
        └── your_provider.h         # Optional: data providers
```

# Developing New Oracle Services

## Steps Overview

1. **Define Interface in Qubic Core**
   - Create `submodules/qubic_core/src/oracle_interfaces/YourInterface.h`
   - Define unique `oracleInterfaceIndex` (next available number)
   - Define `OracleQuery` struct (input fields)
   - Define `OracleReply` struct (output fields)

2. **Create Service Directory**
   - Create `oracles/your_service/` folder
   - Add `CMakeLists.txt` and `src/` subfolder
   - Use `oracles/price/` as template

3. **Implement Service Class**
   - Inherit from `BaseOracleService`
   - Implement `processInterfaceQuery()` method
   - Return `0` for success, non-zero for errors

4. **Implement Data Provider(s)**
   - Create provider class to fetch external data
   - Handle caching and rate limiting as needed

5. **Create Main Entry Point**
   - Load config, initialize logger
   - Create service and call `start()`

6. **Register in Build System**
   - Add `add_subdirectory(your_service)` to `oracles/CMakeLists.txt`

7. **Add Configuration**
   - Add interface to `libs/om_common/include/om_common/default_config.h`
   - Add `YOUR_SERVICE_HOST` and `YOUR_SERVICE_PORT` to `.env`

## Reference Files

| Purpose | File |
|---------|------|
| Base class | `oracles/core/base_oracle_service.h` |
| Example service | `oracles/price/src/price_service.cpp` |
| Example CMake | `oracles/price/CMakeLists.txt` |
| Interface example | `submodules/qubic_core/src/oracle_interfaces/Price.h` |
| Config defaults | `libs/om_common/include/om_common/default_config.h` |