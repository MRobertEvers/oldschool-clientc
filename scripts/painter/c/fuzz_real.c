/* Differential fuzzer: painter_paint_world3d (reference) vs painter_paint_bucket.
 * Invariant: every terrain tile / element drawn by world3d must also be drawn by bucket.
 *
 * Usage:
 *   ./fuzz_real <start> <count>              -- fuzz count seeds starting at start
 *   ./fuzz_real <seed> 1 shrink              -- shrink a failing seed
 *   ./fuzz_real <start> <count> bench [N]   -- benchmark N iters per seed (default 200)
 */
#include "painters.h"

#include "graphics/shared_tables.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef FUZZ_WITH_CACHE
/* Headless dat2 world load — no SDL, no renderer. */
#include "../../src2/libtorirs.h"
#include "../../src2/libtorirs_internal.h"
#include "../../src2/scripting/libtorirs_scriptapi.h"
#include "../../src2/platforms/platform_x/cachelib.h"
#include "../../src2/platforms/platform_x/cachelib_platform.h"
#include "../../src2/platforms/platform_x_io_reactor.h"
#include "../../src2/toriauxlib/c/toriauxlibc.h"
#include "../../src2/toriauxlib/toriauxlib.h"
#include "../../src2/world/world.h"
#include "../../src2/games/runescape.h"
#endif /* FUZZ_WITH_CACHE */

#define MAX_TERRAIN 16384
#define MAX_ELEMENT 16384
#define MAX_GRID 51

typedef struct
{
    uint64_t terrain[MAX_TERRAIN];
    int terrain_n;
    uint32_t elements[MAX_ELEMENT];
    int element_n;
} DrawnSet;

/* Per-seed count of elements successfully added to the painter, by category. */
typedef struct
{
    int scenery;
    int wall;
    int walldecor;
    int grounddecor;
    int groundobj;
} AddedCounts;

typedef struct
{
    uint32_t seed;
    int grid;
    int levels;
    int use_cullmap; /* 0 = nocull, 1 = runtime bake */
    int camera_sx;
    int camera_sz;
    int camera_slevel;
    int pitch;
    int yaw;
    uint8_t level_mask;
    int scenery_count;
    int wall_count;
    int decor_count;
    int ground_decor_count;
} FuzzConfig;

static uint32_t
rng_u32(uint32_t* s)
{
    *s = *s * 1103515245u + 12345u;
    return *s;
}

static int
rng_range(uint32_t* s, int lo, int hi)
{
    if( hi <= lo )
        return lo;
    return lo + (int)(rng_u32(s) % (uint32_t)(hi - lo + 1));
}

/* Returns a coordinate biased toward the camera draw window [cam-24, cam+24],
 * clamped to [0, gs-1].  When !focus, returns a uniform coordinate over the
 * whole grid so out-of-rect placement is still exercised. */
static int
pick_coord_near(uint32_t* s, int cam, int gs, int focus)
{
    if( !focus )
        return rng_range(s, 0, gs - 1);
    int lo = cam - 24;
    int hi = cam + 24;
    if( lo < 0 )
        lo = 0;
    if( hi > gs - 1 )
        hi = gs - 1;
    return rng_range(s, lo, hi);
}

/* Returns a level biased toward levels whose bit is set in level_mask.
 * When !focus (or no level is enabled), returns uniform [0, levels-1]. */
static int
pick_level(uint32_t* s, int levels, uint8_t level_mask, int focus)
{
    if( focus && level_mask )
    {
        /* Collect enabled levels. */
        int enabled[8];
        int n = 0;
        for( int l = 0; l < levels && l < 8; l++ )
            if( level_mask & (1u << l) )
                enabled[n++] = l;
        if( n > 0 )
            return enabled[rng_u32(s) % (uint32_t)n];
    }
    return rng_range(s, 0, levels - 1);
}

static bool
terrain_has(const DrawnSet* d, uint64_t key)
{
    for( int i = 0; i < d->terrain_n; i++ )
        if( d->terrain[i] == key )
            return true;
    return false;
}

static bool
element_has(const DrawnSet* d, uint32_t key)
{
    for( int i = 0; i < d->element_n; i++ )
        if( d->elements[i] == key )
            return true;
    return false;
}

static void
terrain_add(DrawnSet* d, int x, int z, int level)
{
    uint64_t key = ((uint64_t)(uint32_t)level << 32) | ((uint64_t)(uint32_t)z << 16) |
                   (uint64_t)(uint32_t)x;
    if( terrain_has(d, key) )
        return;
    if( d->terrain_n >= MAX_TERRAIN )
        return;
    d->terrain[d->terrain_n++] = key;
}

static void
element_add(DrawnSet* d, uint32_t entity)
{
    if( element_has(d, entity) )
        return;
    if( d->element_n >= MAX_ELEMENT )
        return;
    d->elements[d->element_n++] = entity;
}

/* Entity id ranges match the ranges used in build_scene:
 *   scenery    1000-1999
 *   wall       2000-2999
 *   walldecor  3000-3999
 *   grounddecor 4000-4999
 *   groundobj  5000-5999
 * Returns 0 for terrain or out-of-range ids. */
static int
classify_entity(uint32_t id)
{
    if( id >= 1000 && id < 2000 )
        return 1; /* scenery */
    if( id >= 2000 && id < 3000 )
        return 2; /* wall */
    if( id >= 3000 && id < 4000 )
        return 3; /* walldecor */
    if( id >= 4000 && id < 5000 )
        return 4; /* grounddecor */
    if( id >= 5000 && id < 6000 )
        return 5; /* groundobj */
    return 0;
}

static void
drawn_from_buffer(DrawnSet* d, const struct PaintersBuffer* buf)
{
    memset(d, 0, sizeof(*d));
    for( int i = 0; i < buf->command_count; i++ )
    {
        const struct PaintersElementCommand* cmd = &buf->commands[i];
        if( cmd->_bf_kind == PNTR_CMD_TERRAIN )
        {
            terrain_add(
                d,
                (int)cmd->_terrain._bf_terrain_x,
                (int)cmd->_terrain._bf_terrain_z,
                (int)cmd->_terrain._bf_terrain_y);
        }
        else if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
        {
            element_add(d, cmd->_entity._bf_entity);
        }
    }
}

static int
compare_superset(
    const DrawnSet* reference,
    const DrawnSet* bucket,
    int* missing_terrain,
    int* missing_elements)
{
    int mt = 0;
    int me = 0;
    for( int i = 0; i < reference->terrain_n; i++ )
        if( !terrain_has(bucket, reference->terrain[i]) )
            mt++;
    for( int i = 0; i < reference->element_n; i++ )
        if( !element_has(bucket, reference->elements[i]) )
            me++;
    *missing_terrain = mt;
    *missing_elements = me;
    return (mt == 0 && me == 0) ? 0 : 1;
}

static void
print_drawn_miss(
    const DrawnSet* reference,
    const DrawnSet* bucket,
    int max_print)
{
    int n = 0;
    for( int i = 0; i < reference->terrain_n && n < max_print; i++ )
    {
        uint64_t key = reference->terrain[i];
        if( !terrain_has(bucket, key) )
        {
            int x = (int)(key & 0xffffu);
            int z = (int)((key >> 16) & 0xffffu);
            int level = (int)(key >> 32);
            printf("  missing terrain (%d,%d,%d)\n", x, z, level);
            n++;
        }
    }
    n = 0;
    for( int i = 0; i < reference->element_n && n < max_print; i++ )
    {
        uint32_t e = reference->elements[i];
        if( !element_has(bucket, e) )
        {
            printf("  missing element %u\n", (unsigned)e);
            n++;
        }
    }
}

static struct Painter*
build_scene(const FuzzConfig* cfg, struct PaintersCullMap** out_cm, AddedCounts* added)
{
    uint32_t rng = cfg->seed;
    int gs = cfg->grid;
    int levels = cfg->levels;

    struct Painter* painter = painter_new(
        gs,
        gs,
        levels,
        PAINTER_NEW_CTX_BUCKET | PAINTER_NEW_CTX_WORLD3D);
    if( !painter )
        return NULL;

    struct PaintersCullMap* cm = NULL;
    if( cfg->use_cullmap )
    {
        cm = painters_cullmap_build(25, 512, 512, 384);
        if( !cm )
        {
            painter_free(painter);
            return NULL;
        }
    }
    else
    {
        cm = painters_cullmap_new_nocull();
        if( !cm )
        {
            painter_free(painter);
            return NULL;
        }
    }
    painter_set_cullmap(painter, cm);
    painter_set_camera_angles(painter, cfg->pitch, cfg->yaw);
    painter_set_level_mask(painter, cfg->level_mask);

    if( added )
        memset(added, 0, sizeof(*added));

    uint8_t lmask = cfg->level_mask ? cfg->level_mask : 0xFu;

    for( int i = 0; i < cfg->scenery_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        int w = rng_range(&rng, 1, 4);
        int h = rng_range(&rng, 1, 4);
        if( sx + w > gs )
            w = gs - sx;
        if( sz + h > gs )
            h = gs - sz;
        if( w < 1 )
            w = 1;
        if( h < 1 )
            h = 1;
        if( painter_add_normal_scenery(painter, sx, sz, slevel, 1000 + i, w, h) >= 0 && added )
            added->scenery++;
    }

    for( int i = 0; i < cfg->wall_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->wall_a != -1 && tile->wall_b != -1 )
            continue;
        int wall_ab = (tile->wall_a == -1) ? WALL_A : WALL_B;
        /* Include corner sides (bits 4-7) as well as straight sides (bits 0-3). */
        int side = 1 << rng_range(&rng, 0, 7);
        if( painter_add_wall(painter, sx, sz, slevel, 2000 + i, wall_ab, side) >= 0 && added )
            added->wall++;
    }

    for( int i = 0; i < cfg->decor_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->wall_decor_a != -1 && tile->wall_decor_b != -1 )
            continue;
        int wall_ab = (tile->wall_decor_a == -1) ? WALL_A : WALL_B;
        int side = 1 << rng_range(&rng, 0, 7);
        int through = (rng_u32(&rng) & 3u) == 0 ? THROUGHWALL : 0;
        if( painter_add_wall_decor(
                painter, sx, sz, slevel, 3000 + i, wall_ab, side, through) >= 0 &&
            added )
            added->walldecor++;
    }

    for( int i = 0; i < cfg->ground_decor_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->ground_decor == -1 )
        {
            painter_add_ground_decor(painter, sx, sz, slevel, 4000 + i);
            if( added )
                added->grounddecor++;
        }
    }

    if( levels >= 2 && (rng_u32(&rng) & 3u) == 0 )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int upper = rng_range(&rng, 1, levels - 1);
        painter_tile_set_bridge(painter, sx, sz, upper, sx, sz, 0);
    }

    if( levels >= 2 && (rng_u32(&rng) & 3u) == 0 )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int from = rng_range(&rng, 0, levels - 1);
        int to = rng_range(&rng, 0, levels - 1);
        if( from != to )
            painter_tile_copyto(painter, sx, sz, from, sx, sz, to);
    }

    for( int i = 0; i < 4; i++ )
    {
        if( (rng_u32(&rng) & 1u) == 0 )
            continue;
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        int draw = rng_range(&rng, 0, levels - 1);
        painter_tile_set_draw_level(painter, sx, sz, slevel, draw);
    }

    for( int i = 0; i < 3; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->ground_object_bottom == -1 )
        {
            painter_add_ground_object(painter, sx, sz, slevel, 5000 + i, GROUND_OBJECT_BOTTOM);
            if( added )
                added->groundobj++;
        }
    }

    painter_mark_static_count(painter);
    *out_cm = cm;
    return painter;
}

static void
fill_config(FuzzConfig* cfg, uint32_t seed)
{
    uint32_t rng = seed;
    memset(cfg, 0, sizeof(*cfg));
    cfg->seed = seed;
    cfg->grid = rng_range(&rng, 11, MAX_GRID);
    cfg->levels = rng_range(&rng, 1, 4);
    cfg->use_cullmap = (int)(rng_u32(&rng) & 1u);
    cfg->camera_sx = rng_range(&rng, 0, cfg->grid - 1);
    cfg->camera_sz = rng_range(&rng, 0, cfg->grid - 1);
    if( (rng_u32(&rng) & 7u) == 0 )
    {
        cfg->camera_sx = 0;
        cfg->camera_sz = 0;
    }
    else if( (rng_u32(&rng) & 7u) == 1 )
    {
        cfg->camera_sx = cfg->grid - 1;
        cfg->camera_sz = cfg->grid / 2;
    }
    cfg->camera_slevel = rng_range(&rng, 0, cfg->levels - 1);
    cfg->pitch = rng_range(&rng, 128, 384);
    cfg->yaw = rng_range(&rng, 0, 2047);
    cfg->level_mask = (uint8_t)(0xFu >> rng_range(&rng, 0, 3));
    if( cfg->level_mask == 0 )
        cfg->level_mask = 0xFu;
    cfg->scenery_count = rng_range(&rng, 0, 16);
    cfg->wall_count = rng_range(&rng, 0, 12);
    cfg->decor_count = rng_range(&rng, 0, 10);
    cfg->ground_decor_count = rng_range(&rng, 0, 8);
}

/* drawn_counts_from_set: tally elements from a DrawnSet by entity id category.
 * out[0]=scenery, [1]=wall, [2]=walldecor, [3]=grounddecor, [4]=groundobj. */
static void
drawn_counts_from_set(const DrawnSet* d, int out[5])
{
    for( int i = 0; i < 5; i++ )
        out[i] = 0;
    for( int i = 0; i < d->element_n; i++ )
    {
        int cat = classify_entity(d->elements[i]);
        if( cat >= 1 && cat <= 5 )
            out[cat - 1]++;
    }
}

static int
run_case(const FuzzConfig* cfg, int verbose, AddedCounts* out_added, int drawn_counts[5])
{
    AddedCounts added;
    struct PaintersCullMap* cm = NULL;
    struct Painter* painter = build_scene(cfg, &cm, &added);
    if( out_added )
        *out_added = added;
    if( !painter )
        return -1;

    struct PaintersBuffer* buf_w = painter_buffer_new();
    struct PaintersBuffer* buf_b = painter_buffer_new();
    if( !buf_w || !buf_b )
    {
        painter_free(painter);
        painters_cullmap_free(cm);
        free(buf_w);
        free(buf_b);
        return -1;
    }

    painter_paint_world3d(
        painter, buf_w, cfg->camera_sx, cfg->camera_sz, cfg->camera_slevel);
    painter_paint_bucket(
        painter, buf_b, cfg->camera_sx, cfg->camera_sz, cfg->camera_slevel);

    DrawnSet ref;
    DrawnSet got;
    drawn_from_buffer(&ref, buf_w);
    drawn_from_buffer(&got, buf_b);

    if( drawn_counts )
        drawn_counts_from_set(&ref, drawn_counts);

    int missing_t = 0;
    int missing_e = 0;
    int fail = compare_superset(&ref, &got, &missing_t, &missing_e);

    if( verbose || fail )
    {
        printf(
            "seed=%u grid=%d levels=%d cull=%d cam=(%d,%d,%d) pitch=%d yaw=%d mask=0x%x "
            "sc=%d wall=%d decor=%d gdec=%d | w3d: t=%d e=%d bucket: t=%d e=%d\n",
            cfg->seed,
            cfg->grid,
            cfg->levels,
            cfg->use_cullmap,
            cfg->camera_sx,
            cfg->camera_sz,
            cfg->camera_slevel,
            cfg->pitch,
            cfg->yaw,
            (unsigned)cfg->level_mask,
            cfg->scenery_count,
            cfg->wall_count,
            cfg->decor_count,
            cfg->ground_decor_count,
            ref.terrain_n,
            ref.element_n,
            got.terrain_n,
            got.element_n);
    }

    if( fail )
    {
        printf(
            "FAIL: bucket missing %d terrain, %d elements (world3d drew them)\n",
            missing_t,
            missing_e);
        print_drawn_miss(&ref, &got, 16);
    }

    free(buf_w->commands);
    free(buf_w);
    free(buf_b->commands);
    free(buf_b);
    painter_free(painter);
    painters_cullmap_free(cm);
    return fail;
}

static int
shrink_seed(uint32_t failing_seed)
{
    FuzzConfig base;
    fill_config(&base, failing_seed);
    FuzzConfig best = base;

    if( run_case(&base, 1, NULL, NULL) == 0 )
        return 0;

    /* Shrink scenery count */
    for( int sc = 0; sc < base.scenery_count; sc++ )
    {
        FuzzConfig try_cfg = base;
        try_cfg.scenery_count = sc;
        if( run_case(&try_cfg, 0, NULL, NULL) != 0 )
            best = try_cfg;
    }

    /* Shrink grid */
    for( int g = 11; g < base.grid; g += 2 )
    {
        FuzzConfig try_cfg = best;
        try_cfg.grid = g;
        if( try_cfg.camera_sx >= g )
            try_cfg.camera_sx = g / 2;
        if( try_cfg.camera_sz >= g )
            try_cfg.camera_sz = g / 2;
        if( run_case(&try_cfg, 0, NULL, NULL) != 0 )
            best = try_cfg;
    }

    printf("minimal repro-ish config:\n");
    run_case(&best, 1, NULL, NULL);
    return 1;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double
now_seconds(void)
{
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}
#else
static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}
#endif

static int
run_bench(uint32_t start, uint32_t count, int iters)
{
    double total_ns_w3d = 0.0;
    double total_ns_bkt = 0.0;
    int seeds_slower = 0;
    int seeds_run = 0;

    printf(
        "%-8s %5s %4s %4s %6s %11s %11s %7s\n",
        "seed", "grid", "lvl", "cull", "cam", "w3d_ns/it", "bkt_ns/it", "ratio");

    for( uint32_t i = 0; i < count; i++ )
    {
        FuzzConfig cfg;
        fill_config(&cfg, start + i);

        struct PaintersCullMap* cm = NULL;
        struct Painter* painter = build_scene(&cfg, &cm, NULL);
        if( !painter )
        {
            fprintf(stderr, "bench: build_scene failed seed=%u\n", start + i);
            continue;
        }

        struct PaintersBuffer* buf_w = painter_buffer_new();
        struct PaintersBuffer* buf_b = painter_buffer_new();
        if( !buf_w || !buf_b )
        {
            free(buf_w);
            free(buf_b);
            painter_free(painter);
            painters_cullmap_free(cm);
            continue;
        }

        /* Warmup — also triggers lazy ctx init for both painters. */
        painter_paint_world3d(painter, buf_w, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);
        painter_paint_bucket(painter, buf_b, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);

        double t0, t1;

        t0 = now_seconds();
        for( int it = 0; it < iters; it++ )
            painter_paint_world3d(
                painter, buf_w, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);
        t1 = now_seconds();
        double ns_w3d = (t1 - t0) * 1e9 / (double)iters;

        t0 = now_seconds();
        for( int it = 0; it < iters; it++ )
            painter_paint_bucket(
                painter, buf_b, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);
        t1 = now_seconds();
        double ns_bkt = (t1 - t0) * 1e9 / (double)iters;

        double ratio = (ns_w3d > 0.0) ? ns_bkt / ns_w3d : 0.0;
        int slower = (ns_bkt > ns_w3d);

        printf(
            "%-8u %5d %4d %4d %3d,%-2d %11.1f %11.1f %7.3f%s\n",
            start + i,
            cfg.grid,
            cfg.levels,
            cfg.use_cullmap,
            cfg.camera_sx,
            cfg.camera_sz,
            ns_w3d,
            ns_bkt,
            ratio,
            slower ? " SLOW" : "");

        total_ns_w3d += ns_w3d;
        total_ns_bkt += ns_bkt;
        if( slower )
            seeds_slower++;
        seeds_run++;

        free(buf_w->commands);
        free(buf_w);
        free(buf_b->commands);
        free(buf_b);
        painter_free(painter);
        painters_cullmap_free(cm);
    }

    if( seeds_run == 0 )
    {
        printf("bench: no seeds run\n");
        return 0;
    }

    double mean_ratio = total_ns_bkt / (total_ns_w3d > 0.0 ? total_ns_w3d : 1.0);
    printf(
        "\n--- aggregate over %d seeds ---\n"
        "  world3d total: %.1f ms  (%.1f ns/iter mean)\n"
        "  bucket  total: %.1f ms  (%.1f ns/iter mean)\n"
        "  mean ratio bucket/world3d: %.3f\n"
        "  seeds where bucket slower: %d / %d\n",
        seeds_run,
        total_ns_w3d / 1e6,
        total_ns_w3d / (double)seeds_run,
        total_ns_bkt / 1e6,
        total_ns_bkt / (double)seeds_run,
        mean_ratio,
        seeds_slower,
        seeds_run);

    return (total_ns_bkt > total_ns_w3d) ? 1 : 0;
}

#ifdef FUZZ_WITH_CACHE
/* ---------------------------------------------------------------------------
 * run_cache_scene: load the real dat2 centerzone and run the superset check
 * over a grid of (camera_sx, camera_sz, yaw, pitch, level) values.
 * cache_dir must point to the directory containing main_file_cache.dat2.
 * Returns 0 on full pass, 1 on any superset failure, -1 on load error.
 * --------------------------------------------------------------------------- */
static int
run_cache_scene(const char* cache_dir, int do_bench, int bench_iters)
{
    printf("Loading dat2 cache from: %s\n", cache_dir);
    fflush(stdout);

    struct LibToriRS_Instance* inst =
        LibToriRS_InstanceNewWithCacheMode((int)TORIAUXLIBC_MODE_DAT2);
    if( !inst )
    {
        fprintf(stderr, "cache: LibToriRS_InstanceNewWithCacheMode failed\n");
        return -1;
    }

    struct RSCacheDat2DiskLib* cache = cachelib_new(CACHE_MODE_DAT2);
    if( !cache || cachelib_platform_init(cache, cache_dir) != 1 )
    {
        fprintf(stderr, "cache: cachelib_platform_init failed for %s\n", cache_dir);
        return -1;
    }

    struct RSCacheDat2Disk* disk = cachelib_dat2_disk(cache);
    if( disk )
        ToriAuxLibC_SetDat2Disk(
            ToriAuxLib_C(LibToriRS_GetToriAuxLib(inst)), disk);

    struct LibToriPlatformX_IOReactor* rx = LibToriPlatformX_IOReactorNew(cache);
    if( !rx )
    {
        fprintf(stderr, "cache: LibToriPlatformX_IOReactorNew failed\n");
        return -1;
    }

    LibToriRS_ScriptAPI_Game_Runescape_Init(inst);
    LibToriRS_ScriptAPI_Dat2_TexturesLoad(inst);
    LibToriRS_ScriptAPI_Dat2_SubmitTextures(inst);

    printf("Running world rebuild tasks...\n");
    fflush(stdout);
    while( !LibToriRS_ScriptAPI_RunTasks(inst) )
        LibToriPlatformX_IOReactorProcess(rx, LibToriRS_GetIOQueue(inst));

    LibToriRS_ScriptAPI_Game_Runescape_BuildWorld(inst);

    struct GameRunescape* game = inst->runescape;
    if( !game || !game->world || !game->world->load_complete || !game->world->painter )
    {
        fprintf(stderr, "cache: world not built after init\n");
        return -1;
    }

    struct Painter* painter = game->world->painter;
    printf("World built. Starting painter differential sweep...\n");
    fflush(stdout);

    /* Sweep: 3 positions x 4 yaw x 2 pitch x 2 level_mask = 48 cases */
    static const int SX[] = { 26, 52, 78 };
    static const int SZ[] = { 26, 52, 78 };
    static const int YAWS[] = { 0, 512, 1024, 1536 };
    static const int PITCHES[] = { -200, -350 };
    static const uint8_t MASKS[] = { 0x1, 0xF };

    int total = 0;
    int failures = 0;

    double total_ns_w3d = 0.0;
    double total_ns_bkt = 0.0;

    struct PaintersBuffer* buf_w = painter_buffer_new();
    struct PaintersBuffer* buf_b = painter_buffer_new();
    if( !buf_w || !buf_b )
    {
        fprintf(stderr, "cache: painter_buffer_new failed\n");
        return -1;
    }

    for( int si = 0; si < 3; si++ )
    for( int sz_i = 0; sz_i < 3; sz_i++ )
    for( int yi = 0; yi < 4; yi++ )
    for( int pi = 0; pi < 2; pi++ )
    for( int mi = 0; mi < 2; mi++ )
    {
        int sx = SX[si];
        int sz = SZ[sz_i];
        int yaw = YAWS[yi];
        int pitch = PITCHES[pi];
        uint8_t mask = MASKS[mi];
        int slevel = (mask & 0x2) ? 1 : 0;

        painter_set_camera_angles(painter, pitch, yaw);
        painter_set_level_mask(painter, mask);

        buf_w->command_count = 0;
        buf_b->command_count = 0;

        if( do_bench )
        {
            /* Warmup. */
            painter_paint_world3d(painter, buf_w, sx, sz, slevel);
            painter_paint_bucket(painter, buf_b, sx, sz, slevel);

            double t0 = now_seconds();
            for( int it = 0; it < bench_iters; it++ )
            {
                buf_w->command_count = 0;
                painter_paint_world3d(painter, buf_w, sx, sz, slevel);
            }
            double t1 = now_seconds();
            for( int it = 0; it < bench_iters; it++ )
            {
                buf_b->command_count = 0;
                painter_paint_bucket(painter, buf_b, sx, sz, slevel);
            }
            double t2 = now_seconds();

            double ns_w3d = (t1 - t0) * 1e9 / bench_iters;
            double ns_bkt = (t2 - t1) * 1e9 / bench_iters;
            total_ns_w3d += ns_w3d;
            total_ns_bkt += ns_bkt;

            printf(
                "  bench sx=%d sz=%d yaw=%4d pitch=%4d mask=0x%x  "
                "w3d=%.0f ns  bkt=%.0f ns  ratio=%.3f\n",
                sx, sz, yaw, pitch, (unsigned)mask,
                ns_w3d, ns_bkt, ns_bkt / (ns_w3d > 0.0 ? ns_w3d : 1.0));
        }
        else
        {
            painter_paint_world3d(painter, buf_w, sx, sz, slevel);
            painter_paint_bucket(painter, buf_b, sx, sz, slevel);
        }

        DrawnSet ref, got;
        drawn_from_buffer(&ref, buf_w);
        drawn_from_buffer(&got, buf_b);

        int mt = 0, me = 0;
        int fail = compare_superset(&ref, &got, &mt, &me);
        total++;

        if( fail )
        {
            printf(
                "FAIL  sx=%d sz=%d yaw=%4d pitch=%4d mask=0x%x "
                "missing terrain=%d elements=%d\n",
                sx, sz, yaw, pitch, (unsigned)mask, mt, me);
            print_drawn_miss(&ref, &got, 8);
            failures++;
        }
        else
        {
            printf(
                "PASS  sx=%d sz=%d yaw=%4d pitch=%4d mask=0x%x "
                "terrain=%d elements=%d\n",
                sx, sz, yaw, pitch, (unsigned)mask,
                ref.terrain_n, ref.element_n);
        }
        fflush(stdout);
    }

    free(buf_w->commands);
    free(buf_w);
    free(buf_b->commands);
    free(buf_b);

    printf("\n--- cache scene: %d/%d PASS ---\n", total - failures, total);
    if( do_bench )
    {
        printf(
            "--- bench aggregate over %d sweeps ---\n"
            "  world3d mean: %.1f ns/iter\n"
            "  bucket  mean: %.1f ns/iter\n"
            "  mean ratio bucket/world3d: %.3f\n",
            total,
            total_ns_w3d / total,
            total_ns_bkt / total,
            total_ns_bkt / (total_ns_w3d > 0.0 ? total_ns_w3d : 1.0));
    }

    LibToriPlatformX_IOReactorFree(rx);
    cachelib_free(cache);
    LibToriRS_InstanceFree(inst);

    return failures > 0 ? 1 : 0;
}

/* Resolve dat2 cache directory relative to this binary's typical run location.
 * Returns static string or NULL. */
static const char*
find_cache_dir(void)
{
    static const char* candidates[] = {
        "cache",          "../cache",       "../../cache",
        "../../../cache", "../../../../cache", NULL,
    };
    char path[512];
    for( int i = 0; candidates[i]; i++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.dat2", candidates[i]);
        FILE* f = fopen(path, "rb");
        if( f )
        {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}
#endif /* FUZZ_WITH_CACHE */

int
main(int argc, char** argv)
{
    init_sin_table();
    init_cos_table();

    uint32_t start = 1u;
    uint32_t count = 500u;
    int shrink_one = 0;
    int do_bench = 0;
    int bench_iters = 200;

#ifdef FUZZ_WITH_CACHE
    if( argc >= 2 && strcmp(argv[1], "cache") == 0 )
    {
        const char* cache_dir = (argc >= 3) ? argv[2] : find_cache_dir();
        int cache_bench = (argc >= 4 && strcmp(argv[3], "bench") == 0);
        int cache_bench_iters = (cache_bench && argc >= 5)
            ? (int)strtol(argv[4], NULL, 10) : 200;
        if( !cache_dir )
        {
            fprintf(
                stderr,
                "Usage: fuzz_cache cache <dir> [bench [iters]]\n"
                "Could not auto-detect cache directory.\n");
            return 1;
        }
        return run_cache_scene(cache_dir, cache_bench, cache_bench_iters);
    }
#endif /* FUZZ_WITH_CACHE */

    if( argc >= 2 )
        start = (uint32_t)strtoul(argv[1], NULL, 10);
    if( argc >= 3 )
        count = (uint32_t)strtoul(argv[2], NULL, 10);
    if( argc >= 4 && strcmp(argv[3], "shrink") == 0 )
        shrink_one = 1;
    if( argc >= 4 && strcmp(argv[3], "bench") == 0 )
        do_bench = 1;
    if( do_bench && argc >= 5 )
        bench_iters = (int)strtol(argv[4], NULL, 10);

    if( shrink_one )
        return shrink_seed(start) ? 1 : 0;
    if( do_bench )
        return run_bench(start, count, bench_iters);

    int failures = 0;

    /* Coverage accumulators: [scenery, wall, walldecor, grounddecor, groundobj] */
    long total_added[5] = { 0, 0, 0, 0, 0 };
    long total_drawn[5] = { 0, 0, 0, 0, 0 };

    for( uint32_t i = 0; i < count; i++ )
    {
        FuzzConfig cfg;
        fill_config(&cfg, start + i);
        AddedCounts added;
        int drawn[5];
        int r = run_case(&cfg, 0, &added, drawn);
        if( r < 0 )
        {
            fprintf(stderr, "setup failed seed=%u\n", start + i);
            failures++;
        }
        else
        {
            /* Accumulate coverage regardless of pass/fail. */
            total_added[0] += added.scenery;
            total_added[1] += added.wall;
            total_added[2] += added.walldecor;
            total_added[3] += added.grounddecor;
            total_added[4] += added.groundobj;
            for( int c = 0; c < 5; c++ )
                total_drawn[c] += drawn[c];

            if( r > 0 )
            {
                failures++;
                shrink_seed(start + i);
                break;
            }
        }
    }

    static const char* cat_names[5] = {
        "scenery", "wall", "walldecor", "grounddecor", "groundobj"
    };
    printf(
        "%s: %u seeds\n",
        failures == 0 ? "OK" : "FAIL",
        count);
    printf("coverage (world3d drawn / added to scene):\n");
    for( int c = 0; c < 5; c++ )
    {
        long add = total_added[c];
        long drw = total_drawn[c];
        double pct = (add > 0) ? 100.0 * (double)drw / (double)add : 0.0;
        printf("  %-12s %7ld / %7ld  (%5.1f%%)\n", cat_names[c], drw, add, pct);
    }

    return failures ? 1 : 0;
}
