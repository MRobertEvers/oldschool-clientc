#ifndef RSCACHE_DATATYPES_DAT2_TEXTURE_H
#define RSCACHE_DATATYPES_DAT2_TEXTURE_H

#include "../rscache_profile.h"

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

/*
 * Codec versions.
 *
 *  v1  the multi-sprite definition: sprite ids, types and transforms.
 *  v2  modern OSRS (rev >= 233) simplified single-sprite record — spriteId u16,
 *      averageHsl u16, opaque u8, animationDirection u8, animationSpeed u8
 *      (xrsps SpriteTextureLoader.decodeSimplified). Seven bytes, fixed.
 */
#define RSCACHE_CODEC_TEXTURE_V1 1
#define RSCACHE_CODEC_TEXTURE_V2 2

/** Archive revision above which the textures group uses the simplified record.
 *  Modern archive revisions are unix timestamps, far past any classic value, so
 *  this only has to separate "timestamp" from "small integer". */
#define RSCACHE_TEXTURE_ARCHIVE_REV_SIMPLIFIED 2000

/** Which texture codec this cache needs. */
int
RSCache_Dat2TextureCodecVersion(const struct RSCache* cache);

/**
 * Encode a texture record using the codec the profile selects.
 *
 * A multi-sprite v1 record is never byte-exact: the decoder reads and discards a
 * run of `count - 1` bytes after the sprite types, so their values are already
 * lost and re-encode as zero. Single-sprite records and all v2 records are exact.
 */
uint32_t
RSCache_Dat2TextureEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2Texture* texture,
    uint8_t* out,
    uint32_t out_capacity);

struct RSCache_Dat2Texture*
RSCache_Dat2TextureNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int length);

/** Retained entry point: decode knowing only the textures group's archive
 *  revision. */
struct RSCache_Dat2Texture*
RSCache_Dat2TextureNewDecode(
    int revision,
    char* data,
    int length);
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
