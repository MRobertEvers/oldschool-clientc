#include "dat2_worldmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
RSCache_WorldMapFlags(const struct RSCache* cache)
{
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_WORLDMAP, 238, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        return RSCACHE_WORLDMAP_DECODE_REV238_NO_GROUP_FILE;
    return 0;
}

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
    entry->unknown_int = RSCache_BufferG4(&buffer); /* unused by the client; kept to re-encode */
    entry->background_colour = RSCache_BufferG4(&buffer);
    entry->unknown_byte = RSCache_BufferG1(&buffer);
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
 * own geometry, then the icon list. Each record's leading byte repeats its kind
 * (0 for the region block, 1 for the chunk block), which is what the reference
 * validates the stream against.
 *
 * OSRS >= 238 drops the trailing groupId/fileId BigSmart pair from each record.
 */
static void
rscache_worldmap_read_data0(
    struct RSCache_Buffer* buffer,
    bool has_group_file_ids,
    struct RSCache_WorldMapRegion* out)
{
    memset(out, 0, sizeof(*out));
    out->kind = RSCache_BufferG1(buffer);
    out->min_plane = RSCache_BufferG1(buffer);
    out->planes = RSCache_BufferG1(buffer);
    out->src_region_x = RSCache_BufferG2(buffer);
    out->src_region_y = RSCache_BufferG2(buffer);
    out->dst_region_x = RSCache_BufferG2(buffer);
    out->dst_region_y = RSCache_BufferG2(buffer);
    out->group_id = has_group_file_ids ? RSCache_BufferReadBigSmart(buffer) : -1;
    out->file_id = has_group_file_ids ? RSCache_BufferReadBigSmart(buffer) : -1;
}

static void
rscache_worldmap_read_data1(
    struct RSCache_Buffer* buffer,
    bool has_group_file_ids,
    struct RSCache_WorldMapRegion* out)
{
    memset(out, 0, sizeof(*out));
    out->kind = RSCache_BufferG1(buffer);
    out->min_plane = RSCache_BufferG1(buffer);
    out->planes = RSCache_BufferG1(buffer);
    out->src_region_x = RSCache_BufferG2(buffer);
    out->src_region_y = RSCache_BufferG2(buffer);
    out->src_chunk_x = RSCache_BufferG1(buffer);
    out->src_chunk_y = RSCache_BufferG1(buffer);
    out->dst_region_x = RSCache_BufferG2(buffer);
    out->dst_region_y = RSCache_BufferG2(buffer);
    out->dst_chunk_x = RSCache_BufferG1(buffer);
    out->dst_chunk_y = RSCache_BufferG1(buffer);
    out->group_id = has_group_file_ids ? RSCache_BufferReadBigSmart(buffer) : -1;
    out->file_id = has_group_file_ids ? RSCache_BufferReadBigSmart(buffer) : -1;
}

void
RSCache_WorldMapAreaDecodeIconsInplace(
    struct RSCache_WorldMapArea* entry,
    const void* data,
    int data_size,
    int flags)
{
    struct RSCache_Buffer buffer;
    int count;
    int data0_count;
    int data1_count;
    bool has_group_file_ids = (flags & RSCACHE_WORLDMAP_DECODE_REV238_NO_GROUP_FILE) == 0;

    if( !entry || !data || data_size <= 0 )
        return;

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    data0_count = RSCache_BufferG2(&buffer);
    entry->regions = NULL;
    entry->region_count = 0;

    /* Both blocks land in one array: they describe the same surface at two
     * granularities, and every consumer wants "what covers this region". The
     * allocation happens before the first block is read, so the second block's
     * count has to wait — read data0 into the array, then grow for data1. */
    if( data0_count > 0 )
    {
        entry->regions = calloc((size_t)data0_count, sizeof(*entry->regions));
        if( !entry->regions )
            return;
        for( int i = 0; i < data0_count; i++ )
            rscache_worldmap_read_data0(&buffer, has_group_file_ids, &entry->regions[i]);
        entry->region_count = data0_count;
    }
    entry->data0_count = data0_count > 0 ? data0_count : 0;

    data1_count = RSCache_BufferG2(&buffer);
    if( data1_count > 0 )
    {
        struct RSCache_WorldMapRegion* grown = (struct RSCache_WorldMapRegion*)realloc(
            entry->regions, (size_t)(data0_count + data1_count) * sizeof(*entry->regions));
        if( !grown )
            return;
        entry->regions = grown;
        for( int i = 0; i < data1_count; i++ )
            rscache_worldmap_read_data1(
                &buffer, has_group_file_ids, &entry->regions[data0_count + i]);
        entry->region_count = data0_count + data1_count;
    }

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
    free(entry->regions);
    free(entry->sections);
    free(entry->icons);
    memset(entry, 0, sizeof(*entry));
}

/* ---- encode -------------------------------------------------------------- */

static bool
rscache_worldmap_section_encode(
    struct RSCache_Buffer* buffer,
    const struct RSCache_WorldMapSection* section)
{
    RSCache_BufferP1(buffer, section->type);
    switch( section->type )
    {
    case RSCACHE_WORLDMAP_SECTION_REGION_RANGE:
        RSCache_BufferP1(buffer, section->min_plane);
        RSCache_BufferP1(buffer, section->planes);
        RSCache_BufferP2(buffer, section->src_region_x);
        RSCache_BufferP2(buffer, section->src_region_y);
        RSCache_BufferP2(buffer, section->src_region_x_end);
        RSCache_BufferP2(buffer, section->src_region_y_end);
        RSCache_BufferP2(buffer, section->dst_region_x);
        RSCache_BufferP2(buffer, section->dst_region_y);
        RSCache_BufferP2(buffer, section->dst_region_x_end);
        RSCache_BufferP2(buffer, section->dst_region_y_end);
        return true;
    case RSCACHE_WORLDMAP_SECTION_REGION:
        RSCache_BufferP1(buffer, section->min_plane);
        RSCache_BufferP1(buffer, section->planes);
        RSCache_BufferP2(buffer, section->src_region_x);
        RSCache_BufferP2(buffer, section->src_region_y);
        RSCache_BufferP2(buffer, section->dst_region_x);
        RSCache_BufferP2(buffer, section->dst_region_y);
        return true;
    case RSCACHE_WORLDMAP_SECTION_CHUNK_RANGE:
        RSCache_BufferP1(buffer, section->min_plane);
        RSCache_BufferP1(buffer, section->planes);
        RSCache_BufferP2(buffer, section->src_region_x);
        RSCache_BufferP1(buffer, section->src_chunk_x_low);
        RSCache_BufferP1(buffer, section->src_chunk_x_high);
        RSCache_BufferP2(buffer, section->src_region_y);
        RSCache_BufferP1(buffer, section->src_chunk_y_low);
        RSCache_BufferP1(buffer, section->src_chunk_y_high);
        RSCache_BufferP2(buffer, section->dst_region_x);
        RSCache_BufferP1(buffer, section->dst_chunk_x_low);
        RSCache_BufferP1(buffer, section->dst_chunk_x_high);
        RSCache_BufferP2(buffer, section->dst_region_y);
        RSCache_BufferP1(buffer, section->dst_chunk_y_low);
        RSCache_BufferP1(buffer, section->dst_chunk_y_high);
        return true;
    case RSCACHE_WORLDMAP_SECTION_CHUNK:
        RSCache_BufferP1(buffer, section->min_plane);
        RSCache_BufferP1(buffer, section->planes);
        RSCache_BufferP2(buffer, section->src_region_x);
        RSCache_BufferP1(buffer, section->src_chunk_x_low);
        RSCache_BufferP2(buffer, section->src_region_y);
        RSCache_BufferP1(buffer, section->src_chunk_y_low);
        RSCache_BufferP2(buffer, section->dst_region_x);
        RSCache_BufferP1(buffer, section->dst_chunk_x_low);
        RSCache_BufferP2(buffer, section->dst_region_y);
        RSCache_BufferP1(buffer, section->dst_chunk_y_low);
        return true;
    default:
        return false;
    }
}

uint32_t
RSCache_WorldMapAreaEncodeBound(const struct RSCache_WorldMapArea* entry)
{
    if( !entry )
        return 0;
    uint32_t bound = 32;
    bound += (uint32_t)(entry->internal_name ? strlen(entry->internal_name) : 0) + 1;
    bound += (uint32_t)(entry->external_name ? strlen(entry->external_name) : 0) + 1;
    /* The widest section is CHUNK_RANGE at 21 bytes including its type byte. */
    bound += (uint32_t)entry->section_count * 24u;
    return bound;
}

uint32_t
RSCache_WorldMapAreaEncode(
    const struct RSCache_WorldMapArea* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out || out_capacity < RSCache_WorldMapAreaEncodeBound(entry) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    RSCache_BufferPjstr(
        &buffer, entry->internal_name ? entry->internal_name : "", RSCACHE_JSTR_TERMINATOR_NULL);
    RSCache_BufferPjstr(
        &buffer, entry->external_name ? entry->external_name : "", RSCACHE_JSTR_TERMINATOR_NULL);
    RSCache_BufferP4(&buffer, entry->origin);
    RSCache_BufferP4(&buffer, entry->unknown_int);
    RSCache_BufferP4(&buffer, entry->background_colour);
    RSCache_BufferP1(&buffer, entry->unknown_byte);
    RSCache_BufferP1(&buffer, entry->is_main ? 1 : 0);
    RSCache_BufferP1(&buffer, entry->zoom);

    RSCache_BufferP1(&buffer, entry->section_count);
    for( int i = 0; i < entry->section_count; i++ )
    {
        if( !rscache_worldmap_section_encode(&buffer, &entry->sections[i]) )
            return 0;
    }
    return buffer.position;
}

uint32_t
RSCache_WorldMapAreaEncodeIconsBound(const struct RSCache_WorldMapArea* entry)
{
    if( !entry )
        return 0;
    /* Per region: 15 fixed bytes plus two BigSmarts at 4 each. Per icon: a
     * BigSmart, a coord and a flag. */
    return 8u + (uint32_t)entry->region_count * 24u + (uint32_t)entry->icon_count * 10u;
}

uint32_t
RSCache_WorldMapAreaEncodeIcons(
    const struct RSCache_WorldMapArea* entry,
    int flags,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out || out_capacity < RSCache_WorldMapAreaEncodeIconsBound(entry) )
        return 0;
    bool has_group_file_ids = (flags & RSCACHE_WORLDMAP_DECODE_REV238_NO_GROUP_FILE) == 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    int data0_count = entry->data0_count;
    if( data0_count > entry->region_count )
        data0_count = entry->region_count;
    int data1_count = entry->region_count - data0_count;

    RSCache_BufferP2(&buffer, data0_count);
    for( int i = 0; i < data0_count; i++ )
    {
        const struct RSCache_WorldMapRegion* region = &entry->regions[i];
        RSCache_BufferP1(&buffer, region->kind);
        RSCache_BufferP1(&buffer, region->min_plane);
        RSCache_BufferP1(&buffer, region->planes);
        RSCache_BufferP2(&buffer, region->src_region_x);
        RSCache_BufferP2(&buffer, region->src_region_y);
        RSCache_BufferP2(&buffer, region->dst_region_x);
        RSCache_BufferP2(&buffer, region->dst_region_y);
        if( has_group_file_ids )
        {
            RSCache_BufferWriteBigSmart(&buffer, region->group_id);
            RSCache_BufferWriteBigSmart(&buffer, region->file_id);
        }
    }

    RSCache_BufferP2(&buffer, data1_count);
    for( int i = 0; i < data1_count; i++ )
    {
        const struct RSCache_WorldMapRegion* region = &entry->regions[data0_count + i];
        RSCache_BufferP1(&buffer, region->kind);
        RSCache_BufferP1(&buffer, region->min_plane);
        RSCache_BufferP1(&buffer, region->planes);
        RSCache_BufferP2(&buffer, region->src_region_x);
        RSCache_BufferP2(&buffer, region->src_region_y);
        RSCache_BufferP1(&buffer, region->src_chunk_x);
        RSCache_BufferP1(&buffer, region->src_chunk_y);
        RSCache_BufferP2(&buffer, region->dst_region_x);
        RSCache_BufferP2(&buffer, region->dst_region_y);
        RSCache_BufferP1(&buffer, region->dst_chunk_x);
        RSCache_BufferP1(&buffer, region->dst_chunk_y);
        if( has_group_file_ids )
        {
            RSCache_BufferWriteBigSmart(&buffer, region->group_id);
            RSCache_BufferWriteBigSmart(&buffer, region->file_id);
        }
    }

    RSCache_BufferP2(&buffer, entry->icon_count);
    for( int i = 0; i < entry->icon_count; i++ )
    {
        RSCache_BufferWriteBigSmart(&buffer, entry->icons[i].element);
        RSCache_BufferP4(&buffer, entry->icons[i].coord);
        RSCache_BufferP1(&buffer, entry->icons[i].hidden ? 1 : 0);
    }
    return buffer.position;
}
