#ifndef RSCACHE_CS2_LOSSLESS_H
#define RSCACHE_CS2_LOSSLESS_H

#include "../datatypes/clientscript.h"
#include "cs2_support.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Stable fingerprint used to make a lossless snapshot conditional on unchanged source. */
uint64_t
RSCache_CS2_LosslessHash(const char* data, size_t length);

/** Deep equality over every serialized clientscript field. */
bool
RSCache_CS2_LosslessEqual(
    const struct RSCache_CS2_Script* left,
    const struct RSCache_CS2_Script* right);

/** Append a versioned, hexadecimal snapshot of the decoded script. */
bool
RSCache_CS2_LosslessEncode(
    const struct RSCache_CS2_Script* script,
    struct RSCache_CS2_StrBuf* out);

/** Decode one snapshot. `end` receives the first byte after its hexadecimal payload. */
bool
RSCache_CS2_LosslessDecode(
    const char* text,
    struct RSCache_ClientScript* out,
    const char** end);

#endif
