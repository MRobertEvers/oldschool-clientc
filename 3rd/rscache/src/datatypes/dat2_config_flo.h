#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_FLO_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_FLO_H

#include "../rsbuffer.h"
#include "../rscache_presence.h"

#include <stdbool.h>

/**
 * RS2 (643) flo decode.
 *
 * Needed because opcode 3 *conflicts* between the eras: the dat1/OSRS reading treats it as
 * a bare flag on an overlay, while RS2 makes it a u16 texture with 65535 meaning none. Extra
 * cases alone cannot express that, hence a flag.
 *
 * Opcode tables per void's OverlayDecoder.kt / UnderlayDecoder.kt.
 */
#define RSCACHE_CONFIG_FLO_DECODE_RS2 1

/* Structural codecs, as for loc: opcode 3's *meaning* differs, not its width. */
#define RSCACHE_CODEC_FLO_OSRS 1
#define RSCACHE_CODEC_FLO_RS2 2

/** Which flo codec this cache uses (shared by underlay and overlay). */
int
RSCache_Dat2ConfigFloCodecVersion(const struct RSCache* cache);

/** Decode flags for this cache, derived from the codec version. */
int
RSCache_Dat2ConfigFloFlags(const struct RSCache* cache);

/**
 * The fields an underlay stream can state, for `RSCache_Dat2ConfigUnderlay.present`.
 * RS2's flag opcodes 4 and 5 are consumed and not stored, so they have no field.
 */
enum RSCache_Dat2ConfigUnderlayField
{
    RSCACHE_UNDERLAY_FIELD_COLOUR = 0, /* 1 */
    RSCACHE_UNDERLAY_FIELD_RS2_TEXTURE, /* RS2 2 */
    RSCACHE_UNDERLAY_FIELD_RS2_SCALE,   /* RS2 3 */
};

/**
 * The fields an overlay stream can state, for `RSCache_Dat2ConfigOverlay.present`.
 *
 * Keyed by meaning: the texture is opcode 2 (u8) on OldSchool and opcode 3 (u16)
 * on RS2, and both decode to TEXTURE. RS2's flag opcodes 8, 10 and 12 are consumed
 * and not stored, so they have no field.
 */
enum RSCache_Dat2ConfigOverlayField
{
    RSCACHE_OVERLAY_FIELD_COLOUR = 0,      /* 1 */
    RSCACHE_OVERLAY_FIELD_TEXTURE,         /* 2; RS2 3 */
    RSCACHE_OVERLAY_FIELD_FLOTYPE,         /* 3, OldSchool: a bare flag */
    RSCACHE_OVERLAY_FIELD_SHOW_UNDERLAY,   /* 5: clears hide_underlay */
    RSCACHE_OVERLAY_FIELD_FLOTYPE_NAME,    /* 6 */
    RSCACHE_OVERLAY_FIELD_BLEND_COLOUR,    /* 7 */
    RSCACHE_OVERLAY_FIELD_RS2_SCALE,       /* RS2 9 */
    RSCACHE_OVERLAY_FIELD_RS2_OPCODE_11,   /* RS2 11 */
    RSCACHE_OVERLAY_FIELD_RS2_WATER_COLOUR,     /* RS2 13 */
    RSCACHE_OVERLAY_FIELD_RS2_WATER_SCALE,      /* RS2 14 */
    RSCACHE_OVERLAY_FIELD_RS2_SECONDARY_TEXTURE, /* RS2 15 */
    RSCACHE_OVERLAY_FIELD_RS2_WATER_INTENSITY,  /* RS2 16 */
};

struct RSCache_Dat2ConfigUnderlay
{
    /** RS2 only: opcode 2, a u16 texture id (-1 when the record said 65535). */
    int rs2_texture;
    /** RS2 only: opcode 3, already shifted left by 2 as the reference does. */
    int rs2_scale;
    int _id;
    int rgb_color;
    /** Which fields the stream stated (RSCACHE_UNDERLAY_FIELD_*). */
    struct RSCache_Presence present;
};

struct RSCache_Dat2ConfigOverlay
{
    /* RS2 (643) only. Carried under their opcode numbers where the meaning is not
     * established; water_* are named because the reference names them. */
    int rs2_scale;
    int rs2_opcode_11;
    int rs2_water_colour;
    int rs2_water_scale;
    int rs2_water_intensity;
    /** RS2 only: opcode 15, a u16 secondary texture id (-1 when the record said 65535). */
    int rs2_secondary_texture;
    int _id;

    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;

    // Used in dat. Not used in dat2.
    bool flotype_overlay;
    char* flotype_name;
    /** Which fields the stream stated (RSCACHE_OVERLAY_FIELD_*). */
    struct RSCache_Presence present;
};

/** Encode an overlay record: exactly the OldSchool fields `present` states. The
 *  name field uses the newline terminator its decoder expects. The RS2 fields are
 *  decoded but this encoder cannot write them. */
uint32_t
RSCache_Dat2ConfigOverlayEncode(
    const struct RSCache_Dat2ConfigOverlay* overlay,
    uint8_t* out,
    uint32_t out_capacity);

/** Encode an underlay record — opcode 1 when `present` states the colour (0 is a
 *  colour), else a bare terminator. */
uint32_t
RSCache_Dat2ConfigUnderlayEncode(
    const struct RSCache_Dat2ConfigUnderlay* underlay,
    uint8_t* out,
    uint32_t out_capacity);

struct RSCache_Dat2ConfigOverlay*
RSCache_Dat2ConfigOverlayNewDecode(
    char* buffer,
    int buffer_size);
int
RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
    struct RSCache_Dat2ConfigOverlay* overlay,
    char* data,
    int data_size,
    int flags);

void
RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
    struct RSCache_Dat2ConfigUnderlay* underlay,
    char* data,
    int data_size,
    int flags);

int
RSCache_Dat2ConfigOverlayDecodeInplace(
    struct RSCache_Dat2ConfigOverlay* overlay,
    char* buffer,
    int buffer_size);
void
RSCache_Dat2ConfigOverlayFree(struct RSCache_Dat2ConfigOverlay* overlay);
void
RSCache_Dat2ConfigOverlayFreeInplace(struct RSCache_Dat2ConfigOverlay* overlay);

struct RSCache_Dat2ConfigUnderlay*
RSCache_Dat2ConfigUnderlayNewDecode(
    char* buffer,
    int buffer_size);
void
RSCache_Dat2ConfigUnderlayDecodeInplace(
    struct RSCache_Dat2ConfigUnderlay* underlay,
    char* buffer,
    int buffer_size);
void
RSCache_Dat2ConfigUnderlayFree(struct RSCache_Dat2ConfigUnderlay* underlay);
void
RSCache_Dat2ConfigUnderlayFreeInplace(struct RSCache_Dat2ConfigUnderlay* underlay);

/** Upper bounds on what the encoders above will write. */
uint32_t
RSCache_Dat2ConfigOverlayEncodeBound(const struct RSCache_Dat2ConfigOverlay* overlay);

uint32_t
RSCache_Dat2ConfigUnderlayEncodeBound(const struct RSCache_Dat2ConfigUnderlay* underlay);

#endif
