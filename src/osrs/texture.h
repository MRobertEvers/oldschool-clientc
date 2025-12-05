#ifndef TEXTURE_H
#define TEXTURE_H

#include "scene_cache.h"
#include "tables/sprites.h"

#include <stdbool.h>

struct SpritePackItem
{
    int id;
    int ref_count;
    struct CacheSpritePack* sprite_pack;

    struct SpritePackItem* next;
    struct SpritePackItem* prev;
};

struct SpritePacksCache
{
    struct HMap* map;

    struct SpritePackItem root;
};

struct SpritePackWalk
{
    void* item;
    struct CacheSpritePack* sprite_pack;
};

struct SpritePacksCache* spritepacks_cache_new();
void spritepacks_cache_free(struct SpritePacksCache* spritepacks_cache);

void spritepacks_cache_add(
    struct SpritePacksCache* spritepacks_cache,
    int sprite_pack_id,
    struct CacheSpritePack* sprite_pack);
bool spritepacks_cache_contains(struct SpritePacksCache* spritepacks_cache, int sprite_pack_id);
struct CacheSpritePack*
spritepacks_cache_get(struct SpritePacksCache* spritepacks_cache, int sprite_pack_id);
struct CacheSpritePack*
spritepacks_cache_remove(struct SpritePacksCache* spritepacks_cache, int sprite_pack_id);

struct SpritePackWalk* spritepacks_cache_walk_new(struct SpritePacksCache* spritepacks_cache);
void spritepacks_cache_walk_free(struct SpritePackWalk* walk);
struct CacheSpritePack* spritepacks_cache_walk_next(struct SpritePackWalk* walk);

struct Texture*
texture_new_from_definition(struct CacheTexture* texture_definition, struct Cache* cache);
struct Texture* texture_new_from_definition_with_spritepacks(
    struct CacheTexture* texture_definition, struct CacheSpritePack** spritepacks);

#endif