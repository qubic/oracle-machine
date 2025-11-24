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
    inline int64_t _mul128(int64_t a, int64_t b, int64_t* high) {
        __int128 result = static_cast<__int128>(a) * static_cast<__int128>(b);
        *high = static_cast<int64_t>(result >> 64);
        return static_cast<int64_t>(result);
    }
    
    // Unsigned 64-bit multiply returning low 64 bits and high 64 bits
    inline uint64_t _umul128(uint64_t a, uint64_t b, uint64_t* high) {
        unsigned __int128 result = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
        *high = static_cast<uint64_t>(result >> 64);
        return static_cast<uint64_t>(result);
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