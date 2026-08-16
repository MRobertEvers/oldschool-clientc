#ifndef BLENDMAP_H
#define BLENDMAP_H

#include <stdint.h>

#define BLENDMAP_HSL16_NONE -1

enum BlendmapStatus
{
    BLENDMAP_STATUS_UNBLENDED = 0,
    BLENDMAP_STATUS_BLENDED = 1,
};

struct Blendmap
{
    enum BlendmapStatus status;

    uint32_t* underlaymap;
    int32_t* blendmap;
    int width;
    int height;
    int levels;
};

struct Blendmap*
blendmap_new(
    int width,
    int height,
    int levels);

void
blendmap_free(struct Blendmap* blendmap);

void
blendmap_set_underlay_rgb(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level,
    uint32_t rgb);

uint32_t
blendmap_get_underlay_rgb(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level);

int32_t
blendmap_get_blended_hsl16(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level);

void
blendmap_build(struct Blendmap* blendmap);

#endif