#ifndef CACHE_PATH_RESOLVE_H
#define CACHE_PATH_RESOLVE_H

/** Resolve <repo-root>/cache.kronos (must contain main_file_cache.dat2). Returns static buffer or NULL. */
char const*
cache_path_resolve_kronos_repo(void);

/** Resolve <repo-root>/cache (must contain main_file_cache.dat2). Returns static buffer or NULL. */
char const*
cache_path_resolve_osrs_repo(void);

#endif
