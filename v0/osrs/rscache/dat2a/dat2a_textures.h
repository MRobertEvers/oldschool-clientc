#ifndef RSCACHE_RSCACHEDAT2A_TEXTURES_H
#define RSCACHE_RSCACHEDAT2A_TEXTURES_H

#include <stdbool.h>

enum RSCacheDat2A_TextureDirection
{
    TEXTURE_DIRECTION_NONE,
    TEXTURE_DIRECTION_V_DOWN = 1,
    TEXTURE_DIRECTION_U_DOWN = 2,
    TEXTURE_DIRECTION_V_UP = 3,
    TEXTURE_DIRECTION_U_UP = 4,
};

static const int RSCacheDat2A_TextureUDirection[] = { 0, 0, -1, 0, 1 };

static const int RSCacheDat2A_TextureVDirection[] = { 0, -1, 0, 1, 0 };

struct RSCacheDat2A_Texture
{
    int _id;

    int average_hsl;
    bool opaque;
    int* sprite_ids;
    int sprite_ids_count;

    int* sprite_types;
    int* transforms;

    // See direction above.
    int animation_direction;

    // Pixels per time unit
    int animation_speed;
};

struct RSCacheDat2Disk;
struct RSCacheDat2A_Texture*
RSCacheDat2A_TextureDefinitionNewFromCache(
    struct RSCacheDat2Disk* cache,
    int id);

void
RSCacheDat2A_TextureDefinitionFree(struct RSCacheDat2A_Texture* texture_definition);

void
RSCacheDat2A_TextureDefinitionFreeInplace(struct RSCacheDat2A_Texture* texture_definition);
struct RSCacheDat2A_Texture*
RSCacheDat2A_TextureDefinitionNewDecode(
    const unsigned char* inputData,
    int inputLength);
struct RSCacheDat2A_Texture*
RSCacheDat2A_TextureDefinitionDecodeInplace(
    struct RSCacheDat2A_Texture*,
    const unsigned char* inputData,
    int inputLength);

#endif