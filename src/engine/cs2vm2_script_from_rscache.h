#ifndef CS2VM2_SCRIPT_FROM_RSCACHE_H
#define CS2VM2_SCRIPT_FROM_RSCACHE_H

#include "cs2vm2/cs2vm2_script.h"

#include <rscache.h>
#include <stdbool.h>

/** Moves heap fields from src into dst; leaves src empty. Returns false if op_count <= 0. */
bool
CS2VM2_ScriptFromRSCache(
    struct RSCache_CS2_Script* src,
    struct CS2VM2_Script* dst);

/** Deep-copies src into dst; src is unchanged. Returns false if op_count <= 0. */
bool
CS2VM2_ScriptCopyFromRSCache(
    const struct RSCache_CS2_Script* src,
    struct CS2VM2_Script* dst);

#endif
