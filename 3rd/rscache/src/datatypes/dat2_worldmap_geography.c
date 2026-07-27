#include "dat2_worldmap_geography.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    WORLDMAP_GEOGRAPHY_MARKER_REGION = 0,
    WORLDMAP_GEOGRAPHY_MARKER_CHUNK = 1,
};

static bool
worldmap_decor_add(
    struct RSCache_WorldMapDecorList* list,
    int loc_id,
    int shape,
    int rotation)
{
    if( list->count == list->capacity )
    {
        int capacity = list->capacity ? list->capacity * 2 : 2;
        struct RSCache_WorldMapDecor* grown =
            (struct RSCache_WorldMapDecor*)realloc(list->items, (size_t)capacity * sizeof(*grown));
        if( !grown )
            return false;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count].loc_id = loc_id;
    list->items[list->count].shape = shape;
    list->items[list->count].rotation = rotation;
    list->count++;
    return true;
}

/*
 * A tile is one flags byte then, depending on bit 0, a short form (an underlay
 * and optionally one overlay) or a long form (underlay, an overlay per plane,
 * and a loc list per plane). flags == 0 is the common case: nothing here.
 */
static bool
worldmap_read_tile(
    struct RSCache_WorldMapGeography* out,
    int tile_x,
    int tile_y,
    struct RSCache_Buffer* buffer)
{
    int index = RSCache_WorldMapTileIndex(tile_x, tile_y);
    int flags;

    if( RSCache_BufferRemaining(buffer) < 1 )
        return false;

    flags = RSCache_BufferG1(buffer);
    if( flags == 0 )
        return true;

    if( (flags & 1) != 0 )
    {
        if( (flags & 2) != 0 )
            out->overlay[0][index] = (uint16_t)RSCache_BufferG2(buffer);
        out->underlay[index] = (uint16_t)RSCache_BufferG2(buffer);
        return true;
    }

    /* Long form. Bits 3-4 carry "planes of locs to read" minus one — a count of
     * what the *file* holds, which can exceed the record's plane count; the
     * extra planes are read and dropped so the stream stays aligned. */
    int loc_planes = ((flags & 24) >> 3) + 1;
    bool has_overlays = (flags & 2) != 0;
    bool has_locs = (flags & 4) != 0;

    out->underlay[index] = (uint16_t)RSCache_BufferG2(buffer);

    if( has_overlays )
    {
        int overlay_count = RSCache_BufferG1(buffer);
        for( int plane = 0; plane < overlay_count; plane++ )
        {
            int overlay_id = RSCache_BufferG2(buffer);
            if( overlay_id == 0 )
                continue;
            int overlay_info = RSCache_BufferG1(buffer);
            if( plane < out->planes && plane < RSCACHE_WORLDMAP_MAX_PLANES )
            {
                out->overlay[plane][index] = (uint16_t)overlay_id;
                out->overlay_shape[plane][index] = (uint8_t)(overlay_info >> 2);
                out->overlay_rotation[plane][index] = (uint8_t)(overlay_info & 3);
            }
        }
    }

    if( has_locs )
    {
        for( int plane = 0; plane < loc_planes; plane++ )
        {
            int loc_count = RSCache_BufferG1(buffer);
            for( int i = 0; i < loc_count; i++ )
            {
                int loc_id = RSCache_BufferReadBigSmart(buffer);
                int info = RSCache_BufferG1(buffer);
                if( plane >= out->planes || plane >= RSCACHE_WORLDMAP_MAX_PLANES )
                    continue;
                if( !worldmap_decor_add(
                        &out->decor[plane][index], loc_id, info >> 2, info & 3) )
                    return false;
            }
        }
    }

    return true;
}

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
    int expect_chunk_y)
{
    struct RSCache_Buffer buffer;
    int marker;

    if( !out || !data || data_size <= 0 )
        return false;

    if( planes < 1 )
        planes = 1;
    if( planes > RSCACHE_WORLDMAP_MAX_PLANES )
        planes = RSCACHE_WORLDMAP_MAX_PLANES;
    /* Chunk records assemble into one struct, so the plane count is the widest
     * any contributing record asked for. */
    if( planes > out->planes )
        out->planes = planes;

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    if( kind < 0 )
    {
        /* Headerless (OSRS >= 238): straight into the region's tiles. */
        for( int tile_x = 0; tile_x < RSCACHE_WORLDMAP_TILE_COUNT; tile_x++ )
        {
            for( int tile_y = 0; tile_y < RSCACHE_WORLDMAP_TILE_COUNT; tile_y++ )
            {
                if( !worldmap_read_tile(out, tile_x, tile_y, &buffer) )
                    return false;
            }
        }
        /* Exact consumption. The older files are framed by a marker and their
         * own region coords, so a misread shows up immediately; a headerless
         * file has no such check, and a tile layout this decoder reads wrongly
         * would otherwise surface as plausible-looking floor ids drawn in the
         * wrong places. Landing exactly on the end is the evidence that every
         * tile was read the way the file wrote it. */
        if( RSCache_BufferRemaining(&buffer) != 0 )
        {
            printf(
                "RSCache_WorldMapGeographyDecodeInplace: headerless file left %u bytes\n",
                RSCache_BufferRemaining(&buffer));
            return false;
        }
        return true;
    }

    marker = RSCache_BufferG1(&buffer);
    if( marker != kind )
    {
        printf(
            "RSCache_WorldMapGeographyDecodeInplace: marker %d, expected %d\n", marker, kind);
        return false;
    }

    int region_x = RSCache_BufferG1(&buffer);
    int region_y = RSCache_BufferG1(&buffer);
    if( expect_region_x >= 0 &&
        (region_x != (expect_region_x & 0xFF) || region_y != (expect_region_y & 0xFF)) )
    {
        printf(
            "RSCache_WorldMapGeographyDecodeInplace: region %d,%d, expected %d,%d\n",
            region_x,
            region_y,
            expect_region_x,
            expect_region_y);
        return false;
    }

    if( kind == WORLDMAP_GEOGRAPHY_MARKER_REGION )
    {
        for( int tile_x = 0; tile_x < RSCACHE_WORLDMAP_TILE_COUNT; tile_x++ )
        {
            for( int tile_y = 0; tile_y < RSCACHE_WORLDMAP_TILE_COUNT; tile_y++ )
            {
                if( !worldmap_read_tile(out, tile_x, tile_y, &buffer) )
                    return false;
            }
        }
        return true;
    }

    if( kind != WORLDMAP_GEOGRAPHY_MARKER_CHUNK )
        return false;

    int chunk_x = RSCache_BufferG1(&buffer);
    int chunk_y = RSCache_BufferG1(&buffer);
    if( expect_chunk_x >= 0 && (chunk_x != expect_chunk_x || chunk_y != expect_chunk_y) )
    {
        printf(
            "RSCache_WorldMapGeographyDecodeInplace: chunk %d,%d, expected %d,%d\n",
            chunk_x,
            chunk_y,
            expect_chunk_x,
            expect_chunk_y);
        return false;
    }

    /* The file names the source chunk; the caller says where it lands. */
    for( int tile_x = 0; tile_x < 8; tile_x++ )
    {
        for( int tile_y = 0; tile_y < 8; tile_y++ )
        {
            if( !worldmap_read_tile(
                    out, tile_x + (chunk_x * 8), tile_y + (chunk_y * 8), &buffer) )
                return false;
        }
    }
    return true;
}

void
RSCache_WorldMapGeographyFreeInplace(struct RSCache_WorldMapGeography* geography)
{
    if( !geography )
        return;
    for( int plane = 0; plane < RSCACHE_WORLDMAP_MAX_PLANES; plane++ )
    {
        for( int i = 0; i < RSCACHE_WORLDMAP_TILE_AREA; i++ )
            free(geography->decor[plane][i].items);
    }
    memset(geography, 0, sizeof(*geography));
}
