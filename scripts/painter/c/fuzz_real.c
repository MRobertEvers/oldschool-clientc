/* Differential fuzzer: painter_paint_world3d (reference) vs painter_paint_bucket.
 * Invariant: every terrain tile / element drawn by world3d must also be drawn by bucket.
 *
 * Usage:
 *   ./fuzz_real <start> <count>              -- fuzz count seeds starting at start
 *   ./fuzz_real <seed> 1 shrink              -- shrink a failing seed
 *   ./fuzz_real <start> <count> bench [N]   -- benchmark N iters per seed (default 200)
 */
#include "painter_bench_real.h"
#include "painter_fuzz_diff.h"
#include "painter_fuzz_scene.h"

#include "graphics/shared_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
run_case(
    const PainterFuzzConfig* cfg,
    int verbose,
    PainterFuzzAddedCounts* out_added,
    int drawn_counts[5])
{
    PainterFuzzAddedCounts added;
    struct PaintersCullMap* cm = NULL;
    struct Painter* painter = painter_fuzz_build_scene(cfg, &cm, &added);
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

    PainterFuzzDrawnSet ref;
    PainterFuzzDrawnSet got;
    painter_fuzz_drawn_from_buffer(&ref, buf_w);
    painter_fuzz_drawn_from_buffer(&got, buf_b);

    if( drawn_counts )
        painter_fuzz_drawn_counts_from_set(&ref, drawn_counts);

    int missing_t = 0;
    int missing_e = 0;
    int fail = painter_fuzz_compare_superset(&ref, &got, &missing_t, &missing_e);

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
        painter_fuzz_print_drawn_miss(&ref, &got, 16);
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
    PainterFuzzConfig base;
    painter_fuzz_fill_config(&base, failing_seed);
    PainterFuzzConfig best = base;

    if( run_case(&base, 1, NULL, NULL) == 0 )
        return 0;

    for( int sc = 0; sc < base.scenery_count; sc++ )
    {
        PainterFuzzConfig try_cfg = base;
        try_cfg.scenery_count = sc;
        if( run_case(&try_cfg, 0, NULL, NULL) != 0 )
            best = try_cfg;
    }

    for( int g = 11; g < base.grid; g += 2 )
    {
        PainterFuzzConfig try_cfg = best;
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
        const char* cache_dir = NULL;
        if( argc >= 3 && argv[2][0] != '\0' )
        {
            if( strcmp(argv[2], "kronos") == 0 )
                cache_dir = painter_bench_find_kronos_cache_dir();
            else
                cache_dir = argv[2];
        }
        else
        {
            cache_dir = painter_bench_find_cache_dir();
        }
        int cache_bench = (argc >= 4 && strcmp(argv[3], "bench") == 0);
        int cache_bench_iters =
            (cache_bench && argc >= 5) ? (int)strtol(argv[4], NULL, 10) : 200;
        if( !cache_dir )
        {
            fprintf(
                stderr,
                "Usage: fuzz_cache cache <dir> [bench [iters]]\n"
                "Could not auto-detect cache directory.\n");
            return 1;
        }
        return painter_fuzz_cache_scene(cache_dir, cache_bench, cache_bench_iters, 1);
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
        return painter_bench_seeded(start, count, bench_iters);

    int failures = 0;

    long total_added[5] = { 0, 0, 0, 0, 0 };
    long total_drawn[5] = { 0, 0, 0, 0, 0 };

    for( uint32_t i = 0; i < count; i++ )
    {
        PainterFuzzConfig cfg;
        painter_fuzz_fill_config(&cfg, start + i);
        PainterFuzzAddedCounts added;
        int drawn[5];
        int r = run_case(&cfg, 0, &added, drawn);
        if( r < 0 )
        {
            fprintf(stderr, "setup failed seed=%u\n", start + i);
            failures++;
        }
        else
        {
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
