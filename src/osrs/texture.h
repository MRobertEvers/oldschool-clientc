#ifndef TEXTURE_H
#define TEXTURE_H

#include "graphics/dash.h"
#include "osrs/rscache/dat2a/rscache_dat2a_textures.h"
#include "osrs/rscache/dat1a/rscache_dat1a_config_textures.h"

#include <stdbool.h>

struct SpritePackEntry
{
    int id;
    struct RSCacheDat2A_SpritePack* spritepack;
};

struct DashTexture*
texture_new_from_definition(
    struct RSCacheDat2A_Texture* texture_definition,
    struct DashMap* sprites_hmap);

/**
 * In old revisions (e.g. 245.2) the animated textures were hardcoded.
 * wi==64 -> 64x64 unless upscale_to_128; wi/hi==128 with half_to_64 -> 64x64;
 * otherwise normalize to 128x128 (including non-square sources e.g. 121x128).
 */
struct DashTexture*
texture_new_from_texture_sprite(
    struct RSCacheDat1A_ConfigTexture* texture,
    int animation_direction,
    int animation_speed,
    bool upscale_to_128,
    bool half_to_64);

struct ToriDraw_Texture;

struct ToriDraw_Texture*
texture_new_toridraw_from_texture_sprite(
    struct RSCacheDat1A_ConfigTexture* texture,
    int animation_direction,
    int animation_speed,
    bool upscale_to_128,
    bool half_to_64);

void
texture_free(struct DashTexture* texture);
#endif