#include "game/rs_worldmap_overview.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "game/rs_worldmap_render.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define OVERVIEW_TILES 64
#define OVERVIEW_CHUNK_TILES 8
/* A bake at one pixel per tile costs about 0.12 ms on the M-series Mac this
 * was measured on, so a frame can take 512 (~60 ms) and the whole surface
 * (1632 regions) lands in four polls once its tiles are read. Loads are
 * queued for the whole window at once; the runner retires them as fast as
 * the cache answers. */
#define OVERVIEW_BAKES_PER_FRAME 512
#define OVERVIEW_LOADS_PER_FRAME 4096

struct OverviewBake
{
    /** CacheProvider_WorldMapGeographyKey of the destination region. */
    int key;
    int complete;
    int bakes;
    /** 64x64, north up; NULL for a region the area has no tiles for. */
    uint32_t* pixels;
};

struct RS_WorldMapOverview
{
    struct RS_WorldMapRender* render;
    struct OverviewBake* bakes;
    int bake_count;
    int bake_cap;
    /** Open addressing over `bakes`: index + 1, 0 empty. */
    int* slots;
    int slot_cap;
    int serial;
    uint32_t scratch[OVERVIEW_TILES * OVERVIEW_TILES];
    uint32_t* acc;
    size_t acc_cap;
};

struct RS_WorldMapOverview*
RS_WorldMapOverview_New(void)
{
    struct RS_WorldMapOverview* overview = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*overview));
    assert(overview);
    overview->render = RS_WorldMapRender_New();
    RS_WorldMapRender_SetFrameBudget(
        overview->render, OVERVIEW_BAKES_PER_FRAME, OVERVIEW_LOADS_PER_FRAME);
    return overview;
}

void
RS_WorldMapOverview_Free(struct RS_WorldMapOverview* overview)
{
    if( !overview )
        return;
    for( int i = 0; i < overview->bake_count; i++ )
        ToriMem_Free(ToriMem_Editor(), overview->bakes[i].pixels);
    ToriMem_Free(ToriMem_Editor(), overview->bakes);
    ToriMem_Free(ToriMem_Editor(), overview->slots);
    ToriMem_Free(ToriMem_Editor(), overview->acc);
    RS_WorldMapRender_Free(overview->render);
    ToriMem_Free(ToriMem_Editor(), overview);
}

int
RS_WorldMapOverview_Serial(struct RS_WorldMapOverview const* overview)
{
    assert(overview);
    return overview->serial;
}

/* ---- the bake table ----------------------------------------------------- */

static uint32_t
overview_hash(int key)
{
    return (uint32_t)key * 2654435761u;
}

static struct OverviewBake*
overview_find(
    struct RS_WorldMapOverview* overview,
    int key)
{
    if( overview->slot_cap == 0 )
        return NULL;
    for( uint32_t probe = overview_hash(key) & (uint32_t)(overview->slot_cap - 1);;
         probe = (probe + 1) & (uint32_t)(overview->slot_cap - 1) )
    {
        int const at = overview->slots[probe];
        if( at == 0 )
            return NULL;
        if( overview->bakes[at - 1].key == key )
            return &overview->bakes[at - 1];
    }
}

static void
overview_rehash(
    struct RS_WorldMapOverview* overview,
    int slot_cap)
{
    ToriMem_Free(ToriMem_Editor(), overview->slots);
    overview->slots = ToriMem_Calloc(ToriMem_Editor(), (size_t)slot_cap, sizeof(int));
    assert(overview->slots);
    overview->slot_cap = slot_cap;
    for( int i = 0; i < overview->bake_count; i++ )
    {
        uint32_t probe = overview_hash(overview->bakes[i].key) & (uint32_t)(slot_cap - 1);
        while( overview->slots[probe] != 0 )
            probe = (probe + 1) & (uint32_t)(slot_cap - 1);
        overview->slots[probe] = i + 1;
    }
}

static struct OverviewBake*
overview_insert(
    struct RS_WorldMapOverview* overview,
    int key)
{
    struct OverviewBake* bake;

    if( overview->bake_count == overview->bake_cap )
    {
        overview->bake_cap = overview->bake_cap ? overview->bake_cap * 2 : 256;
        overview->bakes = ToriMem_Realloc(
            ToriMem_Editor(), overview->bakes, (size_t)overview->bake_cap * sizeof(*overview->bakes));
        assert(overview->bakes);
    }
    /* Half full at most, so a probe ends quickly. */
    if( (overview->bake_count + 1) * 2 > overview->slot_cap )
        overview_rehash(overview, overview->slot_cap ? overview->slot_cap * 2 : 512);
    bake = &overview->bakes[overview->bake_count++];
    memset(bake, 0, sizeof(*bake));
    bake->key = key;
    {
        uint32_t probe = overview_hash(key) & (uint32_t)(overview->slot_cap - 1);
        while( overview->slots[probe] != 0 )
            probe = (probe + 1) & (uint32_t)(overview->slot_cap - 1);
        overview->slots[probe] = overview->bake_count;
    }
    return bake;
}

/* ---- the mapping -------------------------------------------------------- */

static void
overview_source_extent(
    struct ToriRS_WorldMapRegionSource const* source,
    int* out_src_x0,
    int* out_src_z0,
    int* out_dst_x0,
    int* out_dst_z0,
    int* out_n)
{
    if( source->kind == 1 )
    {
        *out_src_x0 = source->src_region_x * OVERVIEW_TILES + source->src_chunk_x * OVERVIEW_CHUNK_TILES;
        *out_src_z0 = source->src_region_y * OVERVIEW_TILES + source->src_chunk_y * OVERVIEW_CHUNK_TILES;
        *out_dst_x0 = source->dst_chunk_x * OVERVIEW_CHUNK_TILES;
        *out_dst_z0 = source->dst_chunk_y * OVERVIEW_CHUNK_TILES;
        *out_n = OVERVIEW_CHUNK_TILES;
        return;
    }
    *out_src_x0 = source->src_region_x * OVERVIEW_TILES;
    *out_src_z0 = source->src_region_y * OVERVIEW_TILES;
    *out_dst_x0 = 0;
    *out_dst_z0 = 0;
    *out_n = OVERVIEW_TILES;
}

int
RS_WorldMapOverview_SourceInWindow(
    struct RS_WorldMapOverviewWindow const* window,
    struct ToriRS_WorldMapRegionSource const* source)
{
    assert(window);
    assert(source);
    return source->src_region_x >= window->square_x0
           && source->src_region_x < window->square_x0 + window->squares_w
           && source->src_region_y >= window->square_z0
           && source->src_region_y < window->square_z0 + window->squares_h;
}

void
RS_WorldMapOverview_Accumulate(
    struct RS_WorldMapOverviewWindow const* window,
    struct ToriRS_WorldMapRegionSource const* source,
    uint32_t const* bake,
    uint32_t* acc)
{
    int src_x0;
    int src_z0;
    int dst_x0;
    int dst_z0;
    int n;
    int pps;
    int out_w;
    int win_x0;
    int win_x1;
    int win_z0;
    int win_z1;

    assert(window);
    assert(source);
    assert(bake);
    assert(acc);
    assert(window->pixels_per_square >= 1);
    assert(window->pixels_per_square <= RS_WORLDMAP_OVERVIEW_PPS_MAX);

    pps = window->pixels_per_square;
    out_w = window->squares_w * pps;
    win_x0 = window->square_x0 * OVERVIEW_TILES;
    win_x1 = (window->square_x0 + window->squares_w) * OVERVIEW_TILES;
    win_z0 = window->square_z0 * OVERVIEW_TILES;
    win_z1 = (window->square_z0 + window->squares_h) * OVERVIEW_TILES;
    overview_source_extent(source, &src_x0, &src_z0, &dst_x0, &dst_z0, &n);

    for( int dz = 0; dz < n; dz++ )
    {
        int const tz = src_z0 + dz;
        int oy;
        uint32_t const* bake_row;

        if( tz < win_z0 || tz >= win_z1 )
            continue;
        /* Output row 0 is the window's north edge, bake row 0 the region's. */
        oy = (win_z1 - 1 - tz) * pps / OVERVIEW_TILES;
        bake_row = &bake[(OVERVIEW_TILES - 1 - (dst_z0 + dz)) * OVERVIEW_TILES + dst_x0];
        for( int dx = 0; dx < n; dx++ )
        {
            int const tx = src_x0 + dx;
            uint32_t const texel = bake_row[dx];
            uint32_t* a;

            if( tx < win_x0 || tx >= win_x1 )
                continue;
            a = &acc[((size_t)oy * (size_t)out_w + (size_t)((tx - win_x0) * pps / OVERVIEW_TILES)) * 4];
            a[0] += (texel >> 16) & 0xFF;
            a[1] += (texel >> 8) & 0xFF;
            a[2] += texel & 0xFF;
            a[3] += 1;
        }
    }
}

void
RS_WorldMapOverview_Resolve(
    struct RS_WorldMapOverviewWindow const* window,
    uint32_t const* acc,
    uint32_t* out)
{
    size_t count;

    assert(window);
    assert(acc);
    assert(out);

    count = (size_t)window->squares_w * (size_t)window->pixels_per_square * (size_t)window->squares_h
            * (size_t)window->pixels_per_square;
    for( size_t i = 0; i < count; i++ )
    {
        uint32_t const* a = &acc[i * 4];
        if( a[3] == 0 )
        {
            out[i] = 0;
            continue;
        }
        out[i] = 0xFF000000u | ((a[0] / a[3]) << 16) | ((a[1] / a[3]) << 8) | (a[2] / a[3]);
    }
}

/* ---- polling and drawing ------------------------------------------------ */

int
RS_WorldMapOverview_Poll(
    struct RS_WorldMapOverview* overview,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    int mapscene_scene_id,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window)
{
    int missing = 0;

    assert(overview);
    assert(provider);
    assert(area);
    assert(window);

    RS_WorldMapRender_BeginFrame(overview->render);
    RS_WorldMapRender_SetMapScenes(overview->render, mapscene_scene_id);
    for( int i = 0; i < area->region_source_count; i++ )
    {
        struct ToriRS_WorldMapRegionSource const* source = &area->region_sources[i];
        int const key = CacheProvider_WorldMapGeographyKey(area->id, source->dst_region_x, source->dst_region_y);
        struct OverviewBake* bake;
        bool complete = false;
        int baked;

        if( !RS_WorldMapOverview_SourceInWindow(window, source) )
            continue;
        bake = overview_find(overview, key);
        if( bake && (bake->complete || bake->bakes >= RS_WORLDMAP_OVERVIEW_REBAKES) )
            continue;
        baked = RS_WorldMapRender_RegionPixels(
            overview->render, provider, scene, queue, area, source->dst_region_x, source->dst_region_y, 1,
            overview->scratch, &complete);
        if( baked == 0 )
        {
            missing++;
            continue;
        }
        if( !bake )
            bake = overview_insert(overview, key);
        if( baked < 0 )
        {
            /* Cannot happen for a destination the area's own records name;
             * remembered as nothing so it is never asked again. */
            bake->complete = 1;
            continue;
        }
        if( !bake->pixels )
        {
            bake->pixels =
                ToriMem_Alloc(ToriMem_Editor(), sizeof(uint32_t) * OVERVIEW_TILES * OVERVIEW_TILES);
            assert(bake->pixels);
        }
        memcpy(bake->pixels, overview->scratch, sizeof(uint32_t) * OVERVIEW_TILES * OVERVIEW_TILES);
        bake->complete = complete ? 1 : 0;
        bake->bakes++;
        overview->serial++;
        if( !bake->complete && bake->bakes < RS_WORLDMAP_OVERVIEW_REBAKES )
            missing++;
    }
    return missing;
}

void
RS_WorldMapOverview_Render(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window,
    uint32_t* out)
{
    size_t pixels;

    assert(overview);
    assert(area);
    assert(window);
    assert(out);
    assert(window->squares_w > 0);
    assert(window->squares_h > 0);
    assert(window->pixels_per_square >= 1);
    assert(window->pixels_per_square <= RS_WORLDMAP_OVERVIEW_PPS_MAX);

    pixels = (size_t)window->squares_w * (size_t)window->pixels_per_square * (size_t)window->squares_h
             * (size_t)window->pixels_per_square;
    if( pixels * 4 > overview->acc_cap )
    {
        ToriMem_Free(ToriMem_Editor(), overview->acc);
        overview->acc_cap = pixels * 4;
        overview->acc = ToriMem_Alloc(ToriMem_Editor(), overview->acc_cap * sizeof(uint32_t));
        assert(overview->acc);
    }
    memset(overview->acc, 0, pixels * 4 * sizeof(uint32_t));
    for( int i = 0; i < area->region_source_count; i++ )
    {
        struct ToriRS_WorldMapRegionSource const* source = &area->region_sources[i];
        struct OverviewBake* bake;

        if( !RS_WorldMapOverview_SourceInWindow(window, source) )
            continue;
        bake = overview_find(
            overview, CacheProvider_WorldMapGeographyKey(area->id, source->dst_region_x, source->dst_region_y));
        if( !bake || !bake->pixels )
            continue;
        RS_WorldMapOverview_Accumulate(window, source, bake->pixels, overview->acc);
    }
    RS_WorldMapOverview_Resolve(window, overview->acc, out);
}
