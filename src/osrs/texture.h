#ifndef TEXTURE_H
#define TEXTURE_H

#include "graphics/dash.h"
#include "rscache/tables/textures.h"
#include "rscache/tables_dat/config_textures.h"

#include <stdbool.h>

struct SpritePackEntry
{
    int id;
    struct CacheSpritePack* spritepack;
};

struct DashTexture*
texture_new_from_definition(
    struct CacheTexture* texture_definition,
    struct DashMap* sprites_hmap);

/**
 * In old revisions (e.g. 245.2) the animated textures were hardcoded.
 * wi==64 -> 64x64 unless upscale_to_128; wi/hi==128 with half_to_64 -> 64x64;
 * otherwise normalize to 128x128 (including non-square sources e.g. 121x128).
 */
struct DashTexture*
texture_new_from_texture_sprite(
    struct CacheDatTexture* texture,
    int animation_direction,
    int animation_speed,
    bool upscale_to_128,
    bool half_to_64);

void
texture_free(struct DashTexture* texture);
#endif