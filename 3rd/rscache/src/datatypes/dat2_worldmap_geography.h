#ifndef RSCACHE_DATATYPES_DAT2_WORLDMAP_GEOGRAPHY_H
#define RSCACHE_DATATYPES_DAT2_WORLDMAP_GEOGRAPHY_H

#include "../rsbuffer.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * World map geography (dat2 table 18) — the tiles behind one compositemap
 * record. This is *not* a map square from the maps table: it is a flattened,
 * render-ready copy holding only what the world map draws (floor ids, overlay
 * shapes, and the locs that become wall lines or map scene icons), with no
 * heights, no collision and no full loc list.
 *
 * Two layouts, tagged by a leading marker byte that repeats the record kind:
 *   0 — a whole 64x64 region, tiles in x-major order
 *   1 — one 8x8 chunk of a region, addressed at (chunk_x*8, chunk_y*8)
 *
 * Both write into the same 64x64 grid, so a region assembled from chunk records
 * decodes several files into one struct.
 */

#define RSCACHE_WORLDMAP_TILE_COUNT 64
#define RSCACHE_WORLDMAP_TILE_AREA (RSCACHE_WORLDMAP_TILE_COUNT * RSCACHE_WORLDMAP_TILE_COUNT)
#define RSCACHE_WORLDMAP_MAX_PLANES 4

/** One loc on a tile, reduced to what the map draws it as. */
struct RSCache_WorldMapDecor
{
    int loc_id;
    int shape;    /* loc shape: 0-3 and 9 are walls, 10/11/22 take a map scene */
    int rotation; /* 0-3 */
};

/** Grown per tile; most tiles have none, a few have several. */
struct RSCache_WorldMapDecorList
{
    struct RSCache_WorldMapDecor* items;
    int count;
    int capacity;
};

struct RSCache_WorldMapGeography
{
    int planes; /* 1..4, from the record */

    /* Floor ids are stored +1 by the cache: 0 means "no floor here". */
    uint16_t underlay[RSCACHE_WORLDMAP_TILE_AREA];
    uint16_t overlay[RSCACHE_WORLDMAP_MAX_PLANES][RSCACHE_WORLDMAP_TILE_AREA];
    uint8_t overlay_shape[RSCACHE_WORLDMAP_MAX_PLANES][RSCACHE_WORLDMAP_TILE_AREA];
    uint8_t overlay_rotation[RSCACHE_WORLDMAP_MAX_PLANES][RSCACHE_WORLDMAP_TILE_AREA];
    struct RSCache_WorldMapDecorList decor[RSCACHE_WORLDMAP_MAX_PLANES]
                                          [RSCACHE_WORLDMAP_TILE_AREA];
};

/** (x << 6) | y, the order the tiles are written in. */
static inline int
RSCache_WorldMapTileIndex(int tile_x, int tile_y)
{
    return ((tile_x & 63) << 6) | (tile_y & 63);
}

/**
 * Decode a table-18 file into `out`, which the caller zeroes once and may pass
 * repeatedly to assemble a region from chunk records. `expect_region_x/y` (and
 * `expect_chunk_x/y` for kind 1, else -1) are the record's own coords: the file
 * repeats them, and a mismatch means the compositemap pointed at the wrong file,
 * so the decode is refused rather than written into the wrong tiles.
 *
 * `kind` < 0 means the OSRS >= 238 layout: a whole region with **no header at
 * all**, tiles from byte 0. That release moved the addressing into the group id
 * — table 18 is indexed by (region_x << 8) | region_y — and dropped both the
 * marker and the region coords the older files repeat, along with the
 * compositemap's group/file pair. `expect_*` are then unused.
 *
 * Returns false on a marker/coord mismatch or a truncated file. Whatever was
 * written before the failure stays — partial tiles draw as background, which is
 * what a missing file does anyway.
 */
bool
RSCache_WorldMapGeographyDecodeInplace(
    struct RSCache_WorldMapGeography* out,
    const void* data,
    int data_size,
    int kind,
    int planes,
    int expect_region_x,
    int expect_region_y,
    int expect_chunk_x,
    int expect_chunk_y);

void
RSCache_WorldMapGeographyFreeInplace(struct RSCache_WorldMapGeography* geography);

#endif
