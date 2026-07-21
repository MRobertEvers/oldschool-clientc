#ifndef RSCACHE_DATATYPES_DAT2_WORLDMAP_H
#define RSCACHE_DATATYPES_DAT2_WORLDMAP_H

#include "../rsbuffer.h"

#include <stdbool.h>

/*
 * World map areas — the "details" and "compositemap" archives of the worldmap
 * table (dat2 table 19). An area is a list of sections, each blitting a source
 * rectangle of the real world onto a destination rectangle of the map surface.
 * The four section types differ only in which fields the cache stores; the
 * decode fills the ones its type carries and leaves the rest zero.
 */

enum RSCache_WorldMapSectionType
{
    /* Rectangle of regions -> rectangle of regions. */
    RSCACHE_WORLDMAP_SECTION_REGION_RANGE = 0,
    /* One region -> one region. */
    RSCACHE_WORLDMAP_SECTION_REGION = 1,
    /* Chunk range within one region, on both sides. */
    RSCACHE_WORLDMAP_SECTION_CHUNK_RANGE = 2,
    /* One chunk -> one chunk. */
    RSCACHE_WORLDMAP_SECTION_CHUNK = 3,
};

struct RSCache_WorldMapSection
{
    int type;
    int min_plane;
    int planes;

    /* Source (real world) side. */
    int src_region_x;
    int src_region_y;
    int src_region_x_end; /* REGION_RANGE only */
    int src_region_y_end; /* REGION_RANGE only */
    int src_chunk_x_low;  /* CHUNK_RANGE, CHUNK */
    int src_chunk_y_low;  /* CHUNK_RANGE, CHUNK */
    int src_chunk_x_high; /* CHUNK_RANGE only */
    int src_chunk_y_high; /* CHUNK_RANGE only */

    /* Destination (map surface) side. */
    int dst_region_x;
    int dst_region_y;
    int dst_region_x_end; /* REGION_RANGE only */
    int dst_region_y_end; /* REGION_RANGE only */
    int dst_chunk_x_low;  /* CHUNK_RANGE, CHUNK */
    int dst_chunk_y_low;  /* CHUNK_RANGE, CHUNK */
    int dst_chunk_x_high; /* CHUNK_RANGE only */
    int dst_chunk_y_high; /* CHUNK_RANGE only */
};

/** A map element placement, from the "compositemap" archive. */
struct RSCache_WorldMapIcon
{
    int element; /* map element config id */
    int coord;   /* packed source coord: plane<<28 | x<<14 | y */
    bool hidden;
};

struct RSCache_WorldMapArea
{
    int id;
    char* internal_name;
    char* external_name;
    int origin; /* packed coord */
    int background_colour;
    bool is_main;
    int zoom;

    struct RSCache_WorldMapSection* sections;
    int section_count;

    struct RSCache_WorldMapIcon* icons;
    int icon_count;
};

/**
 * Decode one file of the "details" archive. Returns false (entry left safe to
 * free) when the file carries a section type this decoder does not know, since
 * that desynchronises everything after it.
 */
bool
RSCache_WorldMapAreaDecodeInplace(
    struct RSCache_WorldMapArea* entry,
    int area_id,
    const void* data,
    int data_size);

/**
 * Attach the icons from the matching file of the "compositemap" archive. The
 * two archives list the same file ids, so the caller pairs them by id.
 */
void
RSCache_WorldMapAreaDecodeIconsInplace(
    struct RSCache_WorldMapArea* entry,
    const void* data,
    int data_size);

void
RSCache_WorldMapAreaFreeInplace(struct RSCache_WorldMapArea* entry);

#endif
