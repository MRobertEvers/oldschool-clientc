#ifndef SPRITES_H
#define SPRITES_H

#include "../cache.h"

#include <stdbool.h>
#include <stdint.h>

#define FLAG_VERTICAL 1
#define FLAG_ALPHA 2

struct CacheSprite
{
    int id;
    int file_id;
    int frame;
    // The size of the sprite in memory
    // runelite calls this maxWidth and maxHeight
    // rs-map-viewer calls this width and height
    int width;
    int height;
    // The size of the sprite on disk
    // runelite calls this width and height
    // rs-map-viewer calls this subWidth and subHeight
    // or SpriteLoader.widths[i] and SpriteLoader.heights[i]
    int crop_width;
    int crop_height;
    int offset_x;
    int offset_y;

    // Pixel data using the palette indexes.
    uint8_t* palette_pixels;
    uint8_t* pixel_alphas;
};

struct CacheSpritePack
{
    int count;
    struct CacheSprite* sprites;

    int palette_length;
    int* palette;
};

struct Cache;
struct CacheSpritePack* sprite_pack_new_from_cache(struct Cache* cache, int id);

/**
 * Not all sprites are 128x128 or 64x64 (or variations of the two), so we need to normalize them to
 * 128x128 if they're used in textures.
 * @return struct CacheSpritePack*
 */
enum SpiteLoaderFlags
{
    SPRITELOAD_FLAG_NONE = 0,
    SPRITELOAD_FLAG_NORMALIZE = 1 << 0,
};

struct CacheSpritePack*
sprite_pack_new_decode(const unsigned char* data, int length, enum SpiteLoaderFlags flags);
void sprite_pack_free(struct CacheSpritePack* pack);

int* sprite_get_pixels(struct CacheSprite* sprite, int* palette, int brightness);
int* sprite_texture_get_pixels(struct CacheSprite* sprite, int* palette, int size, int brightness);

#endif