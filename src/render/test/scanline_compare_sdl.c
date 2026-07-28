/*
 * Side-by-side viewer for the `branching` and `scanline` raster families.
 *
 * Loads a real model (with textures and per-face alpha) out of a dat1 cache and
 * draws it twice per frame into two panels of one window: left with the default
 * kernels, right with the scanline family. Both panels share the same camera, so
 * every rotate/zoom key moves the model in both at once and the two images stay
 * directly comparable. An optional third panel shows the amplified absolute
 * difference.
 *
 * Run from src/:
 *   make scanline-compare [DAT1_CACHE=../cache254]
 *   ./build/scanline_compare ../cache254 148
 *
 * Pass the model id: with none it sweeps the cache for the model with the most
 * textured and alpha-blended faces, which takes a couple of minutes. In
 * cache254 that model is 148 (1004 faces, 48 textured, 420 alpha); 597, 598,
 * 1178 and 1199 are also worth a look.
 *
 * Keys:
 *   left / right     rotate the model (yaw)
 *   up / down        tilt the camera (pitch)
 *   + / -            zoom in / out
 *   [ / ]            previous / next model from the scanned candidate list
 *   space            toggle the auto-turntable
 *   a                cycle a synthetic per-face alpha over the model
 *                    (0xFF -> 0xC0 -> 0x80 -> 0x40), so the alpha kernels are
 *                    exercised even on models whose faces are all opaque
 *   d                show / hide the difference panel
 *   x                toggle difference amplification (x16 or raw)
 *   r                reset view
 *   escape / q       quit
 *
 * Environment:
 *   TORIRS_SCANLINE_HEADLESS=N   render N turntable frames with no window,
 *                                print the per-frame pixel difference for each
 *                                alpha mode, dump BMPs, and exit.
 *   TORIRS_SCANLINE_BMP=prefix   BMP output prefix (default build/scanline).
 *   TORIRS_SCANLINE_SCAN=N       how many model ids to sweep (default 4000).
 */

#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/toridraw_font_from_torirs.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_types.h"
#include "task_runner.h"

#include "toridraw.h"
#include "toridraw_font.h"

#include <SDL.h>
#include <bmp.h>

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PANE_W 480
#define PANE_H 540
#define MAX_PANES 3

#define BACKDROP 0x00202028

#define MAX_CANDIDATES 64
#define SCAN_MODEL_LIMIT_DEFAULT 4000

/** Frames averaged for the per-panel raster time. */
#define TIMING_WINDOW 120

enum
{
    FAMILY_BRANCHING = 0,
    FAMILY_SCANLINE = 1,
    FAMILY_COUNT = 2,
};

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
    struct Dat1BuildCache* buildcache;
    struct CacheProvider* provider;
    struct ToriDraw_Scene* scene;
    struct ToriDraw_Font* label_font;

    struct Candidate candidates[MAX_CANDIDATES];
    int candidate_count;
    int candidate_index;

    struct ToriDraw_Model* model;
    int model_id;

    int* pane_branching;
    int* pane_scanline;
    int* pane_diff;

    /* Shared camera state: both panels render from exactly these. */
    int yaw;
    int pitch;
    int zoom;

    int auto_rotate;
    int alpha_mode;
    int show_diff;
    int diff_amplify;

    int last_diff;

    /* Rolling window of raster-only times, one series per family. */
    double ms_samples[FAMILY_COUNT][TIMING_WINDOW];
    int ms_head;
    int ms_count;
};

static const int k_alpha_modes[] = { 0xFF, 0xC0, 0x80, 0x40 };
#define ALPHA_MODE_COUNT ((int)(sizeof(k_alpha_modes) / sizeof(k_alpha_modes[0])))

#define DEFAULT_PITCH 200
#define DEFAULT_ZOOM 1400
#define MIN_ZOOM 300
#define MAX_ZOOM 6000

static int
pane_count(const struct Viewer* viewer)
{
    return viewer->show_diff ? 3 : 2;
}

/* ---------------------------------------------------------------- loading */

static void
run_task(
    struct Viewer* viewer,
    struct ToriRS_Task* task)
{
    if( !task )
        return;
    ToriRS_TaskQueue_Add(viewer->runner.queue, task);
    TaskRunner_Drain(&viewer->runner);
}

/**
 * Upload every texture the model's faces name into the scene texture map. The
 * raster reads only the scene map, and skips any face whose texture is absent,
 * so this has to happen before the first render or the textured faces vanish.
 */
static void
publish_model_textures(
    struct Viewer* viewer,
    const struct ToriRS_Model* rs_model)
{
    struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(viewer->scene);

    if( !rs_model || !rs_model->face_textures || !tex_state )
        return;

    for( int face = 0; face < rs_model->face_count; face++ )
    {
        int texture_id = (int)rs_model->face_textures[face];
        if( texture_id < 0 || texture_id >= 2048 )
            continue;
        if( tex_state->texture_map.textures[texture_id] )
            continue;

        if( !CacheProvider_TextureHas(viewer->provider, texture_id) )
            run_task(viewer, CreateTask_Dat1TextureLoad(viewer->provider, texture_id));

        struct ToriRS_Texture* src = CacheProvider_TextureGet(viewer->provider, texture_id);
        if( !src || !src->texels || src->width <= 0 || src->height <= 0 )
            continue;

        struct ToriDraw_Texture* texture = calloc(1, sizeof(*texture));
        if( !texture )
            continue;
        size_t texel_bytes = (size_t)src->width * (size_t)src->height * sizeof(int);
        texture->texels = malloc(texel_bytes);
        if( !texture->texels )
        {
            free(texture);
            continue;
        }
        memcpy(texture->texels, src->texels, texel_bytes);
        texture->width = src->width;
        texture->height = src->height;
        texture->opaque = src->opaque;
        texture->animation_direction = src->animation_direction;
        texture->animation_speed = src->animation_speed;

        ToriDraw_SceneSetTexture(viewer->scene, texture_id, texture);
    }
}

static int
candidate_cmp(
    const void* lhs,
    const void* rhs)
{
    const struct Candidate* left = lhs;
    const struct Candidate* right = rhs;
    /* Prefer models that exercise both the texture and the alpha kernels. */
    int score_left = left->textured_faces + 4 * left->alpha_faces;
    int score_right = right->textured_faces + 4 * right->alpha_faces;
    if( score_left != score_right )
        return score_right - score_left;
    return right->textured_faces - left->textured_faces;
}

/**
 * Sweep the cache for models worth comparing: a model is interesting when it
 * has textured faces, and more interesting when it also has non-opaque ones.
 */
static void
scan_candidates(struct Viewer* viewer)
{
    struct Candidate found[MAX_CANDIDATES * 4];
    int found_count = 0;

    printf("scanning up to %d models for textures/alpha...\n", g_scan_limit);

    for( int id = 0;
         id < g_scan_limit && found_count < (int)(sizeof(found) / sizeof(found[0]));
         id++ )
    {
        if( !CacheProvider_ModelHas(viewer->provider, id) )
            run_task(viewer, CreateTask_Dat1ModelLoad(viewer->provider, id));

        struct ToriRS_Model* model = CacheProvider_ModelGet(viewer->provider, id);
        if( !model || model->face_count <= 0 )
            continue;

        int textured = 0;
        int alpha = 0;
        for( int face = 0; face < model->face_count; face++ )
        {
            if( model->face_textures && (int)model->face_textures[face] >= 0 )
                textured++;
            if( model->face_alphas && model->face_alphas[face] != 0 )
                alpha++;
        }

        if( textured == 0 )
            continue;

        found[found_count].model_id = id;
        found[found_count].textured_faces = textured;
        found[found_count].alpha_faces = alpha;
        found[found_count].face_count = model->face_count;
        found_count++;
    }

    qsort(found, (size_t)found_count, sizeof(found[0]), candidate_cmp);

    viewer->candidate_count = found_count < MAX_CANDIDATES ? found_count : MAX_CANDIDATES;
    memcpy(viewer->candidates, found, sizeof(found[0]) * (size_t)viewer->candidate_count);

    printf("found %d textured models; top candidates:\n", found_count);
    for( int i = 0; i < viewer->candidate_count && i < 8; i++ )
        printf(
            "  model %5d: %4d faces, %4d textured, %4d alpha\n",
            viewer->candidates[i].model_id,
            viewer->candidates[i].face_count,
            viewer->candidates[i].textured_faces,
            viewer->candidates[i].alpha_faces);
}

/**
 * Build the renderer-side model for `model_id`, applying the current synthetic
 * alpha mode. A mode below 0xFF overwrites every face alpha so the alpha and
 * facealpha kernels get exercised even on fully opaque geometry.
 */
static int
load_model(
    struct Viewer* viewer,
    int model_id)
{
    if( !CacheProvider_ModelHas(viewer->provider, model_id) )
        run_task(viewer, CreateTask_Dat1ModelLoad(viewer->provider, model_id));

    struct ToriRS_Model* src = CacheProvider_ModelGet(viewer->provider, model_id);
    if( !src )
    {
        fprintf(stderr, "model %d not available\n", model_id);
        return 0;
    }

    publish_model_textures(viewer, src);

    if( viewer->model )
    {
        ToriDraw_ModelFree(viewer->model);
        viewer->model = NULL;
    }

    viewer->model = ToriDraw_ModelFromToriRS(src);
    if( !viewer->model )
    {
        fprintf(stderr, "failed to convert model %d\n", model_id);
        return 0;
    }
    viewer->model_id = model_id;

    int alpha = k_alpha_modes[viewer->alpha_mode];
    if( alpha != 0xFF && viewer->model->face_alphas )
    {
        /* Face alpha is stored inverted for untextured faces (the raster does
         * 0xFF - alpha), so write the inverse of what we want to see. */
        for( int face = 0; face < viewer->model->face_count; face++ )
            viewer->model->face_alphas[face] = (alphaint_t)(0xFF - alpha);
    }

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = viewer->model;
    /* Scene/widget default light (ambient 64, attenuation 768). The per-type
     * contrast/ambient overrides are 0 here because a bare model carries none;
     * passing anything else washes the model out to flat white. */
    ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);

    printf("model %d: %d faces, alpha mode 0x%02X\n", model_id, viewer->model->face_count, alpha);
    return 1;
}

/**
 * Labels come from a cache font so the panels are self-describing. Failing to
 * load one is not fatal - the viewer just runs without captions.
 */
static void
load_label_font(struct Viewer* viewer)
{
    static char const* const k_names[] = { "b12", "p12", "p11" };

    for( int i = 0; i < (int)(sizeof(k_names) / sizeof(k_names[0])); i++ )
    {
        if( !CacheProvider_FontHas(viewer->provider, 0) )
            run_task(viewer, CreateTask_Dat1FontLoadByName(viewer->provider, k_names[i], 0));

        struct ToriRS_Font* src = CacheProvider_FontGet(viewer->provider, 0);
        if( !src )
            continue;

        viewer->label_font = ToriDraw_FontFromToriRS(src);
        if( viewer->label_font )
            return;
    }

    fprintf(stderr, "note: no label font available, panels will be unlabelled\n");
}

/* --------------------------------------------------------------- drawing */

static void
draw_label(
    struct Viewer* viewer,
    int* pane,
    int x,
    int y,
    const char* text,
    int color)
{
    if( !viewer->label_font )
        return;

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = PANE_W;
    view_port.height = PANE_H;
    view_port.stride = PANE_W;
    view_port.clip_right = PANE_W;
    view_port.clip_bottom = PANE_H;

    ToriDraw2D_DrawString(
        viewer->label_font, &view_port, x, y, text, color, false, true, pane);
}

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static void
record_timing(
    struct Viewer* viewer,
    int family,
    double ms)
{
    viewer->ms_samples[family][viewer->ms_head] = ms;
    if( family == 1 )
    {
        viewer->ms_head = (viewer->ms_head + 1) % TIMING_WINDOW;
        if( viewer->ms_count < TIMING_WINDOW )
            viewer->ms_count++;
    }
}

static double
average_ms(
    const struct Viewer* viewer,
    int family)
{
    if( viewer->ms_count <= 0 )
        return 0.0;
    double sum = 0.0;
    for( int i = 0; i < viewer->ms_count; i++ )
        sum += viewer->ms_samples[family][i];
    return sum / viewer->ms_count;
}

/**
 * Draw the model into both panels.
 *
 * Projection and face ordering happen once and are shared, so the two families
 * rasterize byte-identical input - and so the timing brackets only the raster
 * itself, not the transform and sort work the two have in common.
 */
static void
render_frame(struct Viewer* viewer)
{
    for( int i = 0; i < PANE_W * PANE_H; i++ )
    {
        viewer->pane_branching[i] = BACKDROP;
        viewer->pane_scanline[i] = BACKDROP;
    }

    if( !viewer->model )
        return;

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = viewer->model;

    /* Same framing convention as ToriDraw_ModelSprite: a pitched orbit camera
     * with the model lifted by half its own height, so RS models (which grow
     * along -y) sit upright and centred. */
    int sin_pitch = (ToriDraw_Sin(viewer->pitch & 2047) * viewer->zoom) >> 16;
    int cos_pitch = (ToriDraw_Cos(viewer->pitch & 2047) * viewer->zoom) >> 16;

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = PANE_W;
    view_port.height = PANE_H;
    view_port.stride = PANE_W;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = PANE_W;
    view_port.clip_bottom = PANE_H;
    view_port.x_center = PANE_W / 2;
    view_port.y_center = PANE_H / 2;

    struct ToriDraw_Camera camera = { 0 };
    camera.pitch = viewer->pitch & 2047;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 50;

    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    int model_height = bounds ? (bounds->max_y - bounds->min_y) : 0;

    struct ToriDraw_Position position = { 0 };
    position.x = 0;
    position.y = sin_pitch + (model_height / 2);
    position.z = cos_pitch;
    position.yaw = viewer->yaw & 2047;

    if( ToriDraw_RenderModel1Project(hnd, viewer->scene, &position, &view_port, &camera) !=
        TORIDRAW_CULL_VISIBLE )
        return;
    ToriDraw_RenderModel2SortFaces(hnd, viewer->scene);

    ToriDraw_RasterSetScanline(false);
    double t0 = now_ms();
    ToriDraw_RenderModel3Raster(
        viewer->scene, &view_port, &camera, viewer->pane_branching, false);
    double t1 = now_ms();

    ToriDraw_RasterSetScanline(true);
    double t2 = now_ms();
    ToriDraw_RenderModel3Raster(
        viewer->scene, &view_port, &camera, viewer->pane_scanline, false);
    double t3 = now_ms();
    ToriDraw_RasterSetScanline(false);

    record_timing(viewer, 0, t1 - t0);
    record_timing(viewer, 1, t3 - t2);
}

static int
render_diff(struct Viewer* viewer)
{
    int differing = 0;

    for( int i = 0; i < PANE_W * PANE_H; i++ )
    {
        int left = viewer->pane_branching[i];
        int right = viewer->pane_scanline[i];

        if( left == right )
        {
            viewer->pane_diff[i] = 0x00101010;
            continue;
        }
        differing++;

        int out = 0;
        for( int channel = 0; channel < 3; channel++ )
        {
            int delta = ((left >> (channel * 8)) & 0xFF) - ((right >> (channel * 8)) & 0xFF);
            if( delta < 0 )
                delta = -delta;
            if( viewer->diff_amplify )
            {
                delta *= 16;
                if( delta > 255 )
                    delta = 255;
            }
            out |= delta << (channel * 8);
        }
        viewer->pane_diff[i] = out;
    }

    return differing;
}

static void
draw_panel_captions(struct Viewer* viewer)
{
    char line[160];

    double ms_branching = average_ms(viewer, FAMILY_BRANCHING);
    double ms_scanline = average_ms(viewer, FAMILY_SCANLINE);

    draw_label(viewer, viewer->pane_branching, 8, 16, "BRANCHING (default)", 0x00FFD070);
    draw_label(viewer, viewer->pane_scanline, 8, 16, "SCANLINE (new)", 0x0080FF90);

    snprintf(
        line,
        sizeof(line),
        "model %d  faces %d  alpha 0x%02X",
        viewer->model_id,
        viewer->model ? viewer->model->face_count : 0,
        k_alpha_modes[viewer->alpha_mode]);
    draw_label(viewer, viewer->pane_branching, 8, 32, line, 0x00C0C0C0);

    snprintf(
        line,
        sizeof(line),
        "yaw %d  pitch %d  zoom %d",
        viewer->yaw & 2047,
        viewer->pitch & 2047,
        viewer->zoom);
    draw_label(viewer, viewer->pane_scanline, 8, 32, line, 0x00C0C0C0);

    snprintf(line, sizeof(line), "%d pixels differ", viewer->last_diff);
    draw_label(
        viewer,
        viewer->pane_scanline,
        8,
        48,
        line,
        viewer->last_diff ? 0x00FFA0A0 : 0x0080FF90);

    /* Raster-only time, averaged over the last TIMING_WINDOW frames. Projection
     * and face sorting are shared and excluded. */
    snprintf(line, sizeof(line), "raster avg %.3f ms  (%d frames)", ms_branching, viewer->ms_count);
    draw_label(viewer, viewer->pane_branching, 8, 48, line, 0x00FFD070);

    if( ms_branching > 0.0 )
        snprintf(
            line,
            sizeof(line),
            "raster avg %.3f ms  (%.2fx branching)",
            ms_scanline,
            ms_scanline / ms_branching);
    else
        snprintf(line, sizeof(line), "raster avg %.3f ms", ms_scanline);
    draw_label(
        viewer,
        viewer->pane_scanline,
        8,
        64,
        line,
        ms_scanline <= ms_branching ? 0x0080FF90 : 0x00FFA0A0);

#ifndef __OPTIMIZE__
    draw_label(
        viewer,
        viewer->pane_branching,
        8,
        64,
        "-O0 build: times are not comparable, use OPT=1",
        0x00FF8080);
#endif

    if( viewer->show_diff )
    {
        draw_label(
            viewer,
            viewer->pane_diff,
            8,
            16,
            viewer->diff_amplify ? "DIFFERENCE x16" : "DIFFERENCE (raw)",
            0x00FF8080);
    }

    draw_label(
        viewer,
        viewer->pane_branching,
        8,
        PANE_H - 10,
        "arrows rotate  +/- zoom  [ ] model  a alpha  d diff  space spin",
        0x00909090);
}

static void
blit_panes(
    struct Viewer* viewer,
    SDL_Texture* texture,
    int window_w)
{
    int* pixels = NULL;
    int pitch = 0;

    if( SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch) != 0 )
        return;

    int stride = pitch / 4;
    const int* panes[MAX_PANES] = {
        viewer->pane_branching, viewer->pane_scanline, viewer->pane_diff
    };
    int panes_shown = pane_count(viewer);

    for( int y = 0; y < PANE_H; y++ )
    {
        for( int pane = 0; pane < panes_shown; pane++ )
        {
            const int* src = panes[pane] + (y * PANE_W);
            int* dst = pixels + (y * stride) + (pane * PANE_W);
            for( int x = 0; x < PANE_W; x++ )
                dst[x] = 0xFF000000u | (unsigned)src[x];
        }
        /* One-pixel separators between panels. */
        for( int pane = 1; pane < panes_shown; pane++ )
            pixels[(y * stride) + (pane * PANE_W)] = 0xFF505060u;
    }

    (void)window_w;
    SDL_UnlockTexture(texture);
}

/* -------------------------------------------------------------- headless */

/**
 * Same sweep with every face's texture stripped, so the whole model goes down
 * the flat/gouraud kernels. Those share the scanline family's left-edge rule
 * exactly, so this must come out at zero differing pixels - which is what pins
 * the textured residual on the reference kernels' (x - 1) >> 16 span start
 * rather than on anything in the walker.
 */
static long
run_untextured_sweep(
    struct Viewer* viewer,
    int frames)
{
    if( !viewer->model || !viewer->model->face_textures )
        return 0;

    size_t bytes = sizeof(faceint_t) * (size_t)viewer->model->face_count;
    faceint_t* saved = malloc(bytes);
    if( !saved )
        return 0;
    memcpy(saved, viewer->model->face_textures, bytes);
    for( int face = 0; face < viewer->model->face_count; face++ )
        viewer->model->face_textures[face] = -1;

    long total_diff = 0;
    for( int frame = 0; frame < frames; frame++ )
    {
        viewer->yaw = (frame * 2048) / (frames > 0 ? frames : 1);
        render_frame(viewer);
        total_diff += render_diff(viewer);
    }

    memcpy(viewer->model->face_textures, saved, bytes);
    free(saved);
    return total_diff;
}

static int
run_headless(
    struct Viewer* viewer,
    int frames,
    const char* bmp_prefix)
{
    int worst_overall = 0;

    for( int mode = 0; mode < ALPHA_MODE_COUNT; mode++ )
    {
        viewer->alpha_mode = mode;
        if( !load_model(viewer, viewer->model_id) )
            return 1;

        long total_diff = 0;
        long total_drawn = 0;
        int worst = 0;

        for( int frame = 0; frame < frames; frame++ )
        {
            viewer->yaw = (frame * 2048) / (frames > 0 ? frames : 1);

            render_frame(viewer);
            int differing = render_diff(viewer);

            int drawn = 0;
            for( int i = 0; i < PANE_W * PANE_H; i++ )
                if( viewer->pane_branching[i] != BACKDROP )
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

        double ms_branching = average_ms(viewer, FAMILY_BRANCHING);
        double ms_scanline = average_ms(viewer, FAMILY_SCANLINE);
        printf(
            "  raster avg: branching %.4f ms, scanline %.4f ms (%.2fx)%s\n",
            ms_branching,
            ms_scanline,
            ms_branching > 0.0 ? ms_scanline / ms_branching : 0.0,
#ifdef __OPTIMIZE__
            "");
#else
            "  [-O0 build, not comparable - rebuild with OPT=1]");
#endif

        if( worst > worst_overall )
            worst_overall = worst;

        long untextured_diff = run_untextured_sweep(viewer, frames);
        printf(
            "  same model with textures stripped (flat/gouraud only): %ld differing pixels%s\n",
            untextured_diff,
            untextured_diff == 0 ? " - exact" : " - UNEXPECTED");

        int* triptych = malloc(sizeof(int) * PANE_W * MAX_PANES * PANE_H);
        if( triptych )
        {
            const int* panes[MAX_PANES] = {
                viewer->pane_branching, viewer->pane_scanline, viewer->pane_diff
            };
            int full_w = PANE_W * MAX_PANES;
            for( int y = 0; y < PANE_H; y++ )
                for( int pane = 0; pane < MAX_PANES; pane++ )
                    memcpy(
                        triptych + (y * full_w) + (pane * PANE_W),
                        panes[pane] + (y * PANE_W),
                        sizeof(int) * PANE_W);

            char path[512];
            snprintf(
                path,
                sizeof(path),
                "%s_model%d_alpha%02X.bmp",
                bmp_prefix,
                viewer->model_id,
                k_alpha_modes[mode]);
            bmp_write_file(path, triptych, full_w, PANE_H);
            printf("  wrote %s\n", path);
            free(triptych);
        }
    }

    return worst_overall != 0;
}

/**
 * Render exactly what the window would show for the current view state, side by
 * side with captions, and write it to a BMP. Verifies the panel layout without
 * needing a display.
 */
static void
write_window_shot(
    struct Viewer* viewer,
    const char* path)
{
    /* Fill the timing window so the caption shows a real average, not one frame. */
    for( int warmup = 0; warmup < TIMING_WINDOW; warmup++ )
        render_frame(viewer);

    viewer->last_diff = render_diff(viewer);
    draw_panel_captions(viewer);

    int panes_shown = pane_count(viewer);
    int full_w = PANE_W * panes_shown;
    int* shot = malloc(sizeof(int) * (size_t)full_w * PANE_H);
    if( !shot )
        return;

    const int* panes[MAX_PANES] = {
        viewer->pane_branching, viewer->pane_scanline, viewer->pane_diff
    };

    for( int y = 0; y < PANE_H; y++ )
    {
        for( int pane = 0; pane < panes_shown; pane++ )
            memcpy(
                shot + (y * full_w) + (pane * PANE_W),
                panes[pane] + (y * PANE_W),
                sizeof(int) * PANE_W);
        for( int pane = 1; pane < panes_shown; pane++ )
            shot[(y * full_w) + (pane * PANE_W)] = 0x00505060;
    }

    bmp_write_file(path, shot, full_w, PANE_H);
    printf("wrote %s (%d panels, %dx%d)\n", path, panes_shown, full_w, PANE_H);
    free(shot);
}

/* ------------------------------------------------------------ event loop */

static void
reset_view(struct Viewer* viewer)
{
    viewer->yaw = 0;
    viewer->pitch = DEFAULT_PITCH;
    viewer->zoom = DEFAULT_ZOOM;
}

/** Returns 1 when the window needs resizing (panel count changed). */
static int
handle_key(
    struct Viewer* viewer,
    SDL_Keycode key,
    int* running)
{
    switch( key )
    {
    case SDLK_ESCAPE:
    case SDLK_q:
        *running = 0;
        break;

    case SDLK_LEFT:
        viewer->yaw = (viewer->yaw + 2048 - 32) & 2047;
        break;
    case SDLK_RIGHT:
        viewer->yaw = (viewer->yaw + 32) & 2047;
        break;
    case SDLK_UP:
        viewer->pitch = (viewer->pitch + 2048 - 16) & 2047;
        break;
    case SDLK_DOWN:
        viewer->pitch = (viewer->pitch + 16) & 2047;
        break;

    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS:
        viewer->zoom -= 100;
        if( viewer->zoom < MIN_ZOOM )
            viewer->zoom = MIN_ZOOM;
        break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        viewer->zoom += 100;
        if( viewer->zoom > MAX_ZOOM )
            viewer->zoom = MAX_ZOOM;
        break;

    case SDLK_LEFTBRACKET:
        if( viewer->candidate_count > 0 )
        {
            viewer->candidate_index =
                (viewer->candidate_index + viewer->candidate_count - 1) % viewer->candidate_count;
            load_model(viewer, viewer->candidates[viewer->candidate_index].model_id);
        }
        break;
    case SDLK_RIGHTBRACKET:
        if( viewer->candidate_count > 0 )
        {
            viewer->candidate_index = (viewer->candidate_index + 1) % viewer->candidate_count;
            load_model(viewer, viewer->candidates[viewer->candidate_index].model_id);
        }
        break;

    case SDLK_SPACE:
        viewer->auto_rotate = !viewer->auto_rotate;
        break;
    case SDLK_a:
        viewer->alpha_mode = (viewer->alpha_mode + 1) % ALPHA_MODE_COUNT;
        load_model(viewer, viewer->model_id);
        break;
    case SDLK_x:
        viewer->diff_amplify = !viewer->diff_amplify;
        break;
    case SDLK_r:
        reset_view(viewer);
        break;
    case SDLK_d:
        viewer->show_diff = !viewer->show_diff;
        return 1;

    default:
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ main */

int
main(
    int argc,
    char** argv)
{
    /* Diagnostic output should survive an interrupted run. */
    setvbuf(stdout, NULL, _IOLBF, 0);

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

    struct Viewer viewer;
    memset(&viewer, 0, sizeof(viewer));
    viewer.diff_amplify = 1;
    viewer.auto_rotate = 1;
    reset_view(&viewer);

    viewer.runner.io = ToriRS_IO_New();
    viewer.runner.queue = ToriRS_TaskQueue_New();
    viewer.runner.px = PlatformX_IO_New();
    assert(viewer.runner.io && viewer.runner.queue && viewer.runner.px);
    PlatformX_IO_InitDat1Disk(viewer.runner.px, disk);

    viewer.buildcache = dat1_buildcache_new();
    viewer.provider = dat1_buildcache_as_provider(viewer.buildcache);
    viewer.scene = ToriDraw_SceneNew(0);
    assert(viewer.scene);

    viewer.pane_branching = malloc(sizeof(int) * PANE_W * PANE_H);
    viewer.pane_scanline = malloc(sizeof(int) * PANE_W * PANE_H);
    viewer.pane_diff = malloc(sizeof(int) * PANE_W * PANE_H);
    assert(viewer.pane_branching && viewer.pane_scanline && viewer.pane_diff);

    if( forced_model >= 0 )
    {
        viewer.candidates[0].model_id = forced_model;
        viewer.candidate_count = 1;
    }
    else
    {
        scan_candidates(&viewer);
        if( viewer.candidate_count == 0 )
        {
            fprintf(stderr, "no textured models found in %s\n", cache_dir);
            return 1;
        }
    }

    if( !load_model(&viewer, viewer.candidates[0].model_id) )
        return 1;

    if( headless_frames > 0 )
        return run_headless(&viewer, headless_frames, bmp_prefix);

    load_label_font(&viewer);

    const char* shot_path = getenv("TORIRS_SCANLINE_SHOT");
    if( shot_path )
    {
        const char* shot_yaw = getenv("TORIRS_SCANLINE_SHOT_YAW");
        const char* shot_pitch = getenv("TORIRS_SCANLINE_SHOT_PITCH");
        viewer.yaw = shot_yaw ? atoi(shot_yaw) : 300;
        if( shot_pitch )
            viewer.pitch = atoi(shot_pitch);
        if( getenv("TORIRS_SCANLINE_SHOT_DIFF") )
            viewer.show_diff = 1;
        write_window_shot(&viewer, shot_path);
        return 0;
    }

    if( SDL_Init(SDL_INIT_VIDEO) != 0 )
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    int window_w = PANE_W * pane_count(&viewer);

    SDL_Window* window = SDL_CreateWindow(
        "toridraw raster compare",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_w,
        PANE_H,
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
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, window_w, PANE_H);
    if( !texture )
    {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return 1;
    }

    printf(
        "\nkeys: arrows rotate/tilt, +/- zoom, [ ] model, space spin, a alpha, "
        "d diff panel, x diff scale, r reset, q quit\n\n");

    int running = 1;
    while( running )
    {
        int relayout = 0;

        SDL_Event event;
        while( SDL_PollEvent(&event) )
        {
            if( event.type == SDL_QUIT )
                running = 0;
            else if( event.type == SDL_KEYDOWN )
                relayout |= handle_key(&viewer, event.key.keysym.sym, &running);
        }

        if( relayout )
        {
            window_w = PANE_W * pane_count(&viewer);
            SDL_SetWindowSize(window, window_w, PANE_H);
            SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING,
                window_w,
                PANE_H);
            if( !texture )
                break;
        }

        if( viewer.auto_rotate )
            viewer.yaw = (viewer.yaw + 8) & 2047;

        render_frame(&viewer);
        viewer.last_diff = render_diff(&viewer);
        draw_panel_captions(&viewer);
        blit_panes(&viewer, texture, window_w);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if( viewer.label_font )
        ToriDraw_FontFree(viewer.label_font);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
