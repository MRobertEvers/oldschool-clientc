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
build_scene(const FuzzConfig* cfg, struct PaintersCullMap** out_cm)
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

    for( int i = 0; i < cfg->scenery_count; i++ )
    {
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int slevel = rng_range(&rng, 0, levels - 1);
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
        painter_add_normal_scenery(painter, sx, sz, slevel, 1000 + i, w, h);
    }

    for( int i = 0; i < cfg->wall_count; i++ )
    {
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int slevel = rng_range(&rng, 0, levels - 1);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->wall_a != -1 && tile->wall_b != -1 )
            continue;
        int wall_ab = (tile->wall_a == -1) ? WALL_A : WALL_B;
        int side = 1 << rng_range(&rng, 0, 3);
        painter_add_wall(painter, sx, sz, slevel, 2000 + i, wall_ab, side);
    }

    for( int i = 0; i < cfg->decor_count; i++ )
    {
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int slevel = rng_range(&rng, 0, levels - 1);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->wall_decor_a != -1 && tile->wall_decor_b != -1 )
            continue;
        int wall_ab = (tile->wall_decor_a == -1) ? WALL_A : WALL_B;
        int side = 1 << rng_range(&rng, 0, 7);
        int through = (rng_u32(&rng) & 3u) == 0 ? THROUGHWALL : 0;
        painter_add_wall_decor(
            painter, sx, sz, slevel, 3000 + i, wall_ab, side, through);
    }

    for( int i = 0; i < cfg->ground_decor_count; i++ )
    {
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int slevel = rng_range(&rng, 0, levels - 1);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->ground_decor == -1 )
            painter_add_ground_decor(painter, sx, sz, slevel, 4000 + i);
    }

    if( levels >= 2 && (rng_u32(&rng) & 3u) == 0 )
    {
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int upper = rng_range(&rng, 1, levels - 1);
        painter_tile_set_bridge(painter, sx, sz, upper, sx, sz, 0);
    }

    if( levels >= 2 && (rng_u32(&rng) & 3u) == 0 )
    {
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int from = rng_range(&rng, 0, levels - 1);
        int to = rng_range(&rng, 0, levels - 1);
        if( from != to )
            painter_tile_copyto(painter, sx, sz, from, sx, sz, to);
    }

    for( int i = 0; i < 4; i++ )
    {
        if( (rng_u32(&rng) & 1u) == 0 )
            continue;
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int slevel = rng_range(&rng, 0, levels - 1);
        int draw = rng_range(&rng, 0, levels - 1);
        painter_tile_set_draw_level(painter, sx, sz, slevel, draw);
    }

    for( int i = 0; i < 3; i++ )
    {
        int sx = rng_range(&rng, 0, gs - 1);
        int sz = rng_range(&rng, 0, gs - 1);
        int slevel = rng_range(&rng, 0, levels - 1);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->ground_object_bottom == -1 )
            painter_add_ground_object(painter, sx, sz, slevel, 5000 + i, GROUND_OBJECT_BOTTOM);
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
    cfg->scenery_count = rng_range(&rng, 0, 8);
    cfg->wall_count = rng_range(&rng, 0, 6);
    cfg->decor_count = rng_range(&rng, 0, 4);
    cfg->ground_decor_count = rng_range(&rng, 0, 4);
}

static int
run_case(const FuzzConfig* cfg, int verbose)
{
    struct PaintersCullMap* cm = NULL;
    struct Painter* painter = build_scene(cfg, &cm);
    if( !painter )
        return -1;

    struct PaintersBuffer* buf_w = painter_buffer_new();
    struct PaintersBuffer* buf_b = painter_buffer_new();
    if( !buf_w || !buf_b )
    {
        painter_buffer_new(); /* silence unused if alloc partial */
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

    if( run_case(&base, 1) == 0 )
        return 0;

    /* Shrink scenery count */
    for( int sc = 0; sc < base.scenery_count; sc++ )
    {
        FuzzConfig try_cfg = base;
        try_cfg.scenery_count = sc;
        if( run_case(&try_cfg, 0) != 0 )
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
        if( run_case(&try_cfg, 0) != 0 )
            best = try_cfg;
    }

    printf("minimal repro-ish config:\n");
    run_case(&best, 1);
    return 1;
}

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

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
        struct Painter* painter = build_scene(&cfg, &cm);
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
    for( uint32_t i = 0; i < count; i++ )
    {
        FuzzConfig cfg;
        fill_config(&cfg, start + i);
        int r = run_case(&cfg, 0);
        if( r < 0 )
        {
            fprintf(stderr, "setup failed seed=%u\n", start + i);
            failures++;
        }
        else if( r > 0 )
        {
            failures++;
            shrink_seed(start + i);
            break;
        }
    }

    if( failures == 0 )
        printf("OK: %u seeds passed superset check\n", count);
    return failures ? 1 : 0;
}
