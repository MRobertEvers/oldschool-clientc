#ifndef RSCACHE_DATATYPES_DAT2_SPRITES_H
#define RSCACHE_DATATYPES_DAT2_SPRITES_H

#include <stdint.h>

#define FLAG_VERTICAL 1
#define FLAG_ALPHA 2

enum RSCache_SpriteLoadFlags
{
    RSCACHE_SPRITELOAD_FLAG_NONE = 0,
    RSCACHE_SPRITELOAD_FLAG_NORMALIZE = 1 << 0,
};

struct RSCache_Dat2Sprite
{
    int id;
    int file_id;
    int frame;
    /* Size of the sprite in memory (runelite maxWidth/maxHeight). */
    int width;
    int height;
    /* Size of the sprite on disk (runelite width/height). */
    int crop_width;
    int crop_height;
    int offset_x;
    int offset_y;

    uint8_t* palette_pixels;
    uint8_t* pixel_alphas;
};

struct RSCache_Dat2SpritePack
{
    int count;
    struct RSCache_Dat2Sprite* sprites;

    int palette_length;
    int* palette;
};

struct RSCache_Dat2SpritePack*
RSCache_Dat2SpritePackNewDecode(
    const unsigned char* data,
    int length,
    enum RSCache_SpriteLoadFlags flags);

void
RSCache_Dat2SpritePackFree(struct RSCache_Dat2SpritePack* pack);

int*
RSCache_Dat2SpriteGetPixels(
    struct RSCache_Dat2Sprite* sprite,
    int* palette,
    int brightness);

#endif
