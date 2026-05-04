#include "osrs/core/game_cache_tag.h"

#include "osrs/buildcache.h"
#include "osrs/buildcachedat.h"

#include <assert.h>
#include <stdlib.h>

struct GameCacheTag
game_cache_tag_new_buildcache(void)
{
    struct BuildCache* b = buildcache_new();
    struct GameCacheTag c = { .kind = GAME_CACHE_TAG_KIND_BUILDCACHE, .impl = b };
    return c;
}

struct GameCacheTag
game_cache_tag_new_buildcachedat(void)
{
    struct BuildCacheDat* d = buildcachedat_new();
    struct GameCacheTag c = { .kind = GAME_CACHE_TAG_KIND_BUILDCACHEDAT, .impl = d };
    return c;
}

void
game_cache_tag_free(struct GameCacheTag* c)
{
    if( !c || c->kind == GAME_CACHE_TAG_KIND_INVALID )
        return;
    switch( c->kind )
    {
    case GAME_CACHE_TAG_KIND_BUILDCACHE:
        buildcache_free((struct BuildCache*)c->impl);
        break;
    case GAME_CACHE_TAG_KIND_BUILDCACHEDAT:
        buildcachedat_free((struct BuildCacheDat*)c->impl);
        break;
    case GAME_CACHE_TAG_KIND_INVALID:
        break;
    }
    c->kind = GAME_CACHE_TAG_KIND_INVALID;
    c->impl = NULL;
}

enum GameCacheTagKind
game_cache_tag_kind(const struct GameCacheTag* c)
{
    if( !c )
        return GAME_CACHE_TAG_KIND_INVALID;
    return c->kind;
}

struct BuildCache*
game_cache_tag_as_buildcache(const struct GameCacheTag* c)
{
    assert(c && c->kind == GAME_CACHE_TAG_KIND_BUILDCACHE);
    return (struct BuildCache*)c->impl;
}

struct BuildCacheDat*
game_cache_tag_as_buildcachedat(const struct GameCacheTag* c)
{
    assert(c && c->kind == GAME_CACHE_TAG_KIND_BUILDCACHEDAT);
    return (struct BuildCacheDat*)c->impl;
}
