#pragma once

#ifndef NO_UEFI
#define NO_UEFI
#endif

#ifndef NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES
#define NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES
#endif

// Define MSVC-style integer types for non-MSVC compilers
// TODO: verify the ‘QPI::uint64*’ {aka ‘long long unsigned int*’} to ‘uint64_t*’ {aka ‘long unsigned int*’} warning
#if !defined(_MSC_VER)
    #ifndef __int8
    #define __int8 char
    #endif
    #ifndef __int16
    #define __int16 short
    #endif
    #ifndef __int32
    #define __int32 int
    #endif
    #ifndef __int64
    #define __int64 long long
    #endif
#endif

#include <network_messages/common_def.h>

// For GCC/Clang, provide implementations of MSVC intrinsics used in qpi.h
#if !defined(_MSC_VER)
    #include <cstdint>

    // Signed 64-bit multiply returning low 64 bits and high 64 bits
    inline long long int _mul128(long long int a, long long int b, long long int* high) {
        __int128 result = static_cast<__int128>(a) * static_cast<__int128>(b);
        *high = static_cast<long long int>(result >> 64);
        return static_cast<long long int>(result);
    }

    // Unsigned 64-bit multiply returning low 64 bits and high 64 bits
    inline long long unsigned int _umul128(long long unsigned int a, long long unsigned int b, long long unsigned int* high) {
        unsigned __int128 result = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
        *high = static_cast<long long unsigned int>(result >> 64);
        return static_cast<long long unsigned int>(result);
    }
#endif

// Include platform types needed by QPI
#include "platform/common_types.h"
#include "platform/m256.h"
#include "platform/uint128.h"
#include "platform/assert.h"

// Include network message common definitions
#include "network_messages/common_def.h"

// Include pre_qpi_def.h which defines types used by QPI like USER_FUNCTION, etc.
#include "contract_core/pre_qpi_def.h"

// Include QPI core definitions
#include "contracts/qpi.h"

// Include Oracle Interface definitions
#include "oracle_core/oracle_interfaces_def.h"

/**
 * Error flags returned by OM with oracle reply.
 *
 * The return value is a 2-byte error bit mask. So multiple errors can be combined with bitwise or `|`.
 * RETURN_NO_ERROR is the absence of any error flag. In consequence it cannot be combined with one of the error flags.
 */
enum OracleErrorFlags : uint16_t
{
    RETURN_NO_ERROR = 0,                                        ///< No error; returned oracle reply is valid.
    RETURN_ERROR_INVALID_ORACLE = ORACLE_FLAG_INVALID_ORACLE,   ///< Oracle (data source) in query is invalid.
    RETURN_ERROR_ORACLE_UNAVAIL = ORACLE_FLAG_ORACLE_UNAVAIL,   ///< Oracle (data source) isn't available at the moment.
    RETURN_ERROR_INVALID_TIME = ORACLE_FLAG_INVALID_TIME,       ///< Time in query was invalid.
    RETURN_ERROR_INVALID_PLACE = ORACLE_FLAG_INVALID_PLACE,     ///< Place in query was invalid.
    RETURN_ERROR_INVALID_ARG = ORACLE_FLAG_INVALID_ARG,         ///< An argument in query was invalid.
};


// Some empty implementations of QPI functions to get rid of warnings
void QPI::AssetIssuanceIterator::begin(const QPI::AssetIssuanceSelect&) {}
QPI::uint64 QPI::AssetIssuanceIterator::assetName() const { return 0; }
QPI::id QPI::AssetIssuanceIterator::issuer() const { return QPI::id::zero(); }
void QPI::AssetOwnershipIterator::begin(const QPI::Asset&, const QPI::AssetOwnershipSelect&) {}
void QPI::AssetPossessionIterator::begin(const QPI::Asset&, const QPI::AssetOwnershipSelect&, const QPI::AssetPossessionSelect&) {}
template <typename T1, typename T2> void QPI::copyMemory(T1&, const T2&) {}
void* __acquireScratchpad(unsigned long long size, bool initZero) { return nullptr; }
void __releaseScratchpad(void* ptr) {}
void addDebugMessageAssert(const char* message, const char* file, const unsigned int lineNumber) {}
