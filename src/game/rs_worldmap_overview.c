#include "game/rs_worldmap_overview.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "game/rs_worldmap_render.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
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
/* The renderer's own cap on one region's icons. */
#define OVERVIEW_ICONS_PER_REGION 96
/* Close-up bakes (2..8 px a tile, what 595 bakes at its zooms): only the
 * regions in view need one, so a small pool, least recently used out, under
 * a byte budget (a region at 8 px a tile is 1 MB). */
#define OVERVIEW_HI_SLOTS 96
#define OVERVIEW_HI_BUDGET_BYTES (64u * 1024u * 1024u)
#define OVERVIEW_HI_SCALE_MAX 8
/* Close-up pixels baked per poll: four regions at 8 px a tile. */
#define OVERVIEW_HI_PIXELS_PER_POLL (4L * 512 * 512)
/* Regions a poll may have in flight (asked for, loaded and waiting on their
 * floors, or past this poll's bake budget) before it stops asking for more.
 * Every region asked for holds its decoded tiles (~70 KB) until its bake is
 * complete, and a poll over the Full map's 8,370 regions that asked for all
 * of them at once held ~590 MB of tiles (measured 2026-11-03; the assets
 * group's peak) and stalled the pump 400 ms on the loads. At 256 the loads
 * stream behind the bakes and the tiles held stay under ~20 MB. */
#define OVERVIEW_REGIONS_IN_FLIGHT 256

struct OverviewHiBake
{
    /** -1 free. */
    int key;
    int scale;
    int complete;
    int bakes;
    unsigned long stamp;
    /** (64 * scale) square, north up. */
    uint32_t* pixels;
};

struct OverviewBake
{
    /** CacheProvider_WorldMapGeographyKey of the destination region. */
    int key;
    int complete;
    int bakes;
    /** 64x64, north up; NULL for a region the area has no tiles for. */
    uint32_t* pixels;
    /** The icons its locs carry, in destination-region tiles. */
    struct RS_WorldMapRegionIcon* icons;
    int icon_count;
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
    struct RS_WorldMapRegionIcon icon_scratch[OVERVIEW_ICONS_PER_REGION];
    uint32_t* acc;
    size_t acc_cap;
    struct OverviewHiBake hi[OVERVIEW_HI_SLOTS];
    size_t hi_bytes;
    uint32_t* tmp;
    size_t tmp_cap;
    unsigned long clock;
    uint32_t* hi_scratch;
};

struct RS_WorldMapOverview*
RS_WorldMapOverview_New(void)
{
    struct RS_WorldMapOverview* overview = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*overview));
    assert(overview);
    overview->render = RS_WorldMapRender_New();
    for( int i = 0; i < OVERVIEW_HI_SLOTS; i++ )
        overview->hi[i].key = -1;
    overview->hi_scratch = ToriMem_Alloc(ToriMem_Editor(),
        sizeof(uint32_t) * OVERVIEW_TILES * OVERVIEW_HI_SCALE_MAX * OVERVIEW_TILES * OVERVIEW_HI_SCALE_MAX);
    assert(overview->hi_scratch);
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
    {
        ToriMem_Free(ToriMem_Editor(), overview->bakes[i].pixels);
        ToriMem_Free(ToriMem_Editor(), overview->bakes[i].icons);
    }
    ToriMem_Free(ToriMem_Editor(), overview->bakes);
    ToriMem_Free(ToriMem_Editor(), overview->slots);
    ToriMem_Free(ToriMem_Editor(), overview->acc);
    for( int i = 0; i < OVERVIEW_HI_SLOTS; i++ )
        ToriMem_Free(ToriMem_Editor(), overview->hi[i].pixels);
    ToriMem_Free(ToriMem_Editor(), overview->hi_scratch);
    ToriMem_Free(ToriMem_Editor(), overview->tmp);
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

int
RS_WorldMapOverview_TilePixel(
    double pixels_per_square,
    int t)
{
    return (int)floor((double)t * pixels_per_square / OVERVIEW_TILES);
}

/** Tile `t`'s pixel span [*lo, *hi), at least one pixel wide. */
static void
overview_span(
    double k,
    int t,
    int* lo,
    int* hi)
{
    *lo = (int)floor((double)t * k);
    *hi = (int)floor((double)(t + 1) * k);
    if( *hi <= *lo )
        *hi = *lo + 1;
}

void
RS_WorldMapOverview_AccumulateTile(
    struct RS_WorldMapOverviewTile const* tile,
    struct ToriRS_WorldMapRegionSource const* source,
    uint32_t const* bake,
    uint32_t* acc)
{
    int src_x0;
    int src_z0;
    int dst_x0;
    int dst_z0;
    int n;
    double k;
    int px0;
    int px1;
    int py0;
    int py1;
    int dx_lo;
    int dx_hi;
    int dz_lo;
    int dz_hi;

    assert(tile);
    assert(source);
    assert(bake);
    assert(acc);
    assert(tile->pixels_per_square > 0.0);
    assert(tile->width > 0);
    assert(tile->height > 0);

    k = tile->pixels_per_square / OVERVIEW_TILES;
    px0 = tile->pixel_x0;
    px1 = tile->pixel_x0 + tile->width;
    py0 = tile->pixel_y0;
    py1 = tile->pixel_y0 + tile->height;
    overview_source_extent(source, &src_x0, &src_z0, &dst_x0, &dst_z0, &n);

    /* Only the source tiles whose spans can reach the picture (one either
     * side for the rounding): at many pixels a tile, a picture holds a few. */
    dx_lo = (int)floor(px0 / k) - 1 - src_x0;
    dx_hi = (int)ceil(px1 / k) + 1 - src_x0;
    dx_lo = dx_lo < 0 ? 0 : dx_lo;
    dx_hi = dx_hi > n ? n : dx_hi;
    {
        /* Rows count from the north: row r is tile z WORLD-1-r. */
        int const r_lo = (int)floor(py0 / k) - 1;
        int const r_hi = (int)ceil(py1 / k) + 1;
        int const tz_hi = RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z - 1 - r_lo;
        int const tz_lo = RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z - 1 - r_hi;
        dz_lo = tz_lo - src_z0;
        dz_hi = tz_hi + 1 - src_z0;
        dz_lo = dz_lo < 0 ? 0 : dz_lo;
        dz_hi = dz_hi > n ? n : dz_hi;
    }

    for( int dz = dz_lo; dz < dz_hi; dz++ )
    {
        int const tz = src_z0 + dz;
        int y_lo;
        int y_hi;
        uint32_t const* bake_row;

        overview_span(k, RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z - 1 - tz, &y_lo, &y_hi);
        y_lo = y_lo < py0 ? py0 : y_lo;
        y_hi = y_hi > py1 ? py1 : y_hi;
        if( y_hi <= y_lo )
            continue;
        bake_row = &bake[(OVERVIEW_TILES - 1 - (dst_z0 + dz)) * OVERVIEW_TILES + dst_x0];
        for( int dx = dx_lo; dx < dx_hi; dx++ )
        {
            uint32_t const texel = bake_row[dx];
            uint32_t const r = (texel >> 16) & 0xFF;
            uint32_t const g = (texel >> 8) & 0xFF;
            uint32_t const b = texel & 0xFF;
            int x_lo;
            int x_hi;

            overview_span(k, src_x0 + dx, &x_lo, &x_hi);
            x_lo = x_lo < px0 ? px0 : x_lo;
            x_hi = x_hi > px1 ? px1 : x_hi;
            if( x_hi <= x_lo )
                continue;
            for( int y = y_lo; y < y_hi; y++ )
            {
                uint32_t* a = &acc[((size_t)(y - py0) * (size_t)tile->width + (size_t)(x_lo - px0)) * 4];
                for( int x = x_lo; x < x_hi; x++, a += 4 )
                {
                    a[0] += r;
                    a[1] += g;
                    a[2] += b;
                    a[3] += 1;
                }
            }
        }
    }
}

int
RS_WorldMapOverview_BakeScale(double pixels_per_square)
{
    int scale = (int)ceil(pixels_per_square / OVERVIEW_TILES - 1e-9);
    if( scale < 1 )
        scale = 1;
    if( scale > OVERVIEW_HI_SCALE_MAX )
        scale = OVERVIEW_HI_SCALE_MAX;
    return scale;
}

void
RS_WorldMapOverview_AccumulateTileScaled(
    struct RS_WorldMapOverviewTile const* tile,
    struct ToriRS_WorldMapRegionSource const* source,
    uint32_t const* bake,
    int scale,
    uint32_t* acc)
{
    int const stride = OVERVIEW_TILES * scale;
    int src_x0;
    int src_z0;
    int dst_x0;
    int dst_z0;
    int n;
    double k;
    int px0;
    int px1;
    int py0;
    int py1;

    assert(tile);
    assert(source);
    assert(bake);
    assert(acc);
    assert(scale >= 1);
    assert(scale <= OVERVIEW_HI_SCALE_MAX);
    assert(tile->pixels_per_square > 0.0);

    k = tile->pixels_per_square / OVERVIEW_TILES;
    px0 = tile->pixel_x0;
    px1 = tile->pixel_x0 + tile->width;
    py0 = tile->pixel_y0;
    py1 = tile->pixel_y0 + tile->height;
    overview_source_extent(source, &src_x0, &src_z0, &dst_x0, &dst_z0, &n);

    /* Each source tile's pixel span, filled from its scale x scale block of
     * the bake (nearest): a wall stays a line, a diagonal stays a diagonal. */
    for( int dz = 0; dz < n; dz++ )
    {
        int y_lo;
        int y_hi;
        int span_h;
        int bake_y0;

        overview_span(k, RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z - 1 - (src_z0 + dz), &y_lo, &y_hi);
        if( y_hi <= py0 || y_lo >= py1 )
            continue;
        span_h = y_hi - y_lo;
        bake_y0 = (OVERVIEW_TILES - 1 - (dst_z0 + dz)) * scale;
        for( int dx = 0; dx < n; dx++ )
        {
            int x_lo;
            int x_hi;
            int span_w;
            int const bake_x0 = (dst_x0 + dx) * scale;

            overview_span(k, src_x0 + dx, &x_lo, &x_hi);
            if( x_hi <= px0 || x_lo >= px1 )
                continue;
            span_w = x_hi - x_lo;
            for( int y = y_lo > py0 ? y_lo : py0; y < y_hi && y < py1; y++ )
            {
                uint32_t const* row = &bake[(size_t)(bake_y0 + (y - y_lo) * scale / span_h) * (size_t)stride];
                uint32_t* a = &acc[((size_t)(y - py0) * (size_t)tile->width) * 4];
                for( int x = x_lo > px0 ? x_lo : px0; x < x_hi && x < px1; x++ )
                {
                    uint32_t const texel = row[bake_x0 + (x - x_lo) * scale / span_w];
                    uint32_t* p = &a[(size_t)(x - px0) * 4];
                    p[0] += (texel >> 16) & 0xFF;
                    p[1] += (texel >> 8) & 0xFF;
                    p[2] += texel & 0xFF;
                    p[3] += 1;
                }
            }
        }
    }
}

static struct OverviewHiBake*
overview_hi_find(
    struct RS_WorldMapOverview* overview,
    int key,
    int scale)
{
    for( int i = 0; i < OVERVIEW_HI_SLOTS; i++ )
        if( overview->hi[i].key == key && overview->hi[i].scale == scale )
            return &overview->hi[i];
    return NULL;
}

/** A slot for a new close-up bake: free, else the least recently used one
 *  not used this poll; NULL when every one is. Keeps the byte budget. */
static struct OverviewHiBake*
overview_hi_claim(
    struct RS_WorldMapOverview* overview,
    size_t bytes)
{
    struct OverviewHiBake* pick = NULL;
    for( int i = 0; i < OVERVIEW_HI_SLOTS; i++ )
    {
        struct OverviewHiBake* hi = &overview->hi[i];
        if( hi->key < 0 )
        {
            pick = hi;
            break;
        }
        if( hi->stamp == overview->clock )
            continue;
        if( !pick || hi->stamp < pick->stamp )
            pick = hi;
    }
    if( !pick )
        return NULL;
    if( pick->key >= 0 || pick->pixels )
    {
        overview->hi_bytes -= pick->pixels ? (size_t)(OVERVIEW_TILES * pick->scale) * (OVERVIEW_TILES * pick->scale) * 4 : 0;
        ToriMem_Free(ToriMem_Editor(), pick->pixels);
        pick->pixels = NULL;
        pick->key = -1;
    }
    /* Over budget: drop the least recently used until it fits. */
    while( overview->hi_bytes + bytes > OVERVIEW_HI_BUDGET_BYTES )
    {
        struct OverviewHiBake* victim = NULL;
        for( int i = 0; i < OVERVIEW_HI_SLOTS; i++ )
        {
            struct OverviewHiBake* hi = &overview->hi[i];
            if( hi == pick || hi->key < 0 || hi->stamp == overview->clock )
                continue;
            if( !victim || hi->stamp < victim->stamp )
                victim = hi;
        }
        if( !victim )
            break;
        overview->hi_bytes -= (size_t)(OVERVIEW_TILES * victim->scale) * (OVERVIEW_TILES * victim->scale) * 4;
        ToriMem_Free(ToriMem_Editor(), victim->pixels);
        victim->pixels = NULL;
        victim->key = -1;
    }
    memset(pick, 0, sizeof(*pick));
    pick->key = -1;
    return pick;
}

void
RS_WorldMapOverview_ResolveTile(
    struct RS_WorldMapOverviewTile const* tile,
    uint32_t const* acc,
    uint32_t* out)
{
    size_t count;

    assert(tile);
    assert(acc);
    assert(out);

    count = (size_t)tile->width * (size_t)tile->height;
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

/** OVERVIEW_REGIONS_IN_FLIGHT, or TORIRS_WORLDMAP_INFLIGHT=<n> (0: no cap,
 *  the shape before the cap) for a measurement. */
static int
overview_regions_in_flight(void)
{
    static int cap = -1;
    if( cap < 0 )
    {
        const char* env = getenv("TORIRS_WORLDMAP_INFLIGHT");
        cap = env && env[0] ? atoi(env) : OVERVIEW_REGIONS_IN_FLIGHT;
        if( cap <= 0 )
            cap = 1 << 30;
    }
    return cap;
}

/** One area's share of a poll (the frame's budgets already begun). */
static int
overview_poll_area(
    struct RS_WorldMapOverview* overview,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window,
    double pixels_per_square)
{
    int missing = 0;
    int const scale = RS_WorldMapOverview_BakeScale(pixels_per_square);

    for( int i = 0; i < area->region_source_count; i++ )
    {
        struct ToriRS_WorldMapRegionSource const* source = &area->region_sources[i];
        int const key = CacheProvider_WorldMapGeographyKey(area->id, source->dst_region_x, source->dst_region_y);
        struct OverviewBake* bake;
        bool complete = false;
        int icon_count = 0;
        int baked;

        if( !RS_WorldMapOverview_SourceInWindow(window, source) )
            continue;
        bake = overview_find(overview, key);
        if( bake && (bake->complete || bake->bakes >= RS_WORLDMAP_OVERVIEW_REBAKES) )
            continue;
        /* Enough in flight: the rest is counted as still to come and asked
         * for on a later poll, once these have baked. */
        if( missing >= overview_regions_in_flight() )
        {
            missing++;
            continue;
        }
        baked = RS_WorldMapRender_RegionPixels(
            overview->render, provider, scene, queue, area, source->dst_region_x, source->dst_region_y, 1,
            overview->scratch, &complete, overview->icon_scratch, OVERVIEW_ICONS_PER_REGION, &icon_count);
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
        ToriMem_Free(ToriMem_Editor(), bake->icons);
        bake->icons = NULL;
        bake->icon_count = 0;
        if( icon_count > 0 )
        {
            bake->icons = ToriMem_Alloc(ToriMem_Editor(), sizeof(*bake->icons) * (size_t)icon_count);
            assert(bake->icons);
            memcpy(bake->icons, overview->icon_scratch, sizeof(*bake->icons) * (size_t)icon_count);
            bake->icon_count = icon_count;
        }
        bake->complete = complete ? 1 : 0;
        bake->bakes++;
        overview->serial++;
        if( !bake->complete && bake->bakes < RS_WORLDMAP_OVERVIEW_REBAKES )
            missing++;
    }

    /* Close up: the regions in view baked again at the zoom's own pixels a
     * tile, as 595 bakes them. */
    long hi_spent = 0;
    for( int i = 0; scale > 1 && i < area->region_source_count; i++ )
    {
        struct ToriRS_WorldMapRegionSource const* source = &area->region_sources[i];
        int const key = CacheProvider_WorldMapGeographyKey(area->id, source->dst_region_x, source->dst_region_y);
        size_t const bytes = (size_t)(OVERVIEW_TILES * scale) * (size_t)(OVERVIEW_TILES * scale) * 4;
        struct OverviewHiBake* hi;
        bool complete = false;
        int icon_count = 0;
        int baked;

        if( !RS_WorldMapOverview_SourceInWindow(window, source) )
            continue;
        hi = overview_hi_find(overview, key, scale);
        if( hi )
            hi->stamp = overview->clock;
        if( hi && (hi->complete || hi->bakes >= RS_WORLDMAP_OVERVIEW_REBAKES) )
            continue;
        /* A close-up bake costs its pixels: a few a poll, the rest next. */
        if( hi_spent >= OVERVIEW_HI_PIXELS_PER_POLL )
        {
            missing++;
            continue;
        }
        hi_spent += (long)(bytes / 4);
        baked = RS_WorldMapRender_RegionPixels(overview->render, provider, scene, queue, area, source->dst_region_x,
            source->dst_region_y, scale, overview->hi_scratch, &complete, overview->icon_scratch,
            OVERVIEW_ICONS_PER_REGION, &icon_count);
        if( baked < 0 )
            continue;
        if( baked == 0 )
        {
            missing++;
            continue;
        }
        if( !hi )
        {
            hi = overview_hi_claim(overview, bytes);
            if( !hi )
                continue;
            hi->key = key;
            hi->scale = scale;
            hi->pixels = ToriMem_Alloc(ToriMem_Editor(), bytes);
            assert(hi->pixels);
            overview->hi_bytes += bytes;
        }
        memcpy(hi->pixels, overview->hi_scratch, bytes);
        hi->complete = complete ? 1 : 0;
        hi->bakes++;
        hi->stamp = overview->clock;
        overview->serial++;
        if( !hi->complete && hi->bakes < RS_WORLDMAP_OVERVIEW_REBAKES )
            missing++;
    }
    return missing;
}

int
RS_WorldMapOverview_Poll(
    struct RS_WorldMapOverview* overview,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    int mapscene_scene_id,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window,
    double pixels_per_square)
{
    assert(overview);
    assert(provider);
    assert(area);
    assert(window);
    overview->clock++;
    RS_WorldMapRender_BeginFrame(overview->render);
    RS_WorldMapRender_SetMapScenes(overview->render, mapscene_scene_id);
    return overview_poll_area(overview, provider, scene, queue, area, window, pixels_per_square);
}

/** The areas in drawing order: the main (surface) one first, the rest by
 *  their place in the cache; `index` 0..count-1. */
static struct ToriRS_WorldMapArea const*
overview_area_in_order(
    struct ToriRS_WorldMapAreas const* areas,
    int index)
{
    int seen = 0;
    for( int pass = 0; pass < 2; pass++ )
        for( int i = 0; i < areas->count; i++ )
        {
            int const is_main = areas->areas[i].is_main != 0;
            if( is_main != (pass == 0) )
                continue;
            if( seen++ == index )
                return &areas->areas[i];
        }
    return NULL;
}

/** Whether any of `area`'s records has a source square in the window. */
static int
overview_area_in_window(
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window)
{
    for( int i = 0; i < area->region_source_count; i++ )
        if( RS_WorldMapOverview_SourceInWindow(window, &area->region_sources[i]) )
            return 1;
    return 0;
}

int
RS_WorldMapOverview_PollAll(
    struct RS_WorldMapOverview* overview,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    int mapscene_scene_id,
    struct ToriRS_WorldMapAreas const* areas,
    struct RS_WorldMapOverviewWindow const* window,
    double pixels_per_square)
{
    int missing = 0;

    assert(overview);
    assert(provider);
    assert(areas);
    assert(window);
    overview->clock++;
    RS_WorldMapRender_BeginFrame(overview->render);
    RS_WorldMapRender_SetMapScenes(overview->render, mapscene_scene_id);
    for( int i = 0; i < areas->count; i++ )
    {
        struct ToriRS_WorldMapArea const* area = overview_area_in_order(areas, i);
        if( area && overview_area_in_window(area, window) )
            missing += overview_poll_area(overview, provider, scene, queue, area, window, pixels_per_square);
    }
    return missing;
}

/** Whether a record's source tiles reach the picture. */
static int
overview_source_in_tile(
    struct RS_WorldMapOverviewTile const* tile,
    struct ToriRS_WorldMapRegionSource const* source)
{
    double const k = tile->pixels_per_square / OVERVIEW_TILES;
    int src_x0;
    int src_z0;
    int dst_x0;
    int dst_z0;
    int n;
    int x_lo;
    int x_hi;
    int y_lo;
    int y_hi;
    int unused;

    overview_source_extent(source, &src_x0, &src_z0, &dst_x0, &dst_z0, &n);
    overview_span(k, src_x0, &x_lo, &unused);
    overview_span(k, src_x0 + n - 1, &unused, &x_hi);
    overview_span(k, RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z - 1 - (src_z0 + n - 1), &y_lo, &unused);
    overview_span(k, RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z - 1 - src_z0, &unused, &y_hi);
    return !(x_hi <= tile->pixel_x0 || x_lo >= tile->pixel_x0 + tile->width || y_hi <= tile->pixel_y0
             || y_lo >= tile->pixel_y0 + tile->height);
}

void
RS_WorldMapOverview_RenderTile(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewTile const* tile,
    uint32_t* out)
{
    size_t pixels;

    assert(overview);
    assert(area);
    assert(tile);
    assert(out);
    assert(tile->width > 0);
    assert(tile->height > 0);
    assert(tile->pixels_per_square > 0.0);

    int const scale = RS_WorldMapOverview_BakeScale(tile->pixels_per_square);

    pixels = (size_t)tile->width * (size_t)tile->height;
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

        if( !overview_source_in_tile(tile, source) )
            continue;
        if( scale > 1 )
        {
            struct OverviewHiBake* hi = overview_hi_find(
                overview, CacheProvider_WorldMapGeographyKey(area->id, source->dst_region_x, source->dst_region_y),
                scale);
            if( hi && hi->pixels )
            {
                RS_WorldMapOverview_AccumulateTileScaled(tile, source, hi->pixels, scale, overview->acc);
                continue;
            }
        }
        bake = overview_find(
            overview, CacheProvider_WorldMapGeographyKey(area->id, source->dst_region_x, source->dst_region_y));
        if( !bake || !bake->pixels )
            continue;
        RS_WorldMapOverview_AccumulateTile(tile, source, bake->pixels, overview->acc);
    }
    RS_WorldMapOverview_ResolveTile(tile, overview->acc, out);
}

void
RS_WorldMapOverview_RenderTileAll(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapAreas const* areas,
    struct RS_WorldMapOverviewTile const* tile,
    uint32_t* out)
{
    size_t const pixels = (size_t)tile->width * (size_t)tile->height;

    assert(overview);
    assert(areas);
    assert(tile);
    assert(out);

    memset(out, 0, pixels * sizeof(uint32_t));
    if( pixels > overview->tmp_cap )
    {
        ToriMem_Free(ToriMem_Editor(), overview->tmp);
        overview->tmp_cap = pixels;
        overview->tmp = ToriMem_Alloc(ToriMem_Editor(), pixels * sizeof(uint32_t));
        assert(overview->tmp);
    }
    /* Every area that places a region on the picture's true squares, the
     * surface first: where two place one, the first drawn keeps it. */
    for( int a = 0; a < areas->count; a++ )
    {
        struct ToriRS_WorldMapArea const* area = overview_area_in_order(areas, a);
        int touches = 0;
        if( !area )
            continue;
        for( int i = 0; i < area->region_source_count && !touches; i++ )
            touches = overview_source_in_tile(tile, &area->region_sources[i]);
        if( !touches )
            continue;
        RS_WorldMapOverview_RenderTile(overview, area, tile, overview->tmp);
        for( size_t p = 0; p < pixels; p++ )
            if( out[p] == 0 )
                out[p] = overview->tmp[p];
    }
}

int
RS_WorldMapOverview_IconsAll(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapAreas const* areas,
    struct RS_WorldMapOverviewWindow const* window,
    struct RS_WorldMapOverviewIcon* out,
    int capacity)
{
    int count = 0;

    assert(overview);
    assert(areas);
    assert(window);
    assert(out || capacity == 0);
    for( int a = 0; a < areas->count && count < capacity; a++ )
    {
        struct ToriRS_WorldMapArea const* area = overview_area_in_order(areas, a);
        if( area && overview_area_in_window(area, window) )
            count += RS_WorldMapOverview_Icons(overview, area, window, out + count, capacity - count);
    }
    return count;
}

int
RS_WorldMapOverview_Icons(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window,
    struct RS_WorldMapOverviewIcon* out,
    int capacity)
{
    int count = 0;

    assert(overview);
    assert(area);
    assert(window);
    assert(out || capacity == 0);

    /* The compositemap's own list first (the labels and the loc-less icons:
     * few, and what a crowded view must not lose): source (world) coords,
     * plane<<28|x<<14|y. Then the icons the baked regions' locs carry. */
    for( int i = 0; i < area->icon_count && count < capacity; i++ )
    {
        struct ToriRS_WorldMapIcon const* icon = &area->icons[i];
        int const x = (icon->coord >> 14) & 0x3FFF;
        int const z = icon->coord & 0x3FFF;
        if( icon->hidden || x / OVERVIEW_TILES < window->square_x0
            || x / OVERVIEW_TILES >= window->square_x0 + window->squares_w || z / OVERVIEW_TILES < window->square_z0
            || z / OVERVIEW_TILES >= window->square_z0 + window->squares_h )
            continue;
        out[count].element_id = icon->element;
        out[count].tile_x = x;
        out[count].tile_z = z;
        count++;
    }
    for( int i = 0; i < area->region_source_count && count < capacity; i++ )
    {
        struct ToriRS_WorldMapRegionSource const* source = &area->region_sources[i];
        struct OverviewBake* bake;
        int src_x0;
        int src_z0;
        int dst_x0;
        int dst_z0;
        int n;

        if( !RS_WorldMapOverview_SourceInWindow(window, source) )
            continue;
        bake = overview_find(
            overview, CacheProvider_WorldMapGeographyKey(area->id, source->dst_region_x, source->dst_region_y));
        if( !bake )
            continue;
        overview_source_extent(source, &src_x0, &src_z0, &dst_x0, &dst_z0, &n);
        for( int k = 0; k < bake->icon_count && count < capacity; k++ )
        {
            struct RS_WorldMapRegionIcon const* icon = &bake->icons[k];
            /* A chunk record owns only the icons inside its own 8x8. */
            if( icon->tile_x < dst_x0 || icon->tile_x >= dst_x0 + n || icon->tile_y < dst_z0
                || icon->tile_y >= dst_z0 + n )
                continue;
            out[count].element_id = icon->element_id;
            out[count].tile_x = src_x0 + (icon->tile_x - dst_x0);
            out[count].tile_z = src_z0 + (icon->tile_y - dst_z0);
            count++;
        }
    }
    return count;
}
