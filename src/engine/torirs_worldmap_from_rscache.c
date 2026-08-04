#include "engine/torirs_worldmap_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdlib.h>
#include <string.h>

#define WORLDMAP_REGION_TILES 64
#define WORLDMAP_CHUNK_TILES 8
#define WORLDMAP_CHUNKS_PER_REGION 8

int
ToriRS_WorldMapPackCoord(
    int plane,
    int x,
    int y)
{
    return ((plane & 0x3) << 28) | ((x & 0x3fff) << 14) | (y & 0x3fff);
}

void
ToriRS_WorldMapUnpackCoord(
    int packed,
    int* plane,
    int* x,
    int* y)
{
    unsigned int packed_u = (unsigned int)packed;

    if( plane )
        *plane = (int)((packed_u >> 28) & 0x3);
    if( x )
        *x = (int)((packed_u >> 14) & 0x3fff);
    if( y )
        *y = (int)(packed_u & 0x3fff);
}

/* Every section is a source tile rectangle blitted to a display tile rectangle;
 * only the cache encoding differs per type. Conversion normalises whole-region
 * types to chunk 0..7 so the geometry below is written once. */
static int
section_src_x_low(struct ToriRS_WorldMapSection const* section)
{
    return (section->src_region_x * WORLDMAP_REGION_TILES) +
           (section->src_chunk_x * WORLDMAP_CHUNK_TILES);
}

static int
section_src_y_low(struct ToriRS_WorldMapSection const* section)
{
    return (section->src_region_y * WORLDMAP_REGION_TILES) +
           (section->src_chunk_y * WORLDMAP_CHUNK_TILES);
}

static int
section_dst_x_low(struct ToriRS_WorldMapSection const* section)
{
    return (section->dst_region_x * WORLDMAP_REGION_TILES) +
           (section->dst_chunk_x * WORLDMAP_CHUNK_TILES);
}

static int
section_dst_y_low(struct ToriRS_WorldMapSection const* section)
{
    return (section->dst_region_y * WORLDMAP_REGION_TILES) +
           (section->dst_chunk_y * WORLDMAP_CHUNK_TILES);
}

static int
section_src_x_high(struct ToriRS_WorldMapSection const* section)
{
    return (section->src_region_x_end * WORLDMAP_REGION_TILES) +
           (section->src_chunk_x_end * WORLDMAP_CHUNK_TILES) + WORLDMAP_CHUNK_TILES - 1;
}

static int
section_src_y_high(struct ToriRS_WorldMapSection const* section)
{
    return (section->src_region_y_end * WORLDMAP_REGION_TILES) +
           (section->src_chunk_y_end * WORLDMAP_CHUNK_TILES) + WORLDMAP_CHUNK_TILES - 1;
}

static int
section_dst_x_high(struct ToriRS_WorldMapSection const* section)
{
    return (section->dst_region_x_end * WORLDMAP_REGION_TILES) +
           (section->dst_chunk_x_end * WORLDMAP_CHUNK_TILES) + WORLDMAP_CHUNK_TILES - 1;
}

static int
section_dst_y_high(struct ToriRS_WorldMapSection const* section)
{
    return (section->dst_region_y_end * WORLDMAP_REGION_TILES) +
           (section->dst_chunk_y_end * WORLDMAP_CHUNK_TILES) + WORLDMAP_CHUNK_TILES - 1;
}

static bool
section_contains_position(
    struct ToriRS_WorldMapSection const* section,
    int x,
    int y)
{
    return x >= section_dst_x_low(section) && x <= section_dst_x_high(section) &&
           y >= section_dst_y_low(section) && y <= section_dst_y_high(section);
}

static bool
section_contains_coord(
    struct ToriRS_WorldMapSection const* section,
    int plane,
    int x,
    int y)
{
    if( plane < section->min_plane || plane >= section->min_plane + section->planes )
        return false;
    return x >= section_src_x_low(section) && x <= section_src_x_high(section) &&
           y >= section_src_y_low(section) && y <= section_src_y_high(section);
}

bool
ToriRS_WorldMapArea_ContainsPosition(
    struct ToriRS_WorldMapArea const* area,
    int x,
    int y)
{
    if( !area )
        return false;
    for( int i = 0; i < area->section_count; i++ )
    {
        if( section_contains_position(&area->sections[i], x, y) )
            return true;
    }
    return false;
}

bool
ToriRS_WorldMapArea_ContainsCoord(
    struct ToriRS_WorldMapArea const* area,
    int plane,
    int x,
    int y)
{
    if( !area )
        return false;
    for( int i = 0; i < area->section_count; i++ )
    {
        if( section_contains_coord(&area->sections[i], plane, x, y) )
            return true;
    }
    return false;
}

bool
ToriRS_WorldMapArea_Coord(
    struct ToriRS_WorldMapArea const* area,
    int x,
    int y,
    int* out_plane,
    int* out_x,
    int* out_y)
{
    if( !area )
        return false;
    for( int i = 0; i < area->section_count; i++ )
    {
        struct ToriRS_WorldMapSection const* section = &area->sections[i];
        if( !section_contains_position(section, x, y) )
            continue;
        if( out_plane )
            *out_plane = section->min_plane;
        if( out_x )
            *out_x = section_src_x_low(section) - section_dst_x_low(section) + x;
        if( out_y )
            *out_y = section_src_y_low(section) - section_dst_y_low(section) + y;
        return true;
    }
    return false;
}

bool
ToriRS_WorldMapArea_Position(
    struct ToriRS_WorldMapArea const* area,
    int plane,
    int x,
    int y,
    int* out_x,
    int* out_y)
{
    if( !area )
        return false;
    for( int i = 0; i < area->section_count; i++ )
    {
        struct ToriRS_WorldMapSection const* section = &area->sections[i];
        if( !section_contains_coord(section, plane, x, y) )
            continue;
        if( out_x )
            *out_x = section_dst_x_low(section) - section_src_x_low(section) + x;
        if( out_y )
            *out_y = section_dst_y_low(section) - section_src_y_low(section) + y;
        return true;
    }
    return false;
}

int
ToriRS_WorldMapArea_WidthTiles(struct ToriRS_WorldMapArea const* area)
{
    if( !area )
        return 0;
    return (area->region_high_x - area->region_low_x + 1) * WORLDMAP_REGION_TILES;
}

int
ToriRS_WorldMapArea_HeightTiles(struct ToriRS_WorldMapArea const* area)
{
    if( !area )
        return 0;
    return (area->region_high_y - area->region_low_y + 1) * WORLDMAP_REGION_TILES;
}

void
ToriRS_WorldMapArea_Bounds(
    struct ToriRS_WorldMapArea const* area,
    int* min_x,
    int* min_y,
    int* max_x,
    int* max_y)
{
    if( !area )
        return;
    if( min_x )
        *min_x = area->region_low_x * WORLDMAP_REGION_TILES;
    if( min_y )
        *min_y = area->region_low_y * WORLDMAP_REGION_TILES;
    if( max_x )
        *max_x = (area->region_high_x * WORLDMAP_REGION_TILES) + WORLDMAP_REGION_TILES - 1;
    if( max_y )
        *max_y = (area->region_high_y * WORLDMAP_REGION_TILES) + WORLDMAP_REGION_TILES - 1;
}

/* Region-granular section types cover whole regions, i.e. chunks 0..7 on both
 * axes; spelling that out here is what lets one rectangle test serve all four
 * cache types. */
static void
section_from_rscache(
    struct ToriRS_WorldMapSection* out,
    struct RSCache_WorldMapSection const* src)
{
    memset(out, 0, sizeof(*out));
    out->min_plane = src->min_plane;
    out->planes = src->planes;
    out->src_region_x = src->src_region_x;
    out->src_region_y = src->src_region_y;
    out->dst_region_x = src->dst_region_x;
    out->dst_region_y = src->dst_region_y;

    switch( src->type )
    {
    case RSCACHE_WORLDMAP_SECTION_REGION_RANGE:
        out->kind = TORIRS_WORLDMAP_SECTION_REGION_RANGE;
        out->src_region_x_end = src->src_region_x_end;
        out->src_region_y_end = src->src_region_y_end;
        out->dst_region_x_end = src->dst_region_x_end;
        out->dst_region_y_end = src->dst_region_y_end;
        out->src_chunk_x_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        out->src_chunk_y_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        out->dst_chunk_x_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        out->dst_chunk_y_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        break;

    case RSCACHE_WORLDMAP_SECTION_REGION:
        out->kind = TORIRS_WORLDMAP_SECTION_REGION;
        out->src_region_x_end = src->src_region_x;
        out->src_region_y_end = src->src_region_y;
        out->dst_region_x_end = src->dst_region_x;
        out->dst_region_y_end = src->dst_region_y;
        out->src_chunk_x_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        out->src_chunk_y_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        out->dst_chunk_x_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        out->dst_chunk_y_end = WORLDMAP_CHUNKS_PER_REGION - 1;
        break;

    case RSCACHE_WORLDMAP_SECTION_CHUNK_RANGE:
        out->kind = TORIRS_WORLDMAP_SECTION_CHUNK_RANGE;
        out->src_region_x_end = src->src_region_x;
        out->src_region_y_end = src->src_region_y;
        out->dst_region_x_end = src->dst_region_x;
        out->dst_region_y_end = src->dst_region_y;
        out->src_chunk_x = src->src_chunk_x_low;
        out->src_chunk_y = src->src_chunk_y_low;
        out->src_chunk_x_end = src->src_chunk_x_high;
        out->src_chunk_y_end = src->src_chunk_y_high;
        out->dst_chunk_x = src->dst_chunk_x_low;
        out->dst_chunk_y = src->dst_chunk_y_low;
        out->dst_chunk_x_end = src->dst_chunk_x_high;
        out->dst_chunk_y_end = src->dst_chunk_y_high;
        break;

    case RSCACHE_WORLDMAP_SECTION_CHUNK:
    default:
        out->kind = TORIRS_WORLDMAP_SECTION_CHUNK;
        out->src_region_x_end = src->src_region_x;
        out->src_region_y_end = src->src_region_y;
        out->dst_region_x_end = src->dst_region_x;
        out->dst_region_y_end = src->dst_region_y;
        out->src_chunk_x = src->src_chunk_x_low;
        out->src_chunk_y = src->src_chunk_y_low;
        out->src_chunk_x_end = src->src_chunk_x_low;
        out->src_chunk_y_end = src->src_chunk_y_low;
        out->dst_chunk_x = src->dst_chunk_x_low;
        out->dst_chunk_y = src->dst_chunk_y_low;
        out->dst_chunk_x_end = src->dst_chunk_x_low;
        out->dst_chunk_y_end = src->dst_chunk_y_low;
        break;
    }
}

static void
area_set_bounds(struct ToriRS_WorldMapArea* area)
{
    for( int i = 0; i < area->section_count; i++ )
    {
        struct ToriRS_WorldMapSection const* section = &area->sections[i];
        if( i == 0 )
        {
            area->region_low_x = section->dst_region_x;
            area->region_high_x = section->dst_region_x_end;
            area->region_low_y = section->dst_region_y;
            area->region_high_y = section->dst_region_y_end;
            continue;
        }
        if( section->dst_region_x < area->region_low_x )
            area->region_low_x = section->dst_region_x;
        if( section->dst_region_x_end > area->region_high_x )
            area->region_high_x = section->dst_region_x_end;
        if( section->dst_region_y < area->region_low_y )
            area->region_low_y = section->dst_region_y;
        if( section->dst_region_y_end > area->region_high_y )
            area->region_high_y = section->dst_region_y_end;
    }
}

static void
area_from_rscache(
    struct ToriRS_WorldMapArea* area,
    struct RSCache_WorldMapArea const* entry)
{
    area->id = entry->id;
    area->internal_name = entry->internal_name ? strdup(entry->internal_name) : NULL;
    area->external_name = entry->external_name ? strdup(entry->external_name) : NULL;
    area->origin = entry->origin;
    area->background_colour = entry->background_colour;
    area->is_main = entry->is_main;
    area->zoom = entry->zoom;

    if( entry->section_count > 0 )
    {
        area->sections = calloc((size_t)entry->section_count, sizeof(*area->sections));
        assert(area->sections);
        for( int i = 0; i < entry->section_count; i++ )
            section_from_rscache(&area->sections[i], &entry->sections[i]);
        area->section_count = entry->section_count;
    }
    area_set_bounds(area);

    if( entry->region_count > 0 )
    {
        area->region_sources =
            calloc((size_t)entry->region_count, sizeof(*area->region_sources));
        assert(area->region_sources);
        for( int i = 0; i < entry->region_count; i++ )
        {
            struct RSCache_WorldMapRegion const* src = &entry->regions[i];
            struct ToriRS_WorldMapRegionSource* dst = &area->region_sources[i];
            dst->kind = src->kind;
            dst->min_plane = src->min_plane;
            dst->planes = src->planes;
            dst->src_region_x = src->src_region_x;
            dst->src_region_y = src->src_region_y;
            dst->src_chunk_x = src->src_chunk_x;
            dst->src_chunk_y = src->src_chunk_y;
            dst->dst_region_x = src->dst_region_x;
            dst->dst_region_y = src->dst_region_y;
            dst->dst_chunk_x = src->dst_chunk_x;
            dst->dst_chunk_y = src->dst_chunk_y;
            dst->group_id = src->group_id;
            dst->file_id = src->file_id;
        }
        area->region_source_count = entry->region_count;
    }

    if( entry->icon_count > 0 )
    {
        area->icons = calloc((size_t)entry->icon_count, sizeof(*area->icons));
        assert(area->icons);
        for( int i = 0; i < entry->icon_count; i++ )
        {
            area->icons[i].element = entry->icons[i].element;
            area->icons[i].coord = entry->icons[i].coord;
            area->icons[i].hidden = entry->icons[i].hidden;
        }
        area->icon_count = entry->icon_count;
    }
}

struct ToriRS_WorldMapAreas*
ToriRS_WorldMapAreasFromRSCacheDat2(
    struct RSCache_WorldMapArea const* entries,
    int count)
{
    struct ToriRS_WorldMapAreas* out;

    assert(entries);

    if( count <= 0 )
        return NULL;

    out = calloc(1, sizeof(*out));
    assert(out);
    out->areas = calloc((size_t)count, sizeof(*out->areas));
    assert(out->areas);
    out->count = count;

    for( int i = 0; i < count; i++ )
        area_from_rscache(&out->areas[i], &entries[i]);

    return out;
}

void
ToriRS_WorldMapAreasFree(struct ToriRS_WorldMapAreas* areas)
{
    if( !areas )
        return;
    for( int i = 0; i < areas->count; i++ )
    {
        free(areas->areas[i].internal_name);
        free(areas->areas[i].external_name);
        free(areas->areas[i].sections);
        free(areas->areas[i].icons);
        free(areas->areas[i].region_sources);
        free(areas->areas[i].overview_pixels);
    }
    free(areas->areas);
    free(areas);
}

struct ToriRS_WorldMapArea*
ToriRS_WorldMapAreasGet(
    struct ToriRS_WorldMapAreas* areas,
    int id)
{
    if( !areas )
        return NULL;
    for( int i = 0; i < areas->count; i++ )
    {
        if( areas->areas[i].id == id )
            return &areas->areas[i];
    }
    return NULL;
}

struct ToriRS_MapElement*
ToriRS_MapElementFromRSCacheDat2(
    int element_id,
    struct RSCache_MapElement* entry)
{
    struct ToriRS_MapElement* element;

    assert(entry);

    element = calloc(1, sizeof(*element));
    assert(element);
    element->id = element_id;
    element->text_size = entry->text_size;
    element->category = entry->category;
    element->sprite_id = entry->sprite_id;
    element->name = entry->name;
    entry->name = NULL;

    return element;
}

void
ToriRS_MapElementFree(struct ToriRS_MapElement* element)
{
    if( !element )
        return;
    free(element->name);
    free(element);
}
