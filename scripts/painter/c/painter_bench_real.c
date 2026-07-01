#include "painter_bench_real.h"

#include "painter_fuzz_diff.h"
#include "painter_fuzz_scene.h"
#include "painters.h"

#include "platforms/platform_x/cache_path_resolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef FUZZ_WITH_CACHE
#include "../../src2/games/runescape.h"
#include "../../src2/libtorirs.h"
#include "../../src2/libtorirs_internal.h"
#include "../../src2/platforms/platform_x/cachelib.h"
#include "../../src2/platforms/platform_x/cachelib_platform.h"
#include "../../src2/platforms/platform_x_io_reactor.h"
#include "../../src2/scripting/libtorirs_scriptapi.h"
#include "../../src2/toriauxlib/c/toriauxlibcache.h"
#include "../../src2/toriauxlib/toriauxlib.h"
#include "../../src2/world/world.h"
#endif /* FUZZ_WITH_CACHE */

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

int
painter_bench_seeded(
    uint32_t start,
    uint32_t count,
    int iters)
{
    double total_ns_w3d = 0.0;
    double total_ns_bkt = 0.0;
    int seeds_slower = 0;
    int seeds_run = 0;

    printf(
        "%-8s %5s %4s %4s %6s %11s %11s %7s\n",
        "seed",
        "grid",
        "lvl",
        "cull",
        "cam",
        "w3d_ns/it",
        "bkt_ns/it",
        "ratio");

    for( uint32_t i = 0; i < count; i++ )
    {
        PainterFuzzConfig cfg;
        painter_fuzz_fill_config(&cfg, start + i);

        struct PaintersCullMap* cm = NULL;
        struct Painter* painter = painter_fuzz_build_scene(&cfg, &cm, NULL);
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

        painter_paint_world3d(painter, buf_w, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);
        painter_paint_bucket(painter, buf_b, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);

        double t0 = now_seconds();
        for( int it = 0; it < iters; it++ )
            painter_paint_world3d(painter, buf_w, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);
        double t1 = now_seconds();
        double ns_w3d = (t1 - t0) * 1e9 / (double)iters;

        t0 = now_seconds();
        for( int it = 0; it < iters; it++ )
            painter_paint_bucket(painter, buf_b, cfg.camera_sx, cfg.camera_sz, cfg.camera_slevel);
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

const char*
painter_bench_find_cache_dir(void)
{
    static const char* candidates[] = {
        "cache", "../cache", "../../cache", "../../../cache", "../../../../cache", NULL,
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

const char*
painter_bench_find_kronos_cache_dir(void)
{
    return cache_path_resolve_kronos_repo();
}

int
painter_fuzz_cache_scene(
    const char* cache_dir,
    int do_bench,
    int bench_iters,
    int diff_check)
{
    printf("Loading dat2 cache from: %s\n", cache_dir);
    fflush(stdout);

    struct LibToriRS_Instance* inst =
        LibToriRS_InstanceNewWithCacheMode((int)TORIAUXLIBCACHE_MODE_DAT2);
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
        ToriAuxLibCache_SetDat2Disk(ToriAuxLib_C(LibToriRS_GetToriAuxLib(inst)), disk);

    struct LibToriPlatformX_IOReactor* rx = LibToriPlatformX_IOReactorNew(cache);
    if( !rx )
    {
        fprintf(stderr, "cache: LibToriPlatformX_IOReactorNew failed\n");
        return -1;
    }

    LibToriRS_ScriptAPI_Game_Runescape_Init(inst);
    /* Painter traversal does not need dat2 textures; skipping avoids decode
     * asserts on caches with truncated/corrupt texture entries. */
    printf("Running world rebuild tasks...\n");
    fflush(stdout);
    while( !LibToriRS_ScriptAPI_RunTasks(inst) )
        LibToriPlatformX_IOReactorProcess(rx, LibToriRS_GetIOQueue(inst));

#define RUNESCAPE_ZONE_CENTER_X 337
#define RUNESCAPE_ZONE_CENTER_Z 437
    LibToriRS_ScriptAPI_Game_Runescape_BuildWorldCenterzone(
        inst, RUNESCAPE_ZONE_CENTER_X, RUNESCAPE_ZONE_CENTER_Z, 104);

    struct GameRunescape* game = inst->runescape;
    if( !game || !game->world || !game->world->load_complete || !game->world->painter )
    {
        fprintf(stderr, "cache: world not built after init\n");
        return -1;
    }

    struct Painter* painter = game->world->painter;
    if( diff_check )
    {
        printf("World built. Starting painter differential sweep...\n");
    }
    else
    {
        printf("World built. Starting painter benchmark sweep...\n");
    }
    fflush(stdout);

    static const int SX[] = { 26, 52, 78 };
    static const int SZ[] = { 26, 52, 78 };
    static const int YAWS[] = { 0, 512, 1024, 1536 };
    static const int PITCHES[] = { -200, -350 };
    static const uint8_t MASKS[] = { 0x1, 0xF };

    int total = 0;
    int failures = 0;
    int bench_sweeps = 0;

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
                            bench_sweeps++;

                            printf(
                                "  bench sx=%d sz=%d yaw=%4d pitch=%4d mask=0x%x  "
                                "w3d=%.0f ns  bkt=%.0f ns  ratio=%.3f\n",
                                sx,
                                sz,
                                yaw,
                                pitch,
                                (unsigned)mask,
                                ns_w3d,
                                ns_bkt,
                                ns_bkt / (ns_w3d > 0.0 ? ns_w3d : 1.0));
                        }
                        else
                        {
                            painter_paint_world3d(painter, buf_w, sx, sz, slevel);
                            painter_paint_bucket(painter, buf_b, sx, sz, slevel);
                        }

                        if( diff_check )
                        {
                            PainterFuzzDrawnSet ref, got;
                            painter_fuzz_drawn_from_buffer(&ref, buf_w);
                            painter_fuzz_drawn_from_buffer(&got, buf_b);

                            int mt = 0, me = 0;
                            int fail = painter_fuzz_compare_superset(&ref, &got, &mt, &me);
                            total++;

                            if( fail )
                            {
                                printf(
                                    "FAIL  sx=%d sz=%d yaw=%4d pitch=%4d mask=0x%x "
                                    "missing terrain=%d elements=%d\n",
                                    sx,
                                    sz,
                                    yaw,
                                    pitch,
                                    (unsigned)mask,
                                    mt,
                                    me);
                                painter_fuzz_print_drawn_miss(&ref, &got, 8);
                                failures++;
                            }
                            else
                            {
                                printf(
                                    "PASS  sx=%d sz=%d yaw=%4d pitch=%4d mask=0x%x "
                                    "terrain=%d elements=%d\n",
                                    sx,
                                    sz,
                                    yaw,
                                    pitch,
                                    (unsigned)mask,
                                    ref.terrain_n,
                                    ref.element_n);
                            }
                            fflush(stdout);
                        }
                        else if( !diff_check )
                        {
                            total++;
                        }
                    }

    free(buf_w->commands);
    free(buf_w);
    free(buf_b->commands);
    free(buf_b);

    if( diff_check )
    {
        printf("\n--- cache scene: %d/%d PASS ---\n", total - failures, total);
    }
    if( do_bench && bench_sweeps > 0 )
    {
        printf(
            "--- bench aggregate over %d sweeps ---\n"
            "  world3d mean: %.1f ns/iter\n"
            "  bucket  mean: %.1f ns/iter\n"
            "  mean ratio bucket/world3d: %.3f\n",
            bench_sweeps,
            total_ns_w3d / bench_sweeps,
            total_ns_bkt / bench_sweeps,
            total_ns_bkt / (total_ns_w3d > 0.0 ? total_ns_w3d : 1.0));
    }

    LibToriPlatformX_IOReactorFree(rx);
    cachelib_free(cache);
    LibToriRS_InstanceFree(inst);

    if( diff_check && failures > 0 )
        return 1;
    if( do_bench && !diff_check && total_ns_bkt > total_ns_w3d )
        return 1;
    return 0;
}

int
painter_bench_cache(
    const char* cache_dir,
    int iters)
{
    return painter_fuzz_cache_scene(cache_dir, 1, iters, 0);
}

#endif /* FUZZ_WITH_CACHE */
