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

    /*
     * Per-texel alpha blending (extension; see RSCACHE_TEXTURE_V2_ALPHA_BYTE).
     *
     * Stock OSRS textures are colour-keyed: a texel is drawn or skipped, and
     * `opaque` says which of the two paths the raster takes. That cannot carry
     * a material whose alpha varies continuously, so imported HD content has to
     * either be thresholded into holes or dropped. When this is set the raster
     * blends each texel by its own alpha instead.
     *
     * Never set by a stock cache: the simplified record is exactly seven bytes
     * there, and this is an optional eighth.
     */
    bool alpha_blended;

    /*
     * Modulate the texel by the face's own colour (extension; same byte).
     *
     * A stock texture is a diffuse map: it carries the surface's colour and the
     * raster only shades it. An imported RS727 blend layer is the opposite - a
     * greyscale mask whose surface colour lives on the face - so it needs the
     * face's chroma multiplied in. Setting this on a stock texture would be
     * wrong, which is why it is a per-texture flag and not a raster mode.
     *
     * Never set by a stock cache, for the same reason as alpha_blended.
     */
    bool modulate;

    /*
     * Use the texture as a detail map over the face's own shaded colour rather
     * than as the surface (extension; same byte).
     *
     * For an imported HD program that is not a diffuse map at all: rendered as
     * a surface it is blown-out white, skipped it takes the model's detail with
     * it. As a detail map it scales the colour the face would have had, so a
     * program that bakes to nothing useful degrades to the flat fallback rather
     * than to garbage. Implies the face is drawn opaque.
     */
    bool detail;

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

/*
 * Length of the stock simplified record, and the offset of the optional flags
 * byte that extends it.
 *
 * The v2 record is fixed-width with no opcode stream, so there is no in-band
 * way to announce a new field. A stock cache always writes exactly
 * RSCACHE_TEXTURE_V2_LENGTH bytes, so a record longer than that is
 * unambiguously ours: readers that predate the extension stop at seven and are
 * unaffected, and a stock record decodes with every extension flag false.
 *
 * The byte is a bitfield rather than a bool so later extensions cost no further
 * bytes; value 1 remains "alpha only", which is what the first version of this
 * extension wrote.
 */
#define RSCACHE_TEXTURE_V2_LENGTH 7
#define RSCACHE_TEXTURE_V2_ALPHA_BYTE 7
#define RSCACHE_TEXTURE_EXT_ALPHA_BLENDED 0x1
#define RSCACHE_TEXTURE_EXT_MODULATE 0x2
#define RSCACHE_TEXTURE_EXT_DETAIL 0x4

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
