/**
 * benchmark_main.c — Rasterizer benchmark suite
 *
 * Benchmarks flat, gouraud, and texture rasterizer variants (sort + branching)
 * over 10 frames with 20,000 triangles each.
 *
 * Uses SDL2 for display and Nuklear for timing overlay.
 * Loads real textures from cache for transparent/opaque texture benchmarks.
 * Targets Windows XP (i686 MinGW, Pentium 4, SSE2).
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SDL2 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

/* Nuklear config + implementation pulled in via the SDL renderer header */
#define TORIRS_NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear/backends/sdl_renderer/nuklear_torirs_sdl_renderer.h"
#include "nuklear/torirs_nuklear.h"

/* Shared tables (hsl->rgb, reciprocal, trig) */
#include "graphics/shared_tables.h"

/* --- Include rasterizer implementations directly (static inline) --- */

/* Flat */
#include "graphics/raster/flat/flat.screen.alpha.branching.s4.c"
#include "graphics/raster/flat/flat.screen.alpha.sort.s4.u.c"
#include "graphics/raster/flat/flat.screen.opaque.branching.s4.c"
#include "graphics/raster/flat/flat.screen.opaque.sort.s4.u.c"

/* Gouraud — span must precede gouraud.screen.alpha.edge.sort.s4.u.c */
#include "graphics/raster/gouraud/gouraud.screen.alpha.bary.branching.s4.c"
#include "graphics/raster/gouraud/gouraud.screen.alpha.bary.sort.s1.u.c"
#include "graphics/raster/gouraud/gouraud.screen.alpha.bary.sort.s4.u.c"
#include "graphics/raster/gouraud/gouraud.screen.alpha.edge.sort.s4.u.c"
#include "graphics/raster/gouraud/gouraud.screen.opaque.bary.branching.s4.c"
#include "graphics/raster/gouraud/gouraud.screen.opaque.bary.sort.s1.u.c"
#include "graphics/raster/gouraud/gouraud.screen.opaque.bary.sort.s4.u.c"
#include "graphics/raster/gouraud/gouraud.screen.opaque.edge.sort.s4.u.c"
#include "graphics/raster/gouraud/span/gouraud.screen.alpha.span.u.c"

/* Texture dependencies — replicate include chain from texture.u.c */
#include "graphics/raster/texture/span/tex.span.u.c"

#define SWAP(a, b)                                                                                 \
    {                                                                                              \
        int tmp = a;                                                                               \
        a = b;                                                                                     \
        b = tmp;                                                                                   \
    }

#include "graphics/projection.u.c"

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Scanline helpers (must come before the triangle rasterizers) */
#include "graphics/raster/texture/texshadeblend.persp.texopaque.sort.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeblend.persp.textrans.sort.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.ordered.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.sort.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeflat.persp.textrans.ordered.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeflat.persp.textrans.sort.lerp8.scanline.u.c"

/* Texture triangle rasterizers (perspective-correct flat + blend + affine blend) */
#include "graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.affine.textrans.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeblend.affine.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.persp.texopaque.sort.lerp8.u.c"
#include "graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeblend.persp.textrans.sort.lerp8.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.sort.lerp8.u.c"
#include "graphics/raster/texture/texshadeflat.persp.textrans.branching.lerp8.u.c"
#include "graphics/raster/texture/texshadeflat.persp.textrans.sort.lerp8.u.c"

/* Pix3D deob reference ports (static inline rasterizers; globals live in dash.c via pix3d_deob_compat) */
#include "graphics/raster/deob/pix3d_deob_state.h"
#include "graphics/raster/flat/flat.deob.u.c"
#include "graphics/raster/gouraud/gouraud.deob.u.c"
#include "graphics/raster/texture/texture.deob.u.c"
#include "graphics/raster/texture/texture.deob2.u.c"

/* Texture loading from cache */
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/texture.h"

/* Config table texture archive ID */
#define CONFIG_DAT_TEXTURES 6

/* ---------- Configuration ---------- */

#define SCREEN_W 800
#define SCREEN_H 600
#define NUM_TRIS 10000
#define MIN_AREA_ABS 64
#define NUM_FRAMES 15
#define WARMUP_FRAMES 1
#define CAMERA_FOV 512
#define TEX_WIDTH 128
#define BENCH_ALPHA7 128

/* ---------- Variant enumeration ---------- */

enum BenchVariant
{
    BENCH_FLAT_OPAQUE_SORT,
    BENCH_FLAT_OPAQUE_BRANCHING,
    BENCH_FLAT_ALPHA_SORT,
    BENCH_FLAT_ALPHA_BRANCHING,
    BENCH_GOURAUD_OPAQUE_EDGE_SORT_S4,
    BENCH_GOURAUD_OPAQUE_BARY_BRANCHING_S4,
    BENCH_GOURAUD_OPAQUE_BARY_SORT_S4,
    BENCH_GOURAUD_OPAQUE_BARY_SORT_S1,
    BENCH_GOURAUD_ALPHA_EDGE_SORT_S4,
    BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S4,
    BENCH_GOURAUD_ALPHA_BARY_SORT_S4,
    BENCH_GOURAUD_ALPHA_BARY_SORT_S1,
    BENCH_TEXFLAT_PERSP_OPAQUE_SORT,
    BENCH_TEXFLAT_PERSP_OPAQUE_BRANCHING,
    BENCH_TEXFLAT_PERSP_TRANS_SORT,
    BENCH_TEXFLAT_PERSP_TRANS_BRANCHING,
    BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING,
    BENCH_TEXBLEND_PERSP_TRANS_BRANCHING,
    BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING_V3,
    BENCH_TEXBLEND_PERSP_TRANS_BRANCHING_V3,
    BENCH_TEXBLEND_PERSP_OPAQUE_SORT,
    BENCH_TEXBLEND_PERSP_TRANS_SORT,
    BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING,
    BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING,
    BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING_V3,
    BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING_V3,
    BENCH_FLAT_DEOB,
    BENCH_GOURAUD_DEOB,
    BENCH_TEX_OPAQUE_DEOB,
    BENCH_TEX_TRANS_DEOB,
    BENCH_TEX_OPAQUE_DEOB2,
    BENCH_TEX_TRANS_DEOB2,
    BENCH_VARIANT_COUNT
};

static const char* variant_names[BENCH_VARIANT_COUNT] = {
    "Flat opaque (sort)",
    "Flat opaque (branching)",
    "Flat alpha (sort)",
    "Flat alpha (branching)",
    "Gouraud opaque (edge sort s4)",
    "Gouraud opaque (bary branching s4)",
    "Gouraud opaque (bary sort s4)",
    "Gouraud opaque (bary sort s1)",
    "Gouraud alpha (edge sort s4)",
    "Gouraud alpha (bary branching s4)",
    "Gouraud alpha (bary sort s4)",
    "Gouraud alpha (bary sort s1)",
    "TexFlat persp opaque (sort)",
    "TexFlat persp opaque (branching)",
    "TexFlat persp transparent (sort)",
    "TexFlat persp transparent (branching)",
    "TexBlend persp opaque (branching lerp8)",
    "TexBlend persp transparent (branching lerp8)",
    "TexBlend persp opaque (branching lerp8_v3)",
    "TexBlend persp transparent (branching lerp8_v3)",
    "TexBlend persp opaque (sort lerp8)",
    "TexBlend persp transparent (sort lerp8)",
    "TexBlend affine opaque (branching lerp8)",
    "TexBlend affine transparent (branching lerp8)",
    "TexBlend affine opaque (branching lerp8_v3)",
    "TexBlend affine transparent (branching lerp8_v3)",
    "Flat (Pix3D deob)",
    "Gouraud (Pix3D deob)",
    "Texture opaque (Pix3D deob)",
    "Texture transparent (Pix3D deob)",
    "Texture opaque (Pix3D deob2)",
    "Texture transparent (Pix3D deob2)",
};

/* Category for display grouping */
static int variant_category[BENCH_VARIANT_COUNT] = {
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5,
};
static const char* category_names[] = {
    "Flat",
    "Gouraud",
    "Texture Flat (Persp)",
    "Texture Blend (Persp)",
    "Texture Blend (Affine)",
    "Deob (Pix3D reference)",
};

/* ---------- Triangle data ---------- */

static int tri_x0[NUM_TRIS], tri_y0[NUM_TRIS];
static int tri_x1[NUM_TRIS], tri_y1[NUM_TRIS];
static int tri_x2[NUM_TRIS], tri_y2[NUM_TRIS];

/* Gouraud per-vertex colors (HSL16) */
static int tri_color0[NUM_TRIS];
static int tri_color1[NUM_TRIS];
static int tri_color2[NUM_TRIS];

/* Flat color */
static int tri_flat_color[NUM_TRIS];

/* Texture UV mapping: orthographic coordinates */
static int tri_ortho_uvorigin_x[NUM_TRIS], tri_ortho_uvorigin_y[NUM_TRIS],
    tri_ortho_uvorigin_z[NUM_TRIS];
static int tri_ortho_uend_x[NUM_TRIS], tri_ortho_uend_y[NUM_TRIS], tri_ortho_uend_z[NUM_TRIS];
static int tri_ortho_vend_x[NUM_TRIS], tri_ortho_vend_y[NUM_TRIS], tri_ortho_vend_z[NUM_TRIS];
static int tri_shade7bit[NUM_TRIS];

/* ---------- Pixel buffer ---------- */

static int pixels[SCREEN_W * SCREEN_H];

/* ---------- Timing results ---------- */

static double frame_times[BENCH_VARIANT_COUNT][NUM_FRAMES];
static double avg_ms[BENCH_VARIANT_COUNT];
static double min_ms[BENCH_VARIANT_COUNT];
static double max_ms[BENCH_VARIANT_COUNT];

/* ---------- Texture data ---------- */

static int opaque_texels[TEX_WIDTH * TEX_WIDTH];
static int transparent_texels[TEX_WIDTH * TEX_WIDTH];
static int have_cache_textures = 0;

/* ---------- Simple LCG PRNG (deterministic, XP-safe) ---------- */

static unsigned int bench_rng_state = 12345;

static int
bench_rand(void)
{
    bench_rng_state = bench_rng_state * 1103515245u + 12345u;
    return (int)((bench_rng_state >> 16) & 0x7fff);
}

static int
bench_rand_range(
    int lo,
    int hi)
{
    if( lo >= hi )
        return lo;
    return lo + (bench_rand() % (hi - lo));
}

/* ---------- Triangle generation ---------- */

static int
cross2d(
    int ax,
    int ay,
    int bx,
    int by)
{
    return ax * by - ay * bx;
}

static void
generate_triangles(void)
{
    int margin = 20;
    int count = 0;

    while( count < NUM_TRIS )
    {
        int x0 = bench_rand_range(margin, SCREEN_W - margin);
        int y0 = bench_rand_range(margin, SCREEN_H - margin);
        int x1 = bench_rand_range(margin, SCREEN_W - margin);
        int y1 = bench_rand_range(margin, SCREEN_H - margin);
        int x2 = bench_rand_range(margin, SCREEN_W - margin);
        int y2 = bench_rand_range(margin, SCREEN_H - margin);

        /* Signed area (positive = CCW) */
        int area = cross2d(x1 - x0, y1 - y0, x2 - x0, y2 - y0);
        if( area < 0 )
        {
            /* Make CCW by swapping v1 and v2 */
            int tmp;
            tmp = x1;
            x1 = x2;
            x2 = tmp;
            tmp = y1;
            y1 = y2;
            y2 = tmp;
            area = -area;
        }
        if( area < MIN_AREA_ABS )
            continue;

        tri_x0[count] = x0;
        tri_y0[count] = y0;
        tri_x1[count] = x1;
        tri_y1[count] = y1;
        tri_x2[count] = x2;
        tri_y2[count] = y2;

        /* HSL16 colors: hue varies, mid saturation, mid lightness */
        tri_flat_color[count] = bench_rand() & 0xFFFF;
        tri_color0[count] = bench_rand() & 0xFFFF;
        tri_color1[count] = bench_rand() & 0xFFFF;
        tri_color2[count] = bench_rand() & 0xFFFF;

        /* Texture UV: place UV origin at vertex 0, U-end at vertex 1, V-end at vertex 2.
         * These are orthographic-space coordinates used by the perspective-correct
         * texture mapper. Use the screen positions directly with a small Z spread. */
        tri_ortho_uvorigin_x[count] = x0;
        tri_ortho_uvorigin_y[count] = y0;
        tri_ortho_uvorigin_z[count] = 200 + bench_rand_range(50, 400);

        tri_ortho_uend_x[count] = x1;
        tri_ortho_uend_y[count] = y1;
        tri_ortho_uend_z[count] = 200 + bench_rand_range(50, 400);

        tri_ortho_vend_x[count] = x2;
        tri_ortho_vend_y[count] = y2;
        tri_ortho_vend_z[count] = 200 + bench_rand_range(50, 400);

        tri_shade7bit[count] = bench_rand_range(16, 120);

        count++;
    }
}

/* ---------- Texture loading from cache ---------- */

static void
load_textures_from_cache(void)
{
    /* Try to find cache254 directory relative to executable / working directory */
    const char* cache_paths[] = {
        "cache254", "../cache254", "../../cache254", "../../../cache254", NULL
    };

    FILE* f = NULL;
    char dat_path[512];
    char idx_path[512];
    const char* found_cache = NULL;

    for( int i = 0; cache_paths[i]; i++ )
    {
        snprintf(dat_path, sizeof(dat_path), "%s/main_file_cache.dat", cache_paths[i]);
        f = fopen(dat_path, "rb");
        if( f )
        {
            fclose(f);
            found_cache = cache_paths[i];
            break;
        }
    }

    if( !found_cache )
    {
        printf("[benchmark] cache254 not found; using synthetic textures.\n");
        goto fallback;
    }

    printf("[benchmark] Found cache at: %s\n", found_cache);

    /* Load textures archive from cache_dat.
     * Table 0 = CONFIG, Archive 6 = TEXTURES. */
    {
        struct CacheDat* cache_dat = cache_dat_new_from_directory(found_cache);
        if( !cache_dat )
        {
            printf("[benchmark] Failed to open cache_dat; using synthetic textures.\n");
            goto fallback;
        }

        /* Load the textures archive: table CONFIGS(0), archive TEXTURES(6) */
        struct CacheDatArchive* archive =
            cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_TEXTURES);
        if( !archive || !archive->data || archive->data_size <= 0 )
        {
            printf("[benchmark] Failed to load texture archive; using synthetic textures.\n");
            cache_dat_free(cache_dat);
            goto fallback;
        }

        struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
        cache_dat_archive_free(archive);
        if( !filelist )
        {
            printf("[benchmark] Failed to decode texture filelist; using synthetic textures.\n");
            cache_dat_free(cache_dat);
            goto fallback;
        }

        /* Iterate textures 0-49, find first opaque and first transparent */
        int found_opaque = 0, found_trans = 0;

        for( int tex_id = 0; tex_id < 50 && (!found_opaque || !found_trans); tex_id++ )
        {
            struct CacheDatTexture* ct =
                cache_dat_texture_new_from_filelist_dat(filelist, tex_id, 0);
            if( !ct )
                continue;

            struct DashTexture* dtex = texture_new_from_texture_sprite(ct, 0 /* no animation */, 0);
            cache_dat_texture_free(ct);

            if( !dtex )
                continue;

            if( dtex->opaque && !found_opaque )
            {
                /* Copy texels into our static buffer */
                int copy_size = dtex->width * dtex->width;
                if( copy_size > TEX_WIDTH * TEX_WIDTH )
                    copy_size = TEX_WIDTH * TEX_WIDTH;
                memcpy(opaque_texels, dtex->texels, copy_size * sizeof(int));
                found_opaque = 1;
                printf(
                    "[benchmark] Loaded opaque texture ID %d (%dx%d)\n",
                    tex_id,
                    dtex->width,
                    dtex->width);
            }
            else if( !dtex->opaque && !found_trans )
            {
                int copy_size = dtex->width * dtex->width;
                if( copy_size > TEX_WIDTH * TEX_WIDTH )
                    copy_size = TEX_WIDTH * TEX_WIDTH;
                memcpy(transparent_texels, dtex->texels, copy_size * sizeof(int));
                found_trans = 1;
                printf(
                    "[benchmark] Loaded transparent texture ID %d (%dx%d)\n",
                    tex_id,
                    dtex->width,
                    dtex->width);
            }

            texture_free(dtex);
        }

        filelist_dat_free(filelist);
        cache_dat_free(cache_dat);

        if( found_opaque && found_trans )
        {
            have_cache_textures = 1;
            printf("[benchmark] Successfully loaded cache textures.\n");
            return;
        }

        printf("[benchmark] Could not find both opaque and transparent textures in cache.\n");
    }

fallback:
    /* Synthetic textures */
    printf("[benchmark] Generating synthetic textures.\n");
    for( int i = 0; i < TEX_WIDTH * TEX_WIDTH; i++ )
    {
        /* Opaque: solid colored pattern */
        int x = i % TEX_WIDTH;
        int y = i / TEX_WIDTH;
        int r = (x * 2) & 0xFF;
        int g = (y * 2) & 0xFF;
        int b = ((x + y) * 1) & 0xFF;
        opaque_texels[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;

        /* Transparent: checkerboard with alpha=0 every other 8x8 block */
        if( ((x >> 3) ^ (y >> 3)) & 1 )
            transparent_texels[i] = 0x00000000; /* fully transparent */
        else
            transparent_texels[i] = (0xFF << 24) | (b << 16) | (r << 8) | g;
    }
}

/* ---------- Run one benchmark variant ---------- */

static void
run_benchmark_variant(
    enum BenchVariant variant,
    int* texels)
{
    printf("  Running: %-30s", variant_names[variant]);
    fflush(stdout);

    /* Warmup */
    for( int f = 0; f < WARMUP_FRAMES; f++ )
    {
        memset(pixels, 0, sizeof(pixels));
        for( int i = 0; i < NUM_TRIS; i++ )
        {
            switch( variant )
            {
            case BENCH_FLAT_OPAQUE_SORT:
                raster_flat_screen_opaque_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i]);
                break;
            case BENCH_FLAT_OPAQUE_BRANCHING:
                raster_flat_screen_opaque_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i]);
                break;
            case BENCH_FLAT_ALPHA_SORT:
                raster_flat_screen_alpha_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_FLAT_ALPHA_BRANCHING:
                raster_flat_screen_alpha_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_OPAQUE_EDGE_SORT_S4:
                raster_gouraud_screen_opaque_edge_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_OPAQUE_BARY_BRANCHING_S4:
                raster_gouraud_screen_opaque_bary_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_OPAQUE_BARY_SORT_S4:
                raster_gouraud_screen_opaque_bary_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_OPAQUE_BARY_SORT_S1:
                raster_gouraud_screen_opaque_bary_sort_s1(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_ALPHA_EDGE_SORT_S4:
                raster_gouraud_screen_alpha_edge_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S4:
                raster_gouraud_screen_alpha_bary_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_ALPHA_BARY_SORT_S4:
                raster_gouraud_screen_alpha_bary_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_ALPHA_BARY_SORT_S1:
                raster_gouraud_screen_alpha_bary_sort_s1(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_TEXFLAT_PERSP_OPAQUE_SORT:
                raster_texshadeflat_persp_texopaque_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXFLAT_PERSP_OPAQUE_BRANCHING:
                raster_texshadeflat_persp_texopaque_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXFLAT_PERSP_TRANS_SORT:
                raster_texshadeflat_persp_textrans_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXFLAT_PERSP_TRANS_BRANCHING:
                raster_texshadeflat_persp_textrans_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING:
                raster_texshadeblend_persp_texopaque_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_TRANS_BRANCHING:
                raster_texshadeblend_persp_textrans_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING_V3:
                raster_texshadeblend_persp_texopaque_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_TRANS_BRANCHING_V3:
                raster_texshadeblend_persp_textrans_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_OPAQUE_SORT:
                raster_texshadeblend_persp_texopaque_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_TRANS_SORT:
                raster_texshadeblend_persp_textrans_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING:
                raster_texshadeblend_affine_texopaque_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING:
                raster_texshadeblend_affine_textrans_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING_V3:
                raster_texshadeblend_affine_texopaque_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING_V3:
                raster_texshadeblend_affine_textrans_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_FLAT_DEOB:
                pix3d_deob_flat_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    g_hsl16_to_rgb_table[tri_flat_color[i] & 0xFFFF]);
                break;
            case BENCH_GOURAUD_DEOB:
                pix3d_deob_gouraud_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_TEX_OPAQUE_DEOB:
                pix3d_deob_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    1);
                break;
            case BENCH_TEX_TRANS_DEOB:
                pix3d_deob_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    0);
                break;
            case BENCH_TEX_OPAQUE_DEOB2:
                pix3d_deob2_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    1);
                break;
            case BENCH_TEX_TRANS_DEOB2:
                pix3d_deob2_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    0);
                break;
            default:
                break;
            }
        }
    }

    /* Timed frames */
    Uint64 freq = SDL_GetPerformanceFrequency();
    for( int f = 0; f < NUM_FRAMES; f++ )
    {
        memset(pixels, 0, sizeof(pixels));

        Uint64 t0 = SDL_GetPerformanceCounter();
        for( int i = 0; i < NUM_TRIS; i++ )
        {
            switch( variant )
            {
            case BENCH_FLAT_OPAQUE_SORT:
                raster_flat_screen_opaque_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i]);
                break;
            case BENCH_FLAT_OPAQUE_BRANCHING:
                raster_flat_screen_opaque_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i]);
                break;
            case BENCH_FLAT_ALPHA_SORT:
                raster_flat_screen_alpha_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_FLAT_ALPHA_BRANCHING:
                raster_flat_screen_alpha_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_flat_color[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_OPAQUE_EDGE_SORT_S4:
                raster_gouraud_screen_opaque_edge_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_OPAQUE_BARY_BRANCHING_S4:
                raster_gouraud_screen_opaque_bary_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_OPAQUE_BARY_SORT_S4:
                raster_gouraud_screen_opaque_bary_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_OPAQUE_BARY_SORT_S1:
                raster_gouraud_screen_opaque_bary_sort_s1(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_GOURAUD_ALPHA_EDGE_SORT_S4:
                raster_gouraud_screen_alpha_edge_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S4:
                raster_gouraud_screen_alpha_bary_branching_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_ALPHA_BARY_SORT_S4:
                raster_gouraud_screen_alpha_bary_sort_s4(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_GOURAUD_ALPHA_BARY_SORT_S1:
                raster_gouraud_screen_alpha_bary_sort_s1(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    BENCH_ALPHA7);
                break;
            case BENCH_TEXFLAT_PERSP_OPAQUE_SORT:
                raster_texshadeflat_persp_texopaque_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXFLAT_PERSP_OPAQUE_BRANCHING:
                raster_texshadeflat_persp_texopaque_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXFLAT_PERSP_TRANS_SORT:
                raster_texshadeflat_persp_textrans_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXFLAT_PERSP_TRANS_BRANCHING:
                raster_texshadeflat_persp_textrans_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING:
                raster_texshadeblend_persp_texopaque_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_TRANS_BRANCHING:
                raster_texshadeblend_persp_textrans_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING_V3:
                raster_texshadeblend_persp_texopaque_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_TRANS_BRANCHING_V3:
                raster_texshadeblend_persp_textrans_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_OPAQUE_SORT:
                raster_texshadeblend_persp_texopaque_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_PERSP_TRANS_SORT:
                raster_texshadeblend_persp_textrans_sort_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING:
                raster_texshadeblend_affine_texopaque_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING:
                raster_texshadeblend_affine_textrans_branching_lerp8(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING_V3:
                raster_texshadeblend_affine_texopaque_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING_V3:
                raster_texshadeblend_affine_textrans_branching_lerp8_v3(
                    pixels,
                    SCREEN_W,
                    SCREEN_W,
                    SCREEN_H,
                    CAMERA_FOV,
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    tri_shade7bit[i],
                    texels,
                    TEX_WIDTH);
                break;
            case BENCH_FLAT_DEOB:
                pix3d_deob_flat_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    g_hsl16_to_rgb_table[tri_flat_color[i] & 0xFFFF]);
                break;
            case BENCH_GOURAUD_DEOB:
                pix3d_deob_gouraud_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i]);
                break;
            case BENCH_TEX_OPAQUE_DEOB:
                pix3d_deob_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    1);
                break;
            case BENCH_TEX_TRANS_DEOB:
                pix3d_deob_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    0);
                break;
            case BENCH_TEX_OPAQUE_DEOB2:
                pix3d_deob2_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    1);
                break;
            case BENCH_TEX_TRANS_DEOB2:
                pix3d_deob2_texture_triangle(
                    tri_x0[i],
                    tri_x1[i],
                    tri_x2[i],
                    tri_y0[i],
                    tri_y1[i],
                    tri_y2[i],
                    tri_color0[i],
                    tri_color1[i],
                    tri_color2[i],
                    tri_ortho_uvorigin_x[i],
                    tri_ortho_uvorigin_y[i],
                    tri_ortho_uvorigin_z[i],
                    tri_ortho_uend_x[i],
                    tri_ortho_vend_x[i],
                    tri_ortho_uend_y[i],
                    tri_ortho_vend_y[i],
                    tri_ortho_uend_z[i],
                    tri_ortho_vend_z[i],
                    texels,
                    0);
                break;
            default:
                break;
            }
        }
        Uint64 t1 = SDL_GetPerformanceCounter();

        frame_times[variant][f] = (double)(t1 - t0) / (double)freq * 1000.0; /* ms */
    }

    /* Prevent dead code elimination */
    volatile int sink = pixels[SCREEN_W / 2 + (SCREEN_H / 2) * SCREEN_W];
    (void)sink;

    /* Compute stats */
    double sum = 0.0;
    double mn = frame_times[variant][0];
    double mx = frame_times[variant][0];
    for( int f = 0; f < NUM_FRAMES; f++ )
    {
        sum += frame_times[variant][f];
        if( frame_times[variant][f] < mn )
            mn = frame_times[variant][f];
        if( frame_times[variant][f] > mx )
            mx = frame_times[variant][f];
    }
    avg_ms[variant] = sum / NUM_FRAMES;
    min_ms[variant] = mn;
    max_ms[variant] = mx;

    printf(" avg=%.2f ms  min=%.2f  max=%.2f\n", avg_ms[variant], mn, mx);
}

/* ---------- NK results display ---------- */

static void
draw_nk_results(struct nk_context* nk)
{
    if( nk_begin(
            nk,
            "Benchmark Results",
            nk_rect(10, 10, 780, 1180),
            NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                NK_WINDOW_MINIMIZABLE) )
    {
        /* Config info */
        nk_layout_row_dynamic(nk, 20, 1);
        nk_labelf(
            nk,
            NK_TEXT_LEFT,
            "Config: %d triangles, %dx%d, %d frames, fov=%d, tex=%dx%d%s",
            NUM_TRIS,
            SCREEN_W,
            SCREEN_H,
            NUM_FRAMES,
            CAMERA_FOV,
            TEX_WIDTH,
            TEX_WIDTH,
            have_cache_textures ? " [cache]" : " [synthetic]");

        nk_layout_row_dynamic(nk, 8, 1);
        nk_spacing(nk, 1);

        /* Find slowest for progress bar scaling */
        double slowest = 0.0;
        for( int v = 0; v < BENCH_VARIANT_COUNT; v++ )
            if( avg_ms[v] > slowest )
                slowest = avg_ms[v];
        if( slowest < 0.001 )
            slowest = 1.0;

        int last_cat = -1;
        for( int v = 0; v < BENCH_VARIANT_COUNT; v++ )
        {
            /* Section header */
            if( variant_category[v] != last_cat )
            {
                last_cat = variant_category[v];
                nk_layout_row_dynamic(nk, 8, 1);
                nk_spacing(nk, 1);
                nk_layout_row_dynamic(nk, 20, 1);
                nk_labelf(nk, NK_TEXT_LEFT, "--- %s ---", category_names[last_cat]);
            }

            /* Variant name */
            nk_layout_row_dynamic(nk, 18, 1);
            nk_labelf(nk, NK_TEXT_LEFT, "%s", variant_names[v]);

            /* Timing row: avg | min | max | tri/sec */
            nk_layout_row_dynamic(nk, 18, 4);
            nk_labelf(nk, NK_TEXT_LEFT, "avg: %.2f ms", avg_ms[v]);
            nk_labelf(nk, NK_TEXT_LEFT, "min: %.2f ms", min_ms[v]);
            nk_labelf(nk, NK_TEXT_LEFT, "max: %.2f ms", max_ms[v]);
            {
                double tris_per_sec = 0.0;
                if( avg_ms[v] > 0.0001 )
                    tris_per_sec = (double)NUM_TRIS / (avg_ms[v] / 1000.0);
                if( tris_per_sec > 1e6 )
                    nk_labelf(nk, NK_TEXT_LEFT, "%.1f Mtri/s", tris_per_sec / 1e6);
                else
                    nk_labelf(nk, NK_TEXT_LEFT, "%.0f Ktri/s", tris_per_sec / 1e3);
            }

            /* Progress bar (relative to slowest) */
            nk_layout_row_dynamic(nk, 14, 1);
            {
                nk_size prog = (nk_size)(avg_ms[v] / slowest * 1000.0);
                if( prog > 1000 )
                    prog = 1000;
                nk_progress(nk, &prog, 1000, NK_FIXED);
            }
        }
    }
    nk_end(nk);
}

/* ---------- Main ---------- */

int
main(
    int argc,
    char* argv[])
{
    (void)argc;
    (void)argv;

    printf("=== Rasterizer Benchmark Suite ===\n");
    printf(
        "Triangles: %d | Resolution: %dx%d | Frames: %d\n\n",
        NUM_TRIS,
        SCREEN_W,
        SCREEN_H,
        NUM_FRAMES);

    /* Initialize shared tables */
    printf("[init] Shared tables...\n");
    init_hsl16_to_rgb_table();
    init_reciprocal16();
    init_sin_table();
    init_cos_table();
    init_tan_table();

    /* Generate test triangles */
    printf("[init] Generating %d triangles...\n", NUM_TRIS);
    generate_triangles();

    /* Load textures */
    printf("[init] Loading textures...\n");
    load_textures_from_cache();

    /* Initialize SDL2 */
    printf("[init] SDL2...\n");
    if( SDL_Init(SDL_INIT_VIDEO) != 0 )
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Rasterizer Benchmark",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_W,
        SCREEN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !window )
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if( !renderer )
    {
        /* Fallback to software renderer (XP compatibility) */
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if( !renderer )
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Create streaming texture for pixel buffer display */
    SDL_Texture* sdl_texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if( !sdl_texture )
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Initialize Nuklear */
    printf("[init] Nuklear...\n");
    struct nk_context* nk = torirs_nk_sdlren_init(window, renderer);
    {
        struct nk_font_atlas* atlas = NULL;
        torirs_nk_sdlren_font_stash_begin(&atlas);
        nk_font_atlas_add_default(atlas, 13.0f, NULL);
        torirs_nk_sdlren_font_stash_end();
    }

    pix3d_deob_set_clipping(pixels, SCREEN_W, SCREEN_H, SCREEN_W, SCREEN_H);
    pix3d_deob_set_alpha(0);
    pix3d_deob_set_hclip(1);
    pix3d_deob_set_low_detail(1);
    pix3d_deob_set_low_mem(0);

    /* ===== Run benchmarks ===== */
    printf("\n=== Running benchmarks ===\n\n");

    printf("[Flat]\n");
    run_benchmark_variant(BENCH_FLAT_OPAQUE_SORT, NULL);
    run_benchmark_variant(BENCH_FLAT_OPAQUE_BRANCHING, NULL);
    run_benchmark_variant(BENCH_FLAT_ALPHA_SORT, NULL);
    run_benchmark_variant(BENCH_FLAT_ALPHA_BRANCHING, NULL);

    printf("\n[Gouraud]\n");
    run_benchmark_variant(BENCH_GOURAUD_OPAQUE_EDGE_SORT_S4, NULL);
    run_benchmark_variant(BENCH_GOURAUD_OPAQUE_BARY_BRANCHING_S4, NULL);
    run_benchmark_variant(BENCH_GOURAUD_OPAQUE_BARY_SORT_S4, NULL);
    run_benchmark_variant(BENCH_GOURAUD_OPAQUE_BARY_SORT_S1, NULL);
    run_benchmark_variant(BENCH_GOURAUD_ALPHA_EDGE_SORT_S4, NULL);
    run_benchmark_variant(BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S4, NULL);
    run_benchmark_variant(BENCH_GOURAUD_ALPHA_BARY_SORT_S4, NULL);
    run_benchmark_variant(BENCH_GOURAUD_ALPHA_BARY_SORT_S1, NULL);

    printf("\n[Texture Flat Persp — opaque]\n");
    run_benchmark_variant(BENCH_TEXFLAT_PERSP_OPAQUE_SORT, opaque_texels);
    run_benchmark_variant(BENCH_TEXFLAT_PERSP_OPAQUE_BRANCHING, opaque_texels);

    printf("\n[Texture Flat Persp — transparent]\n");
    run_benchmark_variant(BENCH_TEXFLAT_PERSP_TRANS_SORT, transparent_texels);
    run_benchmark_variant(BENCH_TEXFLAT_PERSP_TRANS_BRANCHING, transparent_texels);

    printf("\n[Texture Blend Persp — opaque]\n");
    run_benchmark_variant(BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING, opaque_texels);
    run_benchmark_variant(BENCH_TEXBLEND_PERSP_OPAQUE_BRANCHING_V3, opaque_texels);
    run_benchmark_variant(BENCH_TEXBLEND_PERSP_OPAQUE_SORT, opaque_texels);

    printf("\n[Texture Blend Persp — transparent]\n");
    run_benchmark_variant(BENCH_TEXBLEND_PERSP_TRANS_BRANCHING, transparent_texels);
    run_benchmark_variant(BENCH_TEXBLEND_PERSP_TRANS_BRANCHING_V3, transparent_texels);
    run_benchmark_variant(BENCH_TEXBLEND_PERSP_TRANS_SORT, transparent_texels);

    printf("\n[Texture Blend Affine — opaque]\n");
    run_benchmark_variant(BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING, opaque_texels);
    run_benchmark_variant(BENCH_TEXBLEND_AFFINE_OPAQUE_BRANCHING_V3, opaque_texels);

    printf("\n[Texture Blend Affine — transparent]\n");
    run_benchmark_variant(BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING, transparent_texels);
    run_benchmark_variant(BENCH_TEXBLEND_AFFINE_TRANS_BRANCHING_V3, transparent_texels);

    printf("\n[Deob — Pix3D reference ports]\n");
    run_benchmark_variant(BENCH_FLAT_DEOB, NULL);
    run_benchmark_variant(BENCH_GOURAUD_DEOB, NULL);
    run_benchmark_variant(BENCH_TEX_OPAQUE_DEOB, opaque_texels);
    run_benchmark_variant(BENCH_TEX_TRANS_DEOB, transparent_texels);
    run_benchmark_variant(BENCH_TEX_OPAQUE_DEOB2, opaque_texels);
    run_benchmark_variant(BENCH_TEX_TRANS_DEOB2, transparent_texels);

    printf("\n=== Benchmarks complete. Displaying results. ===\n");
    printf("Close the window or press ESC to exit.\n\n");

    /* ===== Display loop ===== */
    int running = 1;
    while( running )
    {
        /* Poll events */
        SDL_Event evt;
        nk_input_begin(nk);
        while( SDL_PollEvent(&evt) )
        {
            if( evt.type == SDL_QUIT )
                running = 0;
            if( evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE )
                running = 0;
            torirs_nk_sdlren_handle_event(&evt);
        }
        nk_input_end(nk);

        /* Upload last rendered pixel buffer to SDL texture */
        {
            void* tex_pixels = NULL;
            int tex_pitch = 0;
            SDL_LockTexture(sdl_texture, NULL, &tex_pixels, &tex_pitch);
            int tex_w = tex_pitch / (int)sizeof(int);
            for( int y = 0; y < SCREEN_H; y++ )
            {
                memcpy((int*)tex_pixels + y * tex_w, pixels + y * SCREEN_W, SCREEN_W * sizeof(int));
            }
            SDL_UnlockTexture(sdl_texture);
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, sdl_texture, NULL, NULL);

        /* Draw NK results overlay */
        draw_nk_results(nk);
        torirs_nk_sdlren_render(NK_ANTI_ALIASING_ON);

        SDL_RenderPresent(renderer);
    }

    /* Cleanup */
    pix3d_deob_free();
    torirs_nk_sdlren_shutdown();
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
