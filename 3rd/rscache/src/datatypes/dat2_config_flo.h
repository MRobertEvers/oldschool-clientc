#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_FLO_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_FLO_H

#include "../rsbuffer.h"

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

struct RSCache_Dat2ConfigUnderlay
{
    /** RS2 only: opcode 2, a u16 texture id (-1 when the record said 65535). */
    int rs2_texture;
    /** RS2 only: opcode 3, already shifted left by 2 as the reference does. */
    int rs2_scale;
    int _id;
    int rgb_color;
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
};

/** Encode an overlay record. Fields at their decode default are omitted, and the
 *  name field uses the newline terminator its decoder expects. */
uint32_t
RSCache_Dat2ConfigOverlayEncode(
    const struct RSCache_Dat2ConfigOverlay* overlay,
    uint8_t* out,
    uint32_t out_capacity);

/** Encode an underlay record — a single opcode-1 colour, or a bare terminator when
 *  the colour is 0. */
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

#endif
