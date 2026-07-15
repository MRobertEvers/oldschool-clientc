#ifndef RSCACHE_RSCACHEDAT2A_SPRITES_H
#define RSCACHE_RSCACHEDAT2A_SPRITES_H

#include "../dat2disk/dat2disk.h"

#include <stdbool.h>
#include <stdint.h>

#define FLAG_VERTICAL 1
#define FLAG_ALPHA 2

struct RSCacheDat2A_Sprite
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

struct RSCacheDat2A_SpritePack
{
    int count;
    struct RSCacheDat2A_Sprite* sprites;

    int palette_length;
    int* palette;
};

struct RSCacheDat2Disk;
struct RSCacheDat2A_SpritePack*
RSCacheDat2A_SpritePackNewFromCache(
    struct RSCacheDat2Disk* cache,
    int id);

/**
 * Not all sprites are 128x128 or 64x64 (or variations of the two), so we need to normalize them to
 * 128x128 if they're used in textures.
 * @return struct RSCacheDat2A_SpritePack*
 */
enum RSCacheDat2A_SpriteLoaderFlags
{
    SPRITELOAD_FLAG_NONE = 0,
    SPRITELOAD_FLAG_NORMALIZE = 1 << 0,
};

struct RSCacheDat2A_SpritePack*
RSCacheDat2A_SpritePackNewDecode(
    const unsigned char* data,
    int length,
    enum RSCacheDat2A_SpriteLoaderFlags flags);
void
RSCacheDat2A_SpritePackFree(struct RSCacheDat2A_SpritePack* pack);

int*
RSCacheDat2A_SpriteGetPixels(
    struct RSCacheDat2A_Sprite* sprite,
    int* palette,
    int brightness);
int*
RSCacheDat2A_SpriteTextureGetPixels(
    struct RSCacheDat2A_Sprite* sprite,
    int* palette,
    int size,
    int brightness);

#endif