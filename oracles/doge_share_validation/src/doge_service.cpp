#include "doge_service.h"
#include "scrypt.h"
#include "om_common/logger.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <numeric>
#include <span>
#include <sstream>
#include <thread>

namespace oracle
{

DogeService::DogeService(const std::string& rHostname, uint16_t hostPort) :
    BaseOracleService(
        rHostname,
        hostPort,
        "Doge",
        DOGE_ORACLE_QUERY_SIZE,
        DOGE_ORACLE_REPLY_SIZE)
{
    
}

DogeService::~DogeService()
{
    printStatistics();
}

static bool verifyHashVsTarget(const std::array<uint8_t, 32>& hash, const std::array<uint8_t, 32>& target)
{
    // Start from the most significant byte (the end of the array).
    for (int i = 31; i >= 0; --i)
    {
        if (hash[i] < target[i])
            return true; // hash is smaller
        if (hash[i] > target[i])
            return false; // hash is larger
    }
    return true; // exactly equal
}

std::array<uint8_t, 32> doubleSHA256(const std::span<uint8_t>& data)
{
    SHA256_CTX ctx;
    std::array<uint8_t, 32> firstHash;
    std::array<uint8_t, 32> secondHash;

    // --- Round 1: Hash the input data ---
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data.data(), data.size());
    SHA256_Final(firstHash.data(), &ctx);

    // --- Round 2: Hash the result of Round 1 ---
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, firstHash.data(), firstHash.size());
    SHA256_Final(secondHash.data(), &ctx);

    return secondHash;
}

static std::array<uint8_t, 32> calculateMerkleRoot(
    const std::span<uint8_t>& coinbase1,
    const std::span<uint8_t>& coinbase2,
    const std::span<uint8_t>& extraNonce1,
    const std::span<uint8_t>& extraNonce2,
    const std::span<std::vector<uint8_t>>& merkleBranches)
{
    // Build the coinbase transaction by concatenation.
    std::vector<uint8_t> coinbase;
    coinbase.reserve(coinbase1.size() + extraNonce1.size() + extraNonce2.size() + coinbase2.size());

    coinbase.insert(coinbase.end(), coinbase1.begin(), coinbase1.end());
    coinbase.insert(coinbase.end(), extraNonce1.begin(), extraNonce1.end());
    coinbase.insert(coinbase.end(), extraNonce2.begin(), extraNonce2.end());
    coinbase.insert(coinbase.end(), coinbase2.begin(), coinbase2.end());

    // Initial hash of the coinbase (the first leaf).
    std::array<uint8_t, 32> currentHash = doubleSHA256(coinbase);

    // Iteratively hash with merkle branches. The hash derived from the coinbase is always on the
    // left.
    for (const auto& branch : merkleBranches)
    {
        std::vector<uint8_t> concat;
        concat.reserve(currentHash.size() + branch.size());

        concat.insert(concat.end(), currentHash.begin(), currentHash.end());
        concat.insert(concat.end(), branch.begin(), branch.end());

        currentHash = doubleSHA256(concat);
    }

    return currentHash;
}


// ============================================================================
// BaseOracleService Implementation
uint16_t DogeService::processInterfaceQuery(
    const std::vector<uint8_t>& queryPayload,
    std::vector<uint8_t>& replyPayload)
{
    // Parse the query
    if (queryPayload.size() < DOGE_ORACLE_QUERY_SIZE)
    {
        OM_LOG_ERROR() << "[Doge] Invalid query size: " << queryPayload.size();
        return RETURN_ERROR_INVALID_ARG;
    }

    // Prepare the output struct
    replyPayload.resize(sizeof(DogeShareValidation::OracleReply));

    // Provide typed access to payload of query and reply
    const auto* query = reinterpret_cast<const DogeShareValidation::OracleQuery*>(queryPayload.data());
    auto* reply = reinterpret_cast<DogeShareValidation::OracleReply*>(replyPayload.data());

    // Get computor index for reply. It is encoded in first 4 bytes of extraNonce2 (big endian byte order).
    uint32_t compIdxFromEN2 = 0;
    for (int i = 0; i < 4; ++i)
        compIdxFromEN2 = (compIdxFromEN2 << 8) | query->solutionExtraNonce2.get(i);
    compIdxFromEN2 %= NUMBER_OF_COMPUTORS;
    reply->compIndex = compIdxFromEN2;

    // Check size of additional data before reading it
    uint32 additionalDataSize = query->extraNonce1NumBytes + query->coinbase1NumBytes +
                                query->coinbase2NumBytes +
                                query->numMerkleBranches * sizeof(unsigned int);
    if (additionalDataSize > DogeShareValidation::OracleQuery::additionalDataSize)
    {
        OM_LOG_ERROR() << "[Doge] Invalid additionalDataSize: " << additionalDataSize;
        return RETURN_ERROR_INVALID_ARG;
    }

    // Gather task information for calculating Merkle root
    const uint8_t* payload = query->additionalData;
    std::vector<uint8_t> extraNonce1(payload, payload + query->extraNonce1NumBytes);
    payload += query->extraNonce1NumBytes;
    std::vector<uint8_t> coinbase1(payload, payload + query->coinbase1NumBytes);
    payload += query->coinbase1NumBytes;
    std::vector<uint8_t> coinbase2(payload, payload + query->coinbase2NumBytes);
    payload += query->coinbase2NumBytes;
    std::vector<std::vector<uint8_t>> merkleBranches;
    const unsigned int* branchesNumBytes = reinterpret_cast<const unsigned int*>(payload);
    additionalDataSize +=
        std::accumulate(branchesNumBytes, branchesNumBytes + query->numMerkleBranches, 0);
    if (additionalDataSize > DogeShareValidation::OracleQuery::additionalDataSize)
    {
        OM_LOG_ERROR() << "[Doge] Invalid additionalDataSize: " << additionalDataSize;
        return RETURN_ERROR_INVALID_ARG;
    }
    payload += query->numMerkleBranches * sizeof(unsigned int);
    for (unsigned int b = 0; b < query->numMerkleBranches; ++b)
    {
        merkleBranches.push_back(std::vector<uint8_t>(payload, payload + branchesNumBytes[b]));
        payload += branchesNumBytes[b];
    }

    // Copy solution extraNonce2
    std::array<uint8_t, 8> extraNonce2;
    memcpy(extraNonce2.data(), &query->solutionExtraNonce2, 8);

    // Calculate Merkle root
    auto merkleRoot = calculateMerkleRoot(coinbase1, coinbase2, extraNonce1, extraNonce2, merkleBranches);

    // Build full header from task and solution info
    std::array<uint8_t, 80> fullHeader;
    memcpy(
        fullHeader.data(),
        &query->taskPartialHeaderVersion,
        sizeof(query->taskPartialHeaderVersion));
    unsigned int offset = sizeof(query->taskPartialHeaderVersion);
    memcpy(
        fullHeader.data() + offset,
        &query->taskPartialHeaderPrevBlockHash,
        sizeof(query->taskPartialHeaderPrevBlockHash));
    offset += sizeof(query->taskPartialHeaderPrevBlockHash);
    memcpy(fullHeader.data() + offset, merkleRoot.data(), merkleRoot.size());
    offset += merkleRoot.size();
    memcpy(fullHeader.data() + offset, &query->solutionTime, sizeof(query->solutionTime));
    offset += sizeof(query->solutionTime);
    memcpy(
        fullHeader.data() + offset,
        &query->taskPartialHeaderDifficultyNBits,
        sizeof(query->taskPartialHeaderDifficultyNBits));
    offset += sizeof(query->taskPartialHeaderDifficultyNBits);
    memcpy(fullHeader.data() + offset, &query->solutionNonce, sizeof(query->solutionNonce));
    offset += sizeof(query->solutionNonce);
    if (offset != 80)
    {
        OM_LOG_ERROR() << "[Doge] Something is wrong with the header size (should be 80 bytes): got " << offset;
        return RETURN_ERROR_INVALID_ARG;
    }

    // Compute 32-bytes hash from 80-bytes full header of task and solution info
    std::array<uint8_t, 32> hash;
    scrypt_1024_1_1_256(
        reinterpret_cast<const char*>(fullHeader.data()), reinterpret_cast<char*>(hash.data()));

    // Compare hash with target difficulty
    const std::array<uint8_t, 32>& target =
        *reinterpret_cast<const std::array<uint8_t, 32>*>(&query->target);
    reply->isValid = verifyHashVsTarget(hash, target);

    OM_LOG_DEBUG() << "  Result: doge share is " << (reply->isValid ? "valid" : "invalid");

    return 0;
}


}
