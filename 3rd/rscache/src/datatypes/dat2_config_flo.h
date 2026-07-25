#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_FLO_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_FLO_H

#include "../rsbuffer.h"

#include <stdbool.h>

struct RSCache_Dat2ConfigUnderlay
{
    int _id;
    int rgb_color;
};

struct RSCache_Dat2ConfigOverlay
{
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
