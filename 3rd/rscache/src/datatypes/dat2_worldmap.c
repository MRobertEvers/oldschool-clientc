#include "dat2_worldmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
rscache_worldmap_section_decode(
    struct RSCache_Buffer* buffer,
    struct RSCache_WorldMapSection* out)
{
    int type = RSCache_BufferG1(buffer);

    memset(out, 0, sizeof(*out));
    out->type = type;

    switch( type )
    {
    case RSCACHE_WORLDMAP_SECTION_REGION_RANGE:
        out->min_plane = RSCache_BufferG1(buffer);
        out->planes = RSCache_BufferG1(buffer);
        out->src_region_x = RSCache_BufferG2(buffer);
        out->src_region_y = RSCache_BufferG2(buffer);
        out->src_region_x_end = RSCache_BufferG2(buffer);
        out->src_region_y_end = RSCache_BufferG2(buffer);
        out->dst_region_x = RSCache_BufferG2(buffer);
        out->dst_region_y = RSCache_BufferG2(buffer);
        out->dst_region_x_end = RSCache_BufferG2(buffer);
        out->dst_region_y_end = RSCache_BufferG2(buffer);
        return true;

    case RSCACHE_WORLDMAP_SECTION_REGION:
        out->min_plane = RSCache_BufferG1(buffer);
        out->planes = RSCache_BufferG1(buffer);
        out->src_region_x = RSCache_BufferG2(buffer);
        out->src_region_y = RSCache_BufferG2(buffer);
        out->dst_region_x = RSCache_BufferG2(buffer);
        out->dst_region_y = RSCache_BufferG2(buffer);
        return true;

    case RSCACHE_WORLDMAP_SECTION_CHUNK_RANGE:
        out->min_plane = RSCache_BufferG1(buffer);
        out->planes = RSCache_BufferG1(buffer);
        out->src_region_x = RSCache_BufferG2(buffer);
        out->src_chunk_x_low = RSCache_BufferG1(buffer);
        out->src_chunk_x_high = RSCache_BufferG1(buffer);
        out->src_region_y = RSCache_BufferG2(buffer);
        out->src_chunk_y_low = RSCache_BufferG1(buffer);
        out->src_chunk_y_high = RSCache_BufferG1(buffer);
        out->dst_region_x = RSCache_BufferG2(buffer);
        out->dst_chunk_x_low = RSCache_BufferG1(buffer);
        out->dst_chunk_x_high = RSCache_BufferG1(buffer);
        out->dst_region_y = RSCache_BufferG2(buffer);
        out->dst_chunk_y_low = RSCache_BufferG1(buffer);
        out->dst_chunk_y_high = RSCache_BufferG1(buffer);
        return true;

    case RSCACHE_WORLDMAP_SECTION_CHUNK:
        out->min_plane = RSCache_BufferG1(buffer);
        out->planes = RSCache_BufferG1(buffer);
        out->src_region_x = RSCache_BufferG2(buffer);
        out->src_chunk_x_low = RSCache_BufferG1(buffer);
        out->src_region_y = RSCache_BufferG2(buffer);
        out->src_chunk_y_low = RSCache_BufferG1(buffer);
        out->dst_region_x = RSCache_BufferG2(buffer);
        out->dst_chunk_x_low = RSCache_BufferG1(buffer);
        out->dst_region_y = RSCache_BufferG2(buffer);
        out->dst_chunk_y_low = RSCache_BufferG1(buffer);
        return true;

    default:
        printf("RSCache_WorldMapAreaDecodeInplace: unknown section type %d\n", type);
        return false;
    }
}

bool
RSCache_WorldMapAreaDecodeInplace(
    struct RSCache_WorldMapArea* entry,
    int area_id,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buffer;
    int section_count;

    if( !entry || !data || data_size <= 0 )
        return false;

    memset(entry, 0, sizeof(*entry));
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    entry->id = area_id;
    entry->internal_name = RSCache_BufferReadStringNullTerminated(&buffer);
    entry->external_name = RSCache_BufferReadStringNullTerminated(&buffer);
    entry->origin = RSCache_BufferG4(&buffer);
    (void)RSCache_BufferG4(&buffer); /* unused int */
    entry->background_colour = RSCache_BufferG4(&buffer);
    (void)RSCache_BufferG1(&buffer);
    entry->is_main = RSCache_BufferG1(&buffer) == 1;
    entry->zoom = RSCache_BufferG1(&buffer);

    section_count = RSCache_BufferG1(&buffer);
    if( section_count <= 0 )
        return true;

    entry->sections = calloc((size_t)section_count, sizeof(*entry->sections));
    if( !entry->sections )
        return false;

    for( int i = 0; i < section_count; i++ )
    {
        if( !rscache_worldmap_section_decode(&buffer, &entry->sections[i]) )
            return false;
        entry->section_count++;
    }
    return true;
}

/*
 * A compositemap file starts with two blocks of records describing the map's
 * own geometry, then the icon list. Nothing here uses the first two blocks, but
 * skipping them exactly is what keeps the icon offsets right.
 */
static void
rscache_worldmap_skip_data0(struct RSCache_Buffer* buffer)
{
    RSCache_BufferG1(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferReadBigSmart(buffer);
    RSCache_BufferReadBigSmart(buffer);
}

static void
rscache_worldmap_skip_data1(struct RSCache_Buffer* buffer)
{
    RSCache_BufferG1(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferG2(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferG1(buffer);
    RSCache_BufferReadBigSmart(buffer);
    RSCache_BufferReadBigSmart(buffer);
}

void
RSCache_WorldMapAreaDecodeIconsInplace(
    struct RSCache_WorldMapArea* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buffer;
    int count;

    if( !entry || !data || data_size <= 0 )
        return;

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    count = RSCache_BufferG2(&buffer);
    for( int i = 0; i < count; i++ )
        rscache_worldmap_skip_data0(&buffer);

    count = RSCache_BufferG2(&buffer);
    for( int i = 0; i < count; i++ )
        rscache_worldmap_skip_data1(&buffer);

    count = RSCache_BufferG2(&buffer);
    if( count <= 0 )
        return;

    entry->icons = calloc((size_t)count, sizeof(*entry->icons));
    if( !entry->icons )
        return;

    for( int i = 0; i < count; i++ )
    {
        entry->icons[i].element = RSCache_BufferReadBigSmart(&buffer);
        entry->icons[i].coord = RSCache_BufferG4(&buffer);
        entry->icons[i].hidden = RSCache_BufferG1(&buffer) == 1;
    }
    entry->icon_count = count;
}

void
RSCache_WorldMapAreaFreeInplace(struct RSCache_WorldMapArea* entry)
{
    if( !entry )
        return;
    free(entry->internal_name);
    free(entry->external_name);
    free(entry->sections);
    free(entry->icons);
    memset(entry, 0, sizeof(*entry));
}
