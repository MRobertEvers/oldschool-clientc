#ifndef RSCACHE_TOOLS_TOOL_PROFILE_H
#define RSCACHE_TOOLS_TOOL_PROFILE_H

#include "rscache.h"

/**
 * Resolve a cache identity from CLI flags.
 *
 * Pass either --rev NAME, or --game/--epoch/--revision (and optionally --quirks).
 * Returns 1 on success, 0 on failure (message already printed to stderr).
 */
int
tool_resolve_profile(
    const char* rev_name,
    const char* game_name,
    const char* epoch_name,
    const char* revision_text,
    const char* quirks_list,
    struct RSCache* out);

/** Print the identity line used by dump tools. */
void
tool_print_profile(
    const char* cache_dir,
    const struct RSCache* profile);

#endif
