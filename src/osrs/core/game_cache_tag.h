#ifndef OSRS_CORE_GAME_CACHE_TAG_H
#define OSRS_CORE_GAME_CACHE_TAG_H

#include <stddef.h>

struct BuildCache;
struct BuildCacheDat;

/** Tagged opaque handle for BuildCache vs BuildCacheDat (not rscache `struct Cache`). */
enum GameCacheTagKind
{
    GAME_CACHE_TAG_KIND_INVALID = 0,
    GAME_CACHE_TAG_KIND_BUILDCACHE = 1,
    GAME_CACHE_TAG_KIND_BUILDCACHEDAT = 2,
};

struct GameCacheTag
{
    enum GameCacheTagKind kind;
    void* impl;
};

struct GameCacheTag
game_cache_tag_new_buildcache(void);

struct GameCacheTag
game_cache_tag_new_buildcachedat(void);

void
game_cache_tag_free(struct GameCacheTag* c);

enum GameCacheTagKind
game_cache_tag_kind(const struct GameCacheTag* c);

struct BuildCache*
game_cache_tag_as_buildcache(const struct GameCacheTag* c);

struct BuildCacheDat*
game_cache_tag_as_buildcachedat(const struct GameCacheTag* c);

#endif
