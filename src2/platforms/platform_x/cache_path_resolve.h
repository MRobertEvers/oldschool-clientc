#ifndef CACHE_PATH_RESOLVE_H
#define CACHE_PATH_RESOLVE_H

#include <stdbool.h>

/** True when dir contains main_file_cache.dat2. */
bool
cache_path_has_dat2(char const* dir);

/** True when both paths refer to the same directory (realpath when possible). */
bool
cache_path_same_dir(char const* a, char const* b);

/** Resolve <repo-root>/cache.kronos (must contain main_file_cache.dat2). Returns static buffer or NULL. */
char const*
cache_path_resolve_kronos_repo(void);

/** Resolve <repo-root>/cache (must contain main_file_cache.dat2). Returns static buffer or NULL. */
char const*
cache_path_resolve_osrs_repo(void);

/**
 * Resolve sibling xrsps-typescript rev-237 cache used by the TS interface editor and
 * cs2_parity goldens: <3draster>/../xrsps-typescript/caches/osrs-237_2026-03-25.
 * Returns static buffer or NULL.
 */
char const*
cache_path_resolve_xrsps_osrs237(void);

#endif
