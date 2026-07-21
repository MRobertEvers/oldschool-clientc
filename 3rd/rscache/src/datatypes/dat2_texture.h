#ifndef RSCACHE_DATATYPES_DAT2_TEXTURE_H
#define RSCACHE_DATATYPES_DAT2_TEXTURE_H

#include <stdbool.h>

enum RSCache_Dat2TextureDirection
{
    RSCACHE_TEXTURE_DIRECTION_NONE = 0,
    RSCACHE_TEXTURE_DIRECTION_V_DOWN = 1,
    RSCACHE_TEXTURE_DIRECTION_U_DOWN = 2,
    RSCACHE_TEXTURE_DIRECTION_V_UP = 3,
    RSCACHE_TEXTURE_DIRECTION_U_UP = 4,
};

static const int RSCache_Dat2TextureUDirection[] = { 0, 0, -1, 0, 1 };

static const int RSCache_Dat2TextureVDirection[] = { 0, -1, 0, 1, 0 };

struct RSCache_Dat2Texture
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

/** revision is the js5 archive revision of the textures group. Modern OSRS
 * (rev >= 233; archive revisions are unix timestamps there) uses a simplified
 * single-sprite definition: spriteId u16, averageHsl u16, opaque u8,
 * animationDirection u8, animationSpeed u8 (xrsps SpriteTextureLoader
 * decodeSimplified). Older caches use the multi-sprite layout. */
struct RSCache_Dat2Texture*
RSCache_Dat2TextureNewDecode(
    int revision,
    char* buffer,
    int buffer_size);
void
RSCache_Dat2TextureFree(struct RSCache_Dat2Texture* texture);
void
RSCache_Dat2TextureFreeInplace(struct RSCache_Dat2Texture* texture);
struct RSCache_Dat2Texture*
RSCache_Dat2TextureDecodeInplace(
    struct RSCache_Dat2Texture* texture,
    char* buffer,
    int buffer_size);

#endif
