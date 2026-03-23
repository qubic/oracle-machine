#ifndef SCRYPT_H
#define SCRYPT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief A context struct for SHA-256 computation.
     */
    typedef struct SHA256Context
    {
        uint32_t state[8];
        uint32_t count[2];
        unsigned char buf[64];
    } SHA256_CTX;

    /**
     * @brief SHA-256 initialization. Begin a SHA-256 operation.
     * @param ctx Context for SHA-256 computation.
     */
    void SHA256_Init(SHA256_CTX* ctx);

    /**
     * @brief Add bytes into the hash.
     * @param ctx Context for SHA-256 computation.
     * @param in Pointer to the input.
     * @param len Size of the input.
     */
    void SHA256_Update(SHA256_CTX* ctx, const void* in, size_t len);

    /**
     * @brief SHA-256 finalization: Pad the input data, export the hash value, clear the context state.
     * @param digest Output array to write the hash to.
     * @param ctx Context for SHA-256 computation.
     */
    void SHA256_Final(unsigned char digest[32], SHA256_CTX* ctx);

    /**
     * @brief Compute scrypt hash with parameters N=1024 (iteration count),
              r=1 (block size), p=1 (parallelization), len=256 (output length in bits).
     * @param input Pointer to the input data.
     * @param output Pointer to an output buffer to write to.
     */
    void scrypt_1024_1_1_256(const char* input, char* output);

#ifdef __cplusplus
}
#endif

#endif
