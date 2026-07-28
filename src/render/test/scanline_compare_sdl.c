/*
 * Side-by-side viewer for the `branching` and `scanline` raster families.
 *
 * Loads a real model (with textures and per-face alpha) out of a dat1 cache,
 * renders it twice per frame - left pane with the default kernels, right pane
 * with the scanline family - and shows a third pane with the amplified
 * absolute difference so any divergence is immediately visible.
 *
 * Run from src/:
 *   make scanline-compare [DAT1_CACHE=../cache254]
 *   ./build/scanline_compare ../cache254 [model_id]
 *
 * With no model id it scans the cache for the model with the most textured
 * and alpha-blended faces, which is the one worth looking at. In cache254 that
 * is model 148 (1004 faces, 48 textured, 420 alpha).
 *
 * Environment:
 *   TORIRS_SCANLINE_HEADLESS=N   render N turntable frames with no window,
 *                                print the per-frame pixel difference for each
 *                                alpha mode, dump BMPs, and exit. Use this to
 *                                check the two families agree without a display.
 *   TORIRS_SCANLINE_BMP=prefix   BMP output prefix (default build/scanline).
 *   TORIRS_SCANLINE_SCAN=N       how many model ids to sweep (default 4000).
 *
 * Keys:
 *   left / right   previous / next model in the scanned candidate list
 *   space          pause / resume the turntable
 *   a              cycle a synthetic per-face alpha over the model
 *                  (0xFF -> 0xC0 -> 0x80 -> 0x40), so the alpha kernels are
 *                  exercised even on models whose faces are all opaque
 *   d              toggle the difference pane between amplified and raw
 *   escape / q     quit
 */

#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_types.h"
#include "task_runner.h"

#include "toridraw.h"

#include <SDL.h>
#include <bmp.h>

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANE_W 420
#define PANE_H 480
#define PANE_COUNT 3
#define WIN_W (PANE_W * PANE_COUNT)
#define WIN_H PANE_H

#define MAX_CANDIDATES 64
#define SCAN_MODEL_LIMIT_DEFAULT 4000

static int g_scan_limit = SCAN_MODEL_LIMIT_DEFAULT;

struct Candidate
{
    int model_id;
    int textured_faces;
    int alpha_faces;
    int face_count;
};

struct Viewer
{
    struct TaskRunner runner;
    struct Dat1BuildCache* bc;
    struct CacheProvider* provider;
    struct ToriDraw_Scene* scene;

    struct Candidate candidates[MAX_CANDIDATES];
    int candidate_count;
    int candidate_index;

    struct ToriDraw_Model* model;
    int model_id;

    int* pane_branching;
    int* pane_scanline;
    int* pane_diff;

    int yaw;
    int paused;
    int alpha_mode;
    int diff_amplify;
};

static const int k_alpha_modes[] = { 0xFF, 0xC0, 0x80, 0x40 };
#define ALPHA_MODE_COUNT ((int)(sizeof(k_alpha_modes) / sizeof(k_alpha_modes[0])))

/* ---------------------------------------------------------------- loading */

static void
run_task(
    struct Viewer* v,
    struct ToriRS_Task* task)
{
    if( !task )
        return;
    ToriRS_TaskQueue_Add(v->runner.queue, task);
    TaskRunner_Drain(&v->runner);
}

/**
 * Upload every texture the model's faces name into the scene texture map. The
 * raster reads only the scene map, and skips any face whose texture is absent,
 * so this has to happen before the first render or the textured faces vanish.
 */
static int
publish_model_textures(
    struct Viewer* v,
    const struct ToriRS_Model* rs_model)
{
    struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(v->scene);
    int published = 0;

    if( !rs_model || !rs_model->face_textures || !tex_state )
        return 0;

    for( int f = 0; f < rs_model->face_count; f++ )
    {
        int texture_id = (int)rs_model->face_textures[f];
        if( texture_id < 0 || texture_id >= 2048 )
            continue;
        if( tex_state->texture_map.textures[texture_id] )
            continue;

        if( !CacheProvider_TextureHas(v->provider, texture_id) )
            run_task(v, CreateTask_Dat1TextureLoad(v->provider, texture_id));

        struct ToriRS_Texture* rs = CacheProvider_TextureGet(v->provider, texture_id);
        if( !rs || !rs->texels || rs->width <= 0 || rs->height <= 0 )
            continue;

        struct ToriDraw_Texture* texture = calloc(1, sizeof(*texture));
        if( !texture )
            continue;
        size_t texel_bytes = (size_t)rs->width * (size_t)rs->height * sizeof(int);
        texture->texels = malloc(texel_bytes);
        if( !texture->texels )
        {
            free(texture);
            continue;
        }
        memcpy(texture->texels, rs->texels, texel_bytes);
        texture->width = rs->width;
        texture->height = rs->height;
        texture->opaque = rs->opaque;
        texture->animation_direction = rs->animation_direction;
        texture->animation_speed = rs->animation_speed;

        ToriDraw_SceneSetTexture(v->scene, texture_id, texture);
        published++;
    }

    return published;
}

static int
candidate_cmp(
    const void* pa,
    const void* pb)
{
    const struct Candidate* a = pa;
    const struct Candidate* b = pb;
    /* Prefer models that exercise both the texture and the alpha kernels. */
    int sa = a->textured_faces + 4 * a->alpha_faces;
    int sb = b->textured_faces + 4 * b->alpha_faces;
    if( sa != sb )
        return sb - sa;
    return b->textured_faces - a->textured_faces;
}

/**
 * Sweep the cache for models worth comparing: a model is interesting when it
 * has textured faces, and more interesting when it also has non-opaque ones.
 */
static void
scan_candidates(struct Viewer* v)
{
    struct Candidate found[MAX_CANDIDATES * 4];
    int found_count = 0;

    printf("scanning up to %d models for textures/alpha...\n", g_scan_limit);

    for( int id = 0; id < g_scan_limit && found_count < (int)(sizeof(found) / sizeof(found[0]));
         id++ )
    {
        if( !CacheProvider_ModelHas(v->provider, id) )
            run_task(v, CreateTask_Dat1ModelLoad(v->provider, id));

        struct ToriRS_Model* m = CacheProvider_ModelGet(v->provider, id);
        if( !m || m->face_count <= 0 )
            continue;

        int textured = 0;
        int alpha = 0;
        for( int f = 0; f < m->face_count; f++ )
        {
            if( m->face_textures && (int)m->face_textures[f] >= 0 )
                textured++;
            if( m->face_alphas && m->face_alphas[f] != 0 )
                alpha++;
        }

        if( textured == 0 )
            continue;

        found[found_count].model_id = id;
        found[found_count].textured_faces = textured;
        found[found_count].alpha_faces = alpha;
        found[found_count].face_count = m->face_count;
        found_count++;
    }

    qsort(found, (size_t)found_count, sizeof(found[0]), candidate_cmp);

    v->candidate_count = found_count < MAX_CANDIDATES ? found_count : MAX_CANDIDATES;
    memcpy(v->candidates, found, sizeof(found[0]) * (size_t)v->candidate_count);

    printf("found %d textured models; top candidates:\n", found_count);
    for( int i = 0; i < v->candidate_count && i < 8; i++ )
        printf(
            "  model %5d: %4d faces, %4d textured, %4d alpha\n",
            v->candidates[i].model_id,
            v->candidates[i].face_count,
            v->candidates[i].textured_faces,
            v->candidates[i].alpha_faces);
}

/**
 * Build the renderer-side model for `model_id`, applying the current synthetic
 * alpha mode. A mode below 0xFF overwrites every face alpha so the alpha and
 * facealpha kernels get exercised even on fully opaque geometry.
 */
static int
load_model(
    struct Viewer* v,
    int model_id)
{
    if( !CacheProvider_ModelHas(v->provider, model_id) )
        run_task(v, CreateTask_Dat1ModelLoad(v->provider, model_id));

    struct ToriRS_Model* rs = CacheProvider_ModelGet(v->provider, model_id);
    if( !rs )
    {
        fprintf(stderr, "model %d not available\n", model_id);
        return 0;
    }

    publish_model_textures(v, rs);

    if( v->model )
    {
        ToriDraw_ModelFree(v->model);
        v->model = NULL;
    }

    v->model = ToriDraw_ModelFromToriRS(rs);
    if( !v->model )
    {
        fprintf(stderr, "failed to convert model %d\n", model_id);
        return 0;
    }
    v->model_id = model_id;

    int alpha = k_alpha_modes[v->alpha_mode];
    if( alpha != 0xFF && v->model->face_alphas )
    {
        /* Face alpha is stored inverted for untextured faces (the raster does
         * 0xFF - alpha), so write the inverse of what we want to see. */
        for( int f = 0; f < v->model->face_count; f++ )
            v->model->face_alphas[f] = (alphaint_t)(0xFF - alpha);
    }

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = v->model;
    ToriDraw_LightModelDefault(hnd, 64, 850);

    printf(
        "model %d: %d faces, alpha mode 0x%02X\n", model_id, v->model->face_count, alpha);
    return 1;
}

/* --------------------------------------------------------------- drawing */

static void
render_pane(
    struct Viewer* v,
    int* pane,
    int use_scanline)
{
    for( int i = 0; i < PANE_W * PANE_H; i++ )
        pane[i] = 0x00202028;

    if( !v->model )
        return;

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = v->model;

    struct ToriDraw_ViewPort viewport = { 0 };
    viewport.width = PANE_W;
    viewport.height = PANE_H;
    viewport.stride = PANE_W;
    viewport.x_center = PANE_W / 2;
    viewport.y_center = PANE_H / 2;

    struct ToriDraw_Camera camera = { 0 };
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 50;

    struct ToriDraw_Position position = { 0 };
    position.x = 0;
    position.y = 0;
    position.z = 900;
    position.yaw = v->yaw & 2047;

    ToriDraw_RasterSetScanline(use_scanline != 0);
    ToriDraw_RenderModel(hnd, v->scene, &position, &viewport, &camera, pane);
    ToriDraw_RasterSetScanline(false);
}

static int
render_diff(struct Viewer* v)
{
    int differing = 0;

    for( int i = 0; i < PANE_W * PANE_H; i++ )
    {
        int a = v->pane_branching[i];
        int b = v->pane_scanline[i];

        if( a == b )
        {
            v->pane_diff[i] = 0x00101010;
            continue;
        }
        differing++;

        int out = 0;
        for( int ch = 0; ch < 3; ch++ )
        {
            int d = ((a >> (ch * 8)) & 0xFF) - ((b >> (ch * 8)) & 0xFF);
            if( d < 0 )
                d = -d;
            if( v->diff_amplify )
            {
                d *= 16;
                if( d > 255 )
                    d = 255;
            }
            out |= d << (ch * 8);
        }
        v->pane_diff[i] = out;
    }

    return differing;
}

static void
blit_panes(
    struct Viewer* v,
    SDL_Texture* texture)
{
    int* pixels = NULL;
    int pitch = 0;

    if( SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch) != 0 )
        return;

    int stride = pitch / 4;
    const int* panes[PANE_COUNT] = { v->pane_branching, v->pane_scanline, v->pane_diff };

    for( int y = 0; y < PANE_H; y++ )
    {
        for( int p = 0; p < PANE_COUNT; p++ )
        {
            const int* src = panes[p] + y * PANE_W;
            int* dst = pixels + y * stride + p * PANE_W;
            for( int x = 0; x < PANE_W; x++ )
                dst[x] = 0xFF000000u | (unsigned)src[x];
        }
        /* One-pixel separators between panes. */
        for( int p = 1; p < PANE_COUNT; p++ )
            pixels[y * stride + p * PANE_W] = 0xFF404050u;
    }

    SDL_UnlockTexture(texture);
}

/**
 * No-window mode: sweep the turntable through `frames` yaw steps for every
 * alpha mode, tally how far the two families diverge, and dump the last frame
 * of each mode as a BMP triptych.
 *
 * Returns non-zero if any frame diverged beyond the sanctioned clip-boundary
 * columns, so this can be wired into a check.
 */
static int
run_headless(
    struct Viewer* v,
    int frames,
    const char* bmp_prefix)
{
    int worst_overall = 0;

    for( int mode = 0; mode < ALPHA_MODE_COUNT; mode++ )
    {
        v->alpha_mode = mode;
        if( !load_model(v, v->model_id) )
            return 1;

        long total_diff = 0;
        long total_drawn = 0;
        int worst = 0;

        for( int f = 0; f < frames; f++ )
        {
            v->yaw = (f * 2048) / (frames > 0 ? frames : 1);

            render_pane(v, v->pane_branching, 0);
            render_pane(v, v->pane_scanline, 1);
            int differing = render_diff(v);

            int drawn = 0;
            for( int i = 0; i < PANE_W * PANE_H; i++ )
                if( v->pane_branching[i] != 0x00202028 )
                    drawn++;

            total_diff += differing;
            total_drawn += drawn;
            if( differing > worst )
                worst = differing;
        }

        printf(
            "alpha 0x%02X over %d yaw steps: %ld differing / %ld drawn pixels (%.4f%%), "
            "worst frame %d\n",
            k_alpha_modes[mode],
            frames,
            total_diff,
            total_drawn,
            total_drawn ? 100.0 * (double)total_diff / (double)total_drawn : 0.0,
            worst);

        if( worst > worst_overall )
            worst_overall = worst;

        char path[512];
        int* triptych = malloc(sizeof(int) * WIN_W * PANE_H);
        if( triptych )
        {
            const int* panes[PANE_COUNT] = {
                v->pane_branching, v->pane_scanline, v->pane_diff
            };
            for( int y = 0; y < PANE_H; y++ )
                for( int p = 0; p < PANE_COUNT; p++ )
                    memcpy(
                        triptych + (y * WIN_W) + (p * PANE_W),
                        panes[p] + (y * PANE_W),
                        sizeof(int) * PANE_W);

            snprintf(
                path,
                sizeof(path),
                "%s_model%d_alpha%02X.bmp",
                bmp_prefix,
                v->model_id,
                k_alpha_modes[mode]);
            bmp_write_file(path, triptych, WIN_W, PANE_H);
            printf("  wrote %s\n", path);
            free(triptych);
        }
    }

    return worst_overall != 0;
}

/* ------------------------------------------------------------------ main */

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "../cache254";
    int forced_model = argc > 2 ? atoi(argv[2]) : -1;

    const char* headless_env = getenv("TORIRS_SCANLINE_HEADLESS");
    int headless_frames = headless_env ? atoi(headless_env) : 0;
    const char* bmp_prefix = getenv("TORIRS_SCANLINE_BMP");
    if( !bmp_prefix )
        bmp_prefix = "build/scanline";
    const char* scan_env = getenv("TORIRS_SCANLINE_SCAN");
    if( scan_env && atoi(scan_env) > 0 )
        g_scan_limit = atoi(scan_env);

    ToriDraw_Init();

    struct RSCache_Dat1Disk* disk = RSCache_Dat1DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "dat1 cache not found at %s\n", cache_dir);
        return 1;
    }

    struct Viewer v;
    memset(&v, 0, sizeof(v));
    v.diff_amplify = 1;

    v.runner.io = ToriRS_IO_New();
    v.runner.queue = ToriRS_TaskQueue_New();
    v.runner.px = PlatformX_IO_New();
    assert(v.runner.io && v.runner.queue && v.runner.px);
    PlatformX_IO_InitDat1Disk(v.runner.px, disk);

    v.bc = dat1_buildcache_new();
    v.provider = dat1_buildcache_as_provider(v.bc);
    v.scene = ToriDraw_SceneNew(0);
    assert(v.scene);

    v.pane_branching = malloc(sizeof(int) * PANE_W * PANE_H);
    v.pane_scanline = malloc(sizeof(int) * PANE_W * PANE_H);
    v.pane_diff = malloc(sizeof(int) * PANE_W * PANE_H);
    assert(v.pane_branching && v.pane_scanline && v.pane_diff);

    if( forced_model >= 0 )
    {
        v.candidates[0].model_id = forced_model;
        v.candidate_count = 1;
    }
    else
    {
        scan_candidates(&v);
        if( v.candidate_count == 0 )
        {
            fprintf(stderr, "no textured models found in %s\n", cache_dir);
            return 1;
        }
    }

    if( !load_model(&v, v.candidates[0].model_id) )
        return 1;

    if( headless_frames > 0 )
        return run_headless(&v, headless_frames, bmp_prefix);

    if( SDL_Init(SDL_INIT_VIDEO) != 0 )
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "toridraw raster compare - left: branching   middle: scanline   right: diff x16",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIN_W,
        WIN_H,
        SDL_WINDOW_SHOWN);
    if( !window )
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if( !renderer )
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if( !renderer )
    {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIN_W, WIN_H);
    if( !texture )
    {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return 1;
    }

    printf(
        "\nkeys: left/right = model, space = pause, a = alpha mode, d = diff scale, q = quit\n\n");

    int running = 1;
    while( running )
    {
        SDL_Event ev;
        while( SDL_PollEvent(&ev) )
        {
            if( ev.type == SDL_QUIT )
                running = 0;
            if( ev.type != SDL_KEYDOWN )
                continue;

            switch( ev.key.keysym.sym )
            {
            case SDLK_ESCAPE:
            case SDLK_q:
                running = 0;
                break;
            case SDLK_SPACE:
                v.paused = !v.paused;
                break;
            case SDLK_d:
                v.diff_amplify = !v.diff_amplify;
                printf("diff scale: %s\n", v.diff_amplify ? "x16" : "raw");
                break;
            case SDLK_a:
                v.alpha_mode = (v.alpha_mode + 1) % ALPHA_MODE_COUNT;
                load_model(&v, v.model_id);
                break;
            case SDLK_LEFT:
                if( v.candidate_count > 0 )
                {
                    v.candidate_index =
                        (v.candidate_index + v.candidate_count - 1) % v.candidate_count;
                    load_model(&v, v.candidates[v.candidate_index].model_id);
                }
                break;
            case SDLK_RIGHT:
                if( v.candidate_count > 0 )
                {
                    v.candidate_index = (v.candidate_index + 1) % v.candidate_count;
                    load_model(&v, v.candidates[v.candidate_index].model_id);
                }
                break;
            default:
                break;
            }
        }

        if( !v.paused )
            v.yaw = (v.yaw + 8) & 2047;

        render_pane(&v, v.pane_branching, 0);
        render_pane(&v, v.pane_scanline, 1);
        render_diff(&v);
        blit_panes(&v, texture);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
