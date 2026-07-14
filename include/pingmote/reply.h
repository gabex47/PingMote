#ifndef PINGMOTE_REPLY_H
#define PINGMOTE_REPLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool reply_sanitize(const char *input, char *output, size_t output_capacity);
const char *reply_offline_fallback(uint64_t seed);

#endif
