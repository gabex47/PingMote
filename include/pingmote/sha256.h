#ifndef PINGMOTE_SHA256_H
#define PINGMOTE_SHA256_H

#include <stdbool.h>
#include <stddef.h>

#define PINGMOTE_SHA256_HEX_CAPACITY 65U

bool sha256_file(
    const char *path,
    char output[PINGMOTE_SHA256_HEX_CAPACITY],
    char *error,
    size_t error_capacity
);
bool sha256_file_matches(
    const char *path,
    const char *expected_hex,
    char *error,
    size_t error_capacity
);

#endif
