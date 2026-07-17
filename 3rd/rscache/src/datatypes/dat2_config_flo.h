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
