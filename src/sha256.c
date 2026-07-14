#include "pingmote/sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Sha256Context {
    uint8_t block[64];
    uint32_t state[8];
    uint64_t bit_length;
    size_t block_length;
} Sha256Context;

static const uint32_t ROUND_CONSTANTS[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static uint32_t rotate_right(uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static void transform(Sha256Context *context)
{
    uint32_t words[64];
    for (size_t index = 0U; index < 16U; ++index) {
        const size_t offset = index * 4U;
        words[index] = ((uint32_t)context->block[offset] << 24U)
            | ((uint32_t)context->block[offset + 1U] << 16U)
            | ((uint32_t)context->block[offset + 2U] << 8U)
            | (uint32_t)context->block[offset + 3U];
    }
    for (size_t index = 16U; index < 64U; ++index) {
        const uint32_t s0 = rotate_right(words[index - 15U], 7U)
            ^ rotate_right(words[index - 15U], 18U)
            ^ (words[index - 15U] >> 3U);
        const uint32_t s1 = rotate_right(words[index - 2U], 17U)
            ^ rotate_right(words[index - 2U], 19U)
            ^ (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];
    for (size_t index = 0U; index < 64U; ++index) {
        const uint32_t sum1 = rotate_right(e, 6U)
            ^ rotate_right(e, 11U)
            ^ rotate_right(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temporary1 = h + sum1 + choose + ROUND_CONSTANTS[index] + words[index];
        const uint32_t sum0 = rotate_right(a, 2U)
            ^ rotate_right(a, 13U)
            ^ rotate_right(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256_init(Sha256Context *context)
{
    *context = (Sha256Context){
        .state = {
            UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
            UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
            UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
            UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
        }
    };
}

static void sha256_update(Sha256Context *context, const uint8_t *data, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        context->block[context->block_length++] = data[index];
        if (context->block_length == sizeof(context->block)) {
            transform(context);
            context->bit_length += UINT64_C(512);
            context->block_length = 0U;
        }
    }
}

static void sha256_finish(Sha256Context *context, uint8_t digest[32])
{
    size_t index = context->block_length;
    context->block[index++] = 0x80U;
    if (index > 56U) {
        while (index < sizeof(context->block)) {
            context->block[index++] = 0U;
        }
        transform(context);
        index = 0U;
    }
    while (index < 56U) {
        context->block[index++] = 0U;
    }

    context->bit_length += (uint64_t)context->block_length * UINT64_C(8);
    for (size_t byte = 0U; byte < 8U; ++byte) {
        context->block[63U - byte] = (uint8_t)(context->bit_length >> (byte * 8U));
    }
    transform(context);

    for (size_t word = 0U; word < 8U; ++word) {
        for (size_t byte = 0U; byte < 4U; ++byte) {
            digest[word * 4U + byte] = (uint8_t)(context->state[word] >> (24U - byte * 8U));
        }
    }
}

bool sha256_file(
    const char *path,
    char output[PINGMOTE_SHA256_HEX_CAPACITY],
    char *error,
    size_t error_capacity
)
{
    if (path == NULL || path[0] == '\0' || output == NULL) {
        set_error(error, error_capacity, "file path and digest output are required");
        return false;
    }
    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error, error_capacity, "failed to open file for verification");
        return false;
    }

    Sha256Context context;
    sha256_init(&context);
    uint8_t buffer[8192];
    size_t bytes_read = 0U;
    do {
        bytes_read = fread(buffer, 1U, sizeof(buffer), file);
        sha256_update(&context, buffer, bytes_read);
    } while (bytes_read == sizeof(buffer));
    const bool read_ok = ferror(file) == 0;
    const bool close_ok = fclose(file) == 0;
    if (!read_ok || !close_ok) {
        set_error(error, error_capacity, "failed to read file for verification");
        return false;
    }

    uint8_t digest[32];
    sha256_finish(&context, digest);
    static const char HEX[] = "0123456789abcdef";
    for (size_t index = 0U; index < sizeof(digest); ++index) {
        output[index * 2U] = HEX[digest[index] >> 4U];
        output[index * 2U + 1U] = HEX[digest[index] & 0x0FU];
    }
    output[64] = '\0';
    set_error(error, error_capacity, "");
    return true;
}

bool sha256_file_matches(
    const char *path,
    const char *expected_hex,
    char *error,
    size_t error_capacity
)
{
    if (expected_hex == NULL || strlen(expected_hex) != 64U) {
        set_error(error, error_capacity, "expected SHA-256 digest is invalid");
        return false;
    }
    char actual[PINGMOTE_SHA256_HEX_CAPACITY];
    if (!sha256_file(path, actual, error, error_capacity)) {
        return false;
    }
    if (strcmp(actual, expected_hex) != 0) {
        set_error(error, error_capacity, "download failed integrity verification");
        return false;
    }
    return true;
}
