/**
 * Qubic Test Node - Oracle Machine Testing Tool
 *
 * Simulates a Qubic node to send oracle queries to the Oracle Machine (OM) for testing.
 * Uses compatible message structures from qubic_core for protocol compatibility.
 *
 * Usage:
 *   qubic_test_node --ip <om_host> --port <om_port> --oracle <oracle_id> --pair <c1/c2> [--pair <c1/c2>...]
 *
 * Example:
 *   qubic_test_node --ip 127.0.0.1 --port 31841 --oracle binance --pair BTC/USDT
 *   qubic_test_node --ip 127.0.0.1 --oracle binance --pair BTC/USDT --pair ETH/USDT --interval 5000
 */

#include <arpa/inet.h>
#include <cstring>
#include <ctime>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// Include only the essential network message definitions from qubic_core
#ifndef NO_UEFI
#define NO_UEFI
#endif

#ifndef NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES
#define NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES
#endif

#include "network_messages/common_def.h"
#include "network_messages/header.h"
#include "oracle_core/core_om_network_messages.h"

// Oracle interface indices (from oracle_core/oracle_interfaces_def.h)
constexpr unsigned int PRICE_ORACLE_INTERFACE_INDEX = 0;
constexpr unsigned int MOCK_ORACLE_INTERFACE_INDEX = 1;

// Error flags (from network_messages/common_def.h and om_common/qpi_adapter.h)
enum OracleErrorFlags : uint16_t
{
    RETURN_NO_ERROR = 0,
    RETURN_ERROR_INVALID_ORACLE = ORACLE_FLAG_INVALID_ORACLE,
    RETURN_ERROR_ORACLE_UNAVAIL = ORACLE_FLAG_ORACLE_UNAVAIL,
    RETURN_ERROR_INVALID_TIME = ORACLE_FLAG_INVALID_TIME,
    RETURN_ERROR_INVALID_PLACE = ORACLE_FLAG_INVALID_PLACE,
    RETURN_ERROR_INVALID_ARG = ORACLE_FLAG_INVALID_ARG,
};

// Price oracle interface structures (compatible with oracle_interfaces/Price.h)
// Layout matches the qubic_core Price interface exactly
#pragma pack(push, 1)

// id type - 256-bit (32 bytes), compatible with m256i/QPI::id
struct OracleId
{
    uint8_t data[32];

    void clear() { memset(data, 0, sizeof(data)); }

    void setFromString(const std::string& str)
    {
        clear();
        size_t len = std::min(str.length(), sizeof(data));
        memcpy(data, str.c_str(), len);
    }

    std::string toString() const
    {
        size_t len = 0;
        while (len < sizeof(data) && data[len] != '\0')
        {
            len++;
        }
        return std::string(reinterpret_cast<const char*>(data), len);
    }
};

// DateAndTime - 64-bit timestamp (compatible with QPI::DateAndTime from contracts/qpi.h)
struct OracleDateTime
{
    uint64_t value;

    // DateAndTime bit layout from qpi.h (all times are UTC):
    // Bits 46-63: year (0-65535)
    // Bits 42-45: month (1-12)
    // Bits 37-41: day (1-31)
    // Bits 32-36: hour (0-23)
    // Bits 26-31: minute (0-59)
    // Bits 20-25: second (0-59)
    // Bits 10-19: millisecond (0-999)
    // Bits 0-9: microsecond during millisecond (0-999)

    // Set to zero (means "current price" to the oracle)
    void setZero()
    {
        value = 0;
    }

    // Set from components (UTC)
    void set(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
    {
        value = 0;
        value |= ((uint64_t)year << 46);
        value |= ((uint64_t)month << 42);
        value |= ((uint64_t)day << 37);
        value |= ((uint64_t)hour << 32);
        value |= ((uint64_t)minute << 26);
        value |= ((uint64_t)second << 20);
    }

    // Set to current UTC time
    void setNow()
    {
        time_t now = time(nullptr);
        struct tm* tm = gmtime(&now);  // gmtime returns UTC
        set(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    }

    uint16_t getYear() const { return (value >> 46) & 0xFFFF; }
    uint8_t getMonth() const { return (value >> 42) & 0xF; }
    uint8_t getDay() const { return (value >> 37) & 0x1F; }
    uint8_t getHour() const { return (value >> 32) & 0x1F; }
    uint8_t getMinute() const { return (value >> 26) & 0x3F; }
    uint8_t getSecond() const { return (value >> 20) & 0x3F; }
};

// Price::OracleQuery - compatible with oracle_interfaces/Price.h
struct PriceOracleQuery
{
    OracleId oracle;       // 32 bytes - Oracle source (e.g., "binance")
    OracleDateTime timestamp; // 8 bytes - Query timestamp
    OracleId currency1;    // 32 bytes - First currency
    OracleId currency2;    // 32 bytes - Second currency
};

// Price::OracleReply - compatible with oracle_interfaces/Price.h
struct PriceOracleReply
{
    int64_t numerator;     // Exchange rate numerator
    int64_t denominator;   // Exchange rate denominator
};

#pragma pack(pop)

// Verify structure sizes match qubic_core
static_assert(sizeof(OracleId) == 32, "OracleId must be 32 bytes");
static_assert(sizeof(OracleDateTime) == 8, "OracleDateTime must be 8 bytes");
static_assert(sizeof(PriceOracleQuery) == 104, "PriceOracleQuery must be 104 bytes");
static_assert(sizeof(PriceOracleReply) == 16, "PriceOracleReply must be 16 bytes");
static_assert(sizeof(RequestResponseHeader) == 8, "RequestResponseHeader must be 8 bytes");
static_assert(sizeof(OracleMachineQuery) == 16, "OracleMachineQuery must be 16 bytes");
static_assert(sizeof(OracleMachineReply) == 16, "OracleMachineReply must be 16 bytes");

// Currency pair structure
struct CurrencyPair
{
    std::string currency1;
    std::string currency2;
};

// Global configuration
struct Config
{
    std::string host = "127.0.0.1";
    uint16_t port = 31841;
    std::string oracle = "binance";
    std::vector<CurrencyPair> pairs;
    uint32_t timeout = 10000;
    uint32_t interval = 0;     // 0 = single run, >0 = repeat interval in ms
    uint32_t pairDelay = 500;  // Delay between currency pair queries in ms (avoid rate limits)
    std::string timestamp;     // Empty = current, "0" = zero timestamp, "yyyymmddhhmmss" = specific UTC time
    bool verbose = false;
};

// Parse timestamp string in format "yyyymmddhhmmss" (e.g., "20240115143022")
// Returns true on success, fills the OracleDateTime
bool parseTimestamp(const std::string& ts, OracleDateTime& dt)
{
    if (ts == "0")
    {
        dt.setZero();
        return true;
    }

    if (ts.length() != 14)
    {
        return false;
    }

    // Validate all characters are digits
    for (char c : ts)
    {
        if (!isdigit(c)) return false;
    }

    uint16_t year = std::stoi(ts.substr(0, 4));
    uint8_t month = std::stoi(ts.substr(4, 2));
    uint8_t day = std::stoi(ts.substr(6, 2));
    uint8_t hour = std::stoi(ts.substr(8, 2));
    uint8_t minute = std::stoi(ts.substr(10, 2));
    uint8_t second = std::stoi(ts.substr(12, 2));

    // Basic validation
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;
    if (hour > 23) return false;
    if (minute > 59) return false;
    if (second > 59) return false;

    dt.set(year, month, day, hour, minute, second);
    return true;
}

// Parse single currency pair string (e.g., "BTC/USDT" -> {"BTC", "USDT"})
bool parseCurrencyPair(const std::string& pairStr, CurrencyPair& pair)
{
    size_t slashPos = pairStr.find('/');
    if (slashPos == std::string::npos || slashPos == 0 || slashPos == pairStr.length() - 1)
    {
        return false;
    }
    pair.currency1 = pairStr.substr(0, slashPos);
    pair.currency2 = pairStr.substr(slashPos + 1);
    return true;
}

// Parse comma-separated currency pairs (e.g., "BTC/USDT,ETH/USDT,SOL/USDT")
bool parseCurrencyPairs(const std::string& input, std::vector<CurrencyPair>& pairs)
{
    size_t start = 0;
    size_t end;

    while ((end = input.find(',', start)) != std::string::npos)
    {
        std::string pairStr = input.substr(start, end - start);
        CurrencyPair pair;
        if (!parseCurrencyPair(pairStr, pair))
        {
            return false;
        }
        pairs.push_back(pair);
        start = end + 1;
    }

    // Parse the last (or only) pair
    if (start < input.length())
    {
        std::string pairStr = input.substr(start);
        CurrencyPair pair;
        if (!parseCurrencyPair(pairStr, pair))
        {
            return false;
        }
        pairs.push_back(pair);
    }

    return !pairs.empty();
}

// Normalize oracle ID for combined oracles (e.g., "gate_binance" -> "binance_gate")
// Sorts exchange names alphabetically so order doesn't matter
std::string normalizeOracleId(const std::string& oracleId)
{
    size_t underscorePos = oracleId.find('_');
    if (underscorePos == std::string::npos)
    {
        // Single source oracle, no normalization needed
        return oracleId;
    }

    std::string first = oracleId.substr(0, underscorePos);
    std::string second = oracleId.substr(underscorePos + 1);

    // Sort alphabetically
    if (first > second)
    {
        std::swap(first, second);
    }

    return first + "_" + second;
}

void printUsage(const char* progName)
{
    std::cout << "Usage: " << progName << " [options]\n"
              << "\nOptions:\n"
              << "  -i, --ip <host>         OM host address (default: 127.0.0.1)\n"
              << "  -p, --port <port>       OM port (default: 31841)\n"
              << "  -o, --oracle <oracle>   Oracle ID (e.g., binance, mexc, gate, binance_mexc)\n"
              << "  -c, --pair <pairs>      Currency pair(s), comma-separated (e.g., BTC/USDT,ETH/USDT)\n"
              << "  -s, --time <timestamp>  UTC timestamp: 0 for current price, or yyyymmddhhmmss\n"
              << "  -n, --interval <ms>     Repeat interval in milliseconds (0 = single run, default: 0)\n"
              << "  -d, --pair-delay <ms>   Delay between currency pair queries in ms (default: 500)\n"
              << "  -t, --timeout <ms>      Query timeout in milliseconds (default: 10000)\n"
              << "  -v, --verbose           Verbose output\n"
              << "  -h, --help              Show this help\n"
              << "\nTimestamp Format:\n"
              << "  (empty)         Use current time for each query\n"
              << "  0               Request current price (timestamp=0)\n"
              << "  yyyymmddhhmmss  Specific UTC time (e.g., 20240115143022 = 2024-01-15 14:30:22 UTC)\n"
              << "\nSupported Oracles:\n"
              << "  Single source: binance, mexc, gate, mock\n"
              << "  Combined:      binance_gate, binance_mexc, gate_mexc (order doesn't matter)\n"
              << "\nExamples:\n"
              << "  " << progName << " --oracle binance --pair BTC/USDT\n"
              << "  " << progName << " --oracle binance -c BTC/USDT,ETH/USDT --time 0\n"
              << "  " << progName << " --oracle binance -c BTC/USDT --time 20240115143000\n"
              << "  " << progName << " --oracle binance --pair BTC/USDT,ETH/USDT --interval 5000\n";
}

std::string errorFlagsToString(uint16_t flags)
{
    if (flags == RETURN_NO_ERROR)
        return "SUCCESS";

    std::string result;
    if (flags & RETURN_ERROR_INVALID_ORACLE)
        result += "INVALID_ORACLE ";
    if (flags & RETURN_ERROR_ORACLE_UNAVAIL)
        result += "ORACLE_UNAVAIL ";
    if (flags & RETURN_ERROR_INVALID_TIME)
        result += "INVALID_TIME ";
    if (flags & RETURN_ERROR_INVALID_PLACE)
        result += "INVALID_PLACE ";
    if (flags & RETURN_ERROR_INVALID_ARG)
        result += "INVALID_ARG ";
    return result.empty() ? "UNKNOWN" : result;
}

bool priceReplyIsValid(const PriceOracleReply& reply)
{
    return reply.numerator > 0 && reply.denominator > 0;
}

// Send a single query and return success/failure
bool sendQuery(const Config& config, const CurrencyPair& pair, const OracleDateTime& timestamp,
               int sock, uint64_t queryId)
{
    // Calculate packet sizes
    constexpr size_t HEADER_SIZE = sizeof(RequestResponseHeader);
    constexpr size_t QUERY_SIZE = sizeof(OracleMachineQuery);
    constexpr size_t PRICE_QUERY_SIZE = sizeof(PriceOracleQuery);
    constexpr size_t TOTAL_SIZE = HEADER_SIZE + QUERY_SIZE + PRICE_QUERY_SIZE;

    // Build query packet
    std::vector<uint8_t> packet(TOTAL_SIZE, 0);

    // Header
    auto* header = reinterpret_cast<RequestResponseHeader*>(packet.data());
    header->checkAndSetSize(static_cast<unsigned int>(TOTAL_SIZE));
    header->setType(OracleMachineQuery::type());
    header->setDejavu(static_cast<unsigned int>(time(nullptr)));

    // OracleMachineQuery
    auto* query = reinterpret_cast<OracleMachineQuery*>(packet.data() + HEADER_SIZE);
    query->oracleQueryId = queryId;
    query->oracleInterfaceIndex = PRICE_ORACLE_INTERFACE_INDEX;
    query->timeoutInMilliseconds = config.timeout;

    // Price::OracleQuery
    auto* priceQuery = reinterpret_cast<PriceOracleQuery*>(packet.data() + HEADER_SIZE + QUERY_SIZE);
    priceQuery->oracle.setFromString(config.oracle);
    priceQuery->timestamp = timestamp;
    priceQuery->currency1.setFromString(pair.currency1);
    priceQuery->currency2.setFromString(pair.currency2);

    if (config.verbose)
    {
        std::cout << "  Query ID:   " << query->oracleQueryId << "\n";
        std::cout << "  Timestamp:  " << priceQuery->timestamp.getYear() << "-"
                  << std::setw(2) << std::setfill('0') << (int)priceQuery->timestamp.getMonth() << "-"
                  << std::setw(2) << std::setfill('0') << (int)priceQuery->timestamp.getDay() << " "
                  << std::setw(2) << std::setfill('0') << (int)priceQuery->timestamp.getHour() << ":"
                  << std::setw(2) << std::setfill('0') << (int)priceQuery->timestamp.getMinute() << ":"
                  << std::setw(2) << std::setfill('0') << (int)priceQuery->timestamp.getSecond() << " UTC\n";
    }

    // Send query
    ssize_t sent = send(sock, packet.data(), packet.size(), 0);
    if (sent != static_cast<ssize_t>(packet.size()))
    {
        std::cerr << "  Error: Failed to send query\n";
        return false;
    }

    // Receive response header
    RequestResponseHeader respHeader;
    ssize_t received = recv(sock, &respHeader, sizeof(respHeader), MSG_WAITALL);
    if (received != sizeof(respHeader))
    {
        std::cerr << "  Error: Failed to receive response header\n";
        return false;
    }

    if (respHeader.type() != OracleMachineReply::type())
    {
        std::cerr << "  Error: Unexpected response type: " << (int)respHeader.type() << "\n";
        return false;
    }

    // Receive response payload
    size_t payloadSize = respHeader.size() - sizeof(RequestResponseHeader);
    std::vector<uint8_t> respPayload(payloadSize);
    received = recv(sock, respPayload.data(), payloadSize, MSG_WAITALL);
    if (received != static_cast<ssize_t>(payloadSize))
    {
        std::cerr << "  Error: Failed to receive response payload\n";
        return false;
    }

    // Parse response
    auto* reply = reinterpret_cast<OracleMachineReply*>(respPayload.data());
    auto* priceReply = reinterpret_cast<PriceOracleReply*>(
        respPayload.data() + sizeof(OracleMachineReply));

    std::cout << "  " << pair.currency1 << "/" << pair.currency2 << ": ";

    if (reply->oracleMachineErrorFlags == RETURN_NO_ERROR)
    {
        if (priceReply->denominator != 0)
        {
            double price = static_cast<double>(priceReply->numerator) / priceReply->denominator;
            std::cout << std::fixed << std::setprecision(8) << price;
            if (config.verbose)
            {
                std::cout << " (num=" << priceReply->numerator << ", denom=" << priceReply->denominator << ")";
            }
            std::cout << "\n";
        }
        else
        {
            std::cout << "ERROR (zero denominator)\n";
            return false;
        }
    }
    else
    {
        std::cout << "ERROR: " << errorFlagsToString(reply->oracleMachineErrorFlags) << "\n";
        return false;
    }

    return true;
}

int main(int argc, char* argv[])
{
    Config config;

    static struct option longOptions[] = {
        {"ip", required_argument, nullptr, 'i'},
        {"port", required_argument, nullptr, 'p'},
        {"oracle", required_argument, nullptr, 'o'},
        {"pair", required_argument, nullptr, 'c'},
        {"time", required_argument, nullptr, 's'},
        {"interval", required_argument, nullptr, 'n'},
        {"pair-delay", required_argument, nullptr, 'd'},
        {"timeout", required_argument, nullptr, 't'},
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "i:p:o:c:s:n:d:t:vh", longOptions, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'i':
            config.host = optarg;
            break;
        case 'p':
            config.port = static_cast<uint16_t>(std::stoi(optarg));
            break;
        case 'o':
            config.oracle = normalizeOracleId(optarg);
            break;
        case 'c':
        {
            if (!parseCurrencyPairs(optarg, config.pairs))
            {
                std::cerr << "Error: Invalid currency pair format: " << optarg << "\n";
                std::cerr << "Expected format: C1/C2 or C1/C2,C3/C4,... (e.g., BTC/USDT,ETH/USDT)\n";
                return 1;
            }
            break;
        }
        case 's':
            config.timestamp = optarg;
            break;
        case 'n':
            config.interval = static_cast<uint32_t>(std::stoi(optarg));
            break;
        case 'd':
            config.pairDelay = static_cast<uint32_t>(std::stoi(optarg));
            break;
        case 't':
            config.timeout = static_cast<uint32_t>(std::stoi(optarg));
            break;
        case 'v':
            config.verbose = true;
            break;
        case 'h':
            printUsage(argv[0]);
            return 0;
        default:
            printUsage(argv[0]);
            return 1;
        }
    }

    // Default pair if none specified
    if (config.pairs.empty())
    {
        config.pairs.push_back({"BTC", "USDT"});
    }

    // Parse and validate timestamp if specified
    OracleDateTime fixedTimestamp;
    bool useFixedTimestamp = false;
    if (!config.timestamp.empty())
    {
        if (!parseTimestamp(config.timestamp, fixedTimestamp))
        {
            std::cerr << "Error: Invalid timestamp format: " << config.timestamp << "\n";
            std::cerr << "Expected: 0 or yyyymmddhhmmss (e.g., 20240115143022)\n";
            return 1;
        }
        useFixedTimestamp = true;
    }

    std::cout << "=== Qubic Test Node - Oracle Query ===\n";
    std::cout << "Target:   " << config.host << ":" << config.port << "\n";
    std::cout << "Oracle:   " << config.oracle << "\n";
    std::cout << "Pairs:    ";
    for (size_t i = 0; i < config.pairs.size(); ++i)
    {
        if (i > 0) std::cout << ", ";
        std::cout << config.pairs[i].currency1 << "/" << config.pairs[i].currency2;
    }
    std::cout << "\n";
    if (useFixedTimestamp)
    {
        if (fixedTimestamp.value == 0)
        {
            std::cout << "Time:     0 (current price)\n";
        }
        else
        {
            std::cout << "Time:     " << fixedTimestamp.getYear() << "-"
                      << std::setw(2) << std::setfill('0') << (int)fixedTimestamp.getMonth() << "-"
                      << std::setw(2) << std::setfill('0') << (int)fixedTimestamp.getDay() << " "
                      << std::setw(2) << std::setfill('0') << (int)fixedTimestamp.getHour() << ":"
                      << std::setw(2) << std::setfill('0') << (int)fixedTimestamp.getMinute() << ":"
                      << std::setw(2) << std::setfill('0') << (int)fixedTimestamp.getSecond() << " UTC\n";
        }
    }
    if (config.interval > 0)
    {
        std::cout << "Interval: " << config.interval << " ms\n";
    }
    std::cout << "\n";

    if (config.verbose)
    {
        std::cout << "Packet structure sizes (from qubic_core):\n";
        std::cout << "  RequestResponseHeader: " << sizeof(RequestResponseHeader) << " bytes\n";
        std::cout << "  OracleMachineQuery:    " << sizeof(OracleMachineQuery) << " bytes\n";
        std::cout << "  Price::OracleQuery:    " << sizeof(PriceOracleQuery) << " bytes\n\n";
    }

    // generate random queryId to prevent wrong OM cache hits in subsequent calls of this program
    srand(time(NULL));
    uint64_t queryId = rand();
    int exitCode = 0;

    // Main query loop
    do
    {
        // Create socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            std::cerr << "Error: Failed to create socket\n";
            return 1;
        }

        // Set socket timeout
        struct timeval tv;
        tv.tv_sec = config.timeout / 1000;
        tv.tv_usec = (config.timeout % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // Connect to OM
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(config.port);

        if (inet_pton(AF_INET, config.host.c_str(), &serverAddr.sin_addr) <= 0)
        {
            std::cerr << "Error: Invalid address\n";
            close(sock);
            return 1;
        }

        if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
        {
            std::cerr << "Error: Connection failed\n";
            close(sock);
            if (config.interval > 0)
            {
                std::cout << "Retrying in " << config.interval << " ms...\n";
                usleep(config.interval * 1000);
                continue;
            }
            return 1;
        }

        // Prepare timestamp for this query round
        OracleDateTime queryTimestamp;
        if (useFixedTimestamp)
        {
            queryTimestamp = fixedTimestamp;
        }
        else
        {
            queryTimestamp.setNow();
        }

        // Print timestamp for this query round
        std::cout << "[";
        if (queryTimestamp.value == 0)
        {
            std::cout << "current";
        }
        else
        {
            std::cout << queryTimestamp.getYear() << "-"
                      << std::setw(2) << std::setfill('0') << (int)queryTimestamp.getMonth() << "-"
                      << std::setw(2) << std::setfill('0') << (int)queryTimestamp.getDay() << " "
                      << std::setw(2) << std::setfill('0') << (int)queryTimestamp.getHour() << ":"
                      << std::setw(2) << std::setfill('0') << (int)queryTimestamp.getMinute() << ":"
                      << std::setw(2) << std::setfill('0') << (int)queryTimestamp.getSecond() << " UTC";
        }
        std::cout << "] " << config.oracle << "\n";

        // Query each currency pair
        for (size_t i = 0; i < config.pairs.size(); ++i)
        {
            const auto& pair = config.pairs[i];
            if (!sendQuery(config, pair, queryTimestamp, sock, queryId++))
            {
                exitCode = 1;
            }

            // Delay between pairs to avoid rate limiting (skip delay after last pair)
            if (config.pairDelay > 0 && i + 1 < config.pairs.size())
            {
                usleep(config.pairDelay * 1000);
            }
        }

        close(sock);

        // If interval is set, wait and repeat
        if (config.interval > 0)
        {
            std::cout << "\n";
            usleep(config.interval * 1000);
        }

    } while (config.interval > 0);

    return exitCode;
}
