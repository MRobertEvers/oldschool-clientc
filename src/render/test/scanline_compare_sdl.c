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
 * Two subjects:
 *   model  - one cache model on a turntable
 *   world  - a real map square built through WorldBuilder and drawn through the
 *            production painter -> frame emitter -> software renderer path, so
 *            what is compared is exactly what the client draws
 *
 * Run from src/:
 *   make scanline-compare [DAT1_CACHE=../cache254]
 *   ./build/scanline_compare ../cache254 148        # model mode
 *   ./build/scanline_compare ../cache254 world      # world mode at 50,50
 *
 * In model mode, pass the model id: with none it sweeps the cache for the model
 * with the most textured and alpha-blended faces, which takes a couple of
 * minutes. In cache254 that model is 148 (1004 faces, 48 textured, 420 alpha);
 * 597, 598, 1178 and 1199 are also worth a look.
 *
 * Timings are only meaningful from an optimized build: `make OPT=1
 * scanline-compare`, then run build_opt/scanline_compare. A debug build says so
 * in the panel.
 *
 * Keys. Every key means exactly one thing; WASD is reserved for the world pan
 * cluster, which is why the difference panel is on `v` and the alpha cycle on
 * `t` rather than the more obvious `d` and `a`.
 *
 * Both modes:
 *   left / right     yaw
 *   up / down        pitch
 *   + / -            zoom (model) / camera height (world)
 *   m                switch between model and world
 *   v                show / hide the difference panel
 *   x                toggle difference amplification (x16 or raw)
 *   r                reset view
 *   escape / q       quit
 *
 * Model mode:
 *   [ / ]            previous / next model from the scanned candidate list
 *   space            toggle the auto-turntable
 *   t                cycle a synthetic per-face alpha over the model
 *                    (0xFF -> 0xC0 -> 0x80 -> 0x40), so the alpha kernels are
 *                    exercised even on models whose faces are all opaque
 *
 * World mode:
 *   w / a / s / d    pan the camera north / west / south / east
 *   1 / 2            map square X -1 / +1
 *   3 / 4            map square Z -1 / +1
 *
 * Environment:
 *   TORIRS_SCANLINE_HEADLESS=N   render N turntable frames with no window,
 *                                print the per-frame pixel difference for each
 *                                alpha mode, dump BMPs, and exit (model mode).
 *   TORIRS_SCANLINE_SHOT=path    write a single frame of exactly what the
 *                                window shows, then exit.
 *   TORIRS_SCANLINE_SHOT_YAW/_PITCH/_DIFF   view state for the shot.
 *   TORIRS_SCANLINE_MAP=x,z      start in world mode on that square.
 *   TORIRS_SCANLINE_REV=N        dat1 cache revision (default 254).
 *   TORIRS_SCANLINE_BMP=prefix   BMP output prefix (default build/scanline).
 *   TORIRS_SCANLINE_SCAN=N       how many model ids to sweep (default 4000).
 */

#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/toridraw_font_from_torirs.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_types.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "painters/painters.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "task_runner.h"
#include "ui/uitree_emit.h"
#include "varp/varp_manager.h"
#include "world/world.h"

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

enum ViewMode
{
    MODE_MODEL = 0,
    MODE_WORLD = 1,
};

/** Lumbridge; the client's own default spawn square. */
#define DEFAULT_MAP_X 50
#define DEFAULT_MAP_Z 50

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

    /* --- world mode --- */
    enum ViewMode mode;
    struct World* world;
    struct WorldBuilder* world_builder;
    struct VarPManager varp;
    struct PaintersBuffer* painter_buffer;
    int map_x;
    int map_z;
    int world_loaded;
    int world_commands;
    /* Camera position over the scene, in world units (y is negative upward). */
    int world_cam_x;
    int world_cam_y;
    int world_cam_z;
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

/* ---------------------------------------------------------------- timing */

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1.0e6);
}

/**
 * One sample per family per frame. The window advances on the scanline sample,
 * so the two series stay index-aligned and a partially filled window never
 * mixes frames.
 */
static void
record_timing(
    struct Viewer* viewer,
    int family,
    double elapsed_ms)
{
    viewer->ms_samples[family][viewer->ms_head] = elapsed_ms;
    if( family == FAMILY_SCANLINE )
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
    ToriDraw_LightModelScene(hnd, 0, 0);

    printf("model %d: %d faces, alpha mode 0x%02X\n", model_id, viewer->model->face_count, alpha);
    return 1;
}

/* ------------------------------------------------------------------ world */

/**
 * Drain the texture ids the models built by the world rebuild reported, load
 * them, and publish into the scene texture map. This is the same event-driven
 * hand-off the client does in app_sync_textures: a model reports its texture
 * set exactly once, when it is converted, and nothing else will ever report it.
 */
static void
world_sync_textures(struct Viewer* viewer)
{
    struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(viewer->scene);
    int ids[256];
    int id_count;

    if( !tex_state )
        return;

    while( (id_count = ToriDraw_ModelTextureWantsTake(ids, 256)) > 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int texture_id = ids[i];
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
}

/** Scene centre, high up and looking down - the client's own scene-reset camera. */
static void
world_reset_camera(struct Viewer* viewer)
{
    int scene_size = viewer->world ? viewer->world->_scene_size : 104;
    viewer->world_cam_x = (scene_size / 2 * 128) + 64;
    viewer->world_cam_z = (scene_size / 2 * 128) + 64;
    viewer->world_cam_y = -2000;
    viewer->pitch = 450;
    viewer->yaw = 0;
}

/**
 * Load one map square into a fresh world. The static scene pool is cleared
 * first: terrain and scenery elements from the previous square would otherwise
 * survive the rebuild and paint on top of the new one.
 */
static int
world_load(
    struct Viewer* viewer,
    int map_x,
    int map_z)
{
    if( map_x < 0 )
        map_x = 0;
    if( map_z < 0 )
        map_z = 0;

    printf("loading map square %d,%d ...\n", map_x, map_z);

    if( viewer->world_builder )
    {
        WorldBuilder_Free(viewer->world_builder);
        viewer->world_builder = NULL;
    }
    if( viewer->world )
    {
        World_Free(viewer->world);
        viewer->world = NULL;
    }
    ToriDraw_SceneClearPool(viewer->scene, TORIDRAW_SCENE_POOL_STATIC);

    viewer->world = World_New();
    if( !viewer->world )
        return 0;
    VarPManager_Init(&viewer->varp);
    viewer->world_builder =
        WorldBuilder_New(viewer->world, viewer->provider, viewer->scene, &viewer->varp);
    if( !viewer->world_builder )
        return 0;

    if( !viewer->painter_buffer )
        viewer->painter_buffer = painter_buffer_new();

    int chunks[2] = { map_x, map_z };
    run_task(
        viewer,
        CreateTask_WorldLoad(
            viewer->provider, viewer->world_builder, chunks, 1, -1, -1, NULL, NULL, NULL));

    if( !viewer->world->load_complete )
    {
        fprintf(stderr, "map square %d,%d did not load\n", map_x, map_z);
        viewer->world_loaded = 0;
        return 0;
    }

    world_sync_textures(viewer);

    viewer->map_x = map_x;
    viewer->map_z = map_z;
    viewer->world_loaded = 1;
    world_reset_camera(viewer);

    printf("map square %d,%d loaded (scene %d tiles)\n", map_x, map_z, viewer->world->_scene_size);
    return 1;
}

/**
 * Replay the painted world into one pane through the production frame emitter
 * and software renderer, so what is compared is exactly what the client draws.
 */
static int
world_replay(
    struct Viewer* viewer,
    int* pane)
{
    struct UITreeEmitDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_WORLD;
    desc.x = 0;
    desc.y = 0;
    desc.w = PANE_W;
    desc.h = PANE_H;
    desc.clip.x = 0;
    desc.clip.y = 0;
    desc.clip.w = PANE_W;
    desc.clip.h = PANE_H;
    desc.world_level_mask = 0xF;

    struct ToriDraw_Camera camera = { 0 };
    camera.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
    camera.proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT;
    camera.near_plane_z = 50;
    camera.pitch = viewer->pitch & 2047;
    camera.yaw = viewer->yaw & 2047;
    camera.roll = 0;

    struct ToriRS_Frame frame;
    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, viewer->scene);
    ToriRS_FrameSetCanvas(&frame, PANE_W, PANE_H);
    ToriRS_FrameSetEmit(&frame, &desc, 1);
    ToriRS_FrameSetWorld(
        &frame,
        viewer->world,
        viewer->painter_buffer,
        &camera,
        viewer->world_cam_x,
        viewer->world_cam_y,
        viewer->world_cam_z);

    struct ToriRS_Soft3D soft;
    ToriRS_Soft3D_Init(&soft, viewer->scene, pane, PANE_W, PANE_H);

    int drawn = 0;
    struct ToriRS_RenderCommand cmd;
    ToriRS_FrameBegin(&frame);
    while( ToriRS_FrameNextCommand(&frame, &cmd) )
    {
        ToriRS_Soft3D_Execute(&soft, &cmd);
        drawn++;
    }
    ToriRS_FrameEnd(&frame);

    return drawn;
}

/**
 * Draw the world into both panels.
 *
 * The painter runs once and both families replay the identical command list, so
 * the timing difference between the two is the raster. Unlike model mode the
 * bracket also covers per-model projection and face sorting, which the command
 * replay owns - that work is identical for both families.
 */
static void
render_world_frame(struct Viewer* viewer)
{
    for( int i = 0; i < PANE_W * PANE_H; i++ )
    {
        viewer->pane_branching[i] = BACKDROP;
        viewer->pane_scanline[i] = BACKDROP;
    }

    if( !viewer->world_loaded || !viewer->world || !viewer->painter_buffer )
        return;

    int max_tile = viewer->world->_scene_size - 1;
    int cam_sx = viewer->world_cam_x >> 7;
    int cam_sz = viewer->world_cam_z >> 7;
    if( cam_sx < 0 )
        cam_sx = 0;
    if( cam_sx > max_tile )
        cam_sx = max_tile;
    if( cam_sz < 0 )
        cam_sz = 0;
    if( cam_sz > max_tile )
        cam_sz = max_tile;

    painter_set_camera_angles(
        viewer->world->painter, viewer->pitch & 2047, viewer->yaw & 2047);
    painter_set_level_mask(viewer->world->painter, 0xF);
    painter_paint_bucket(viewer->world->painter, viewer->painter_buffer, cam_sx, cam_sz, 0);

    ToriDraw_RasterSetScanline(false);
    double t0 = now_ms();
    viewer->world_commands = world_replay(viewer, viewer->pane_branching);
    double t1 = now_ms();

    ToriDraw_RasterSetScanline(true);
    double t2 = now_ms();
    world_replay(viewer, viewer->pane_scanline);
    double t3 = now_ms();
    ToriDraw_RasterSetScanline(false);

    record_timing(viewer, FAMILY_BRANCHING, t1 - t0);
    record_timing(viewer, FAMILY_SCANLINE, t3 - t2);
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
    camera.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
    camera.proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT;
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

    record_timing(viewer, FAMILY_BRANCHING, t1 - t0);
    record_timing(viewer, FAMILY_SCANLINE, t3 - t2);
}

/** Draw whichever subject the viewer is showing into both panels. */
static void
render_subject(struct Viewer* viewer)
{
    if( viewer->mode == MODE_WORLD )
        render_world_frame(viewer);
    else
        render_frame(viewer);
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

    if( viewer->mode == MODE_WORLD )
    {
        snprintf(
            line,
            sizeof(line),
            "WORLD  square %d,%d  %d draws",
            viewer->map_x,
            viewer->map_z,
            viewer->world_commands);
        draw_label(viewer, viewer->pane_branching, 8, 32, line, 0x00C0C0C0);

        snprintf(
            line,
            sizeof(line),
            "cam %d,%d,%d  yaw %d  pitch %d",
            viewer->world_cam_x,
            viewer->world_cam_y,
            viewer->world_cam_z,
            viewer->yaw & 2047,
            viewer->pitch & 2047);
        draw_label(viewer, viewer->pane_scanline, 8, 32, line, 0x00C0C0C0);
    }
    else
    {
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
    }

    snprintf(line, sizeof(line), "%d pixels differ", viewer->last_diff);
    draw_label(
        viewer,
        viewer->pane_scanline,
        8,
        48,
        line,
        viewer->last_diff ? 0x00FFA0A0 : 0x0080FF90);

    /*
     * Averaged over the last TIMING_WINDOW frames.
     *
     * Model mode brackets the raster alone - projection and face sorting run
     * once and are shared by both families. World mode replays a painter
     * command list, which owns per-model projection and sorting, so its bracket
     * necessarily includes them. That work is identical for both families, but
     * it dilutes the raster difference, hence the different label.
     */
    const char* timing_kind = viewer->mode == MODE_WORLD ? "draw" : "raster";

    snprintf(
        line, sizeof(line), "%s avg %.3f ms  (%d frames)", timing_kind, ms_branching,
        viewer->ms_count);
    draw_label(viewer, viewer->pane_branching, 8, 48, line, 0x00FFD070);

    if( ms_branching > 0.0 )
        snprintf(
            line,
            sizeof(line),
            "%s avg %.3f ms  (%.2fx branching)",
            timing_kind,
            ms_scanline,
            ms_scanline / ms_branching);
    else
        snprintf(line, sizeof(line), "%s avg %.3f ms", timing_kind, ms_scanline);
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
        viewer->mode == MODE_WORLD
            ? "arrows look  wasd pan  +/- height  1/2 mapX  3/4 mapZ  m model  v diff"
            : "arrows rotate  +/- zoom  [ ] model  t alpha  m world  v diff  space spin",
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
        render_subject(viewer);

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

    double ms_branching = average_ms(viewer, FAMILY_BRANCHING);
    double ms_scanline = average_ms(viewer, FAMILY_SCANLINE);
    printf(
        "  %s: %d pixels differ; %s avg branching %.4f ms, scanline %.4f ms (%.2fx)%s\n",
        viewer->mode == MODE_WORLD ? "world" : "model",
        viewer->last_diff,
        viewer->mode == MODE_WORLD ? "draw (incl. project+sort)" : "raster",
        ms_branching,
        ms_scanline,
        ms_branching > 0.0 ? ms_scanline / ms_branching : 0.0,
#ifdef __OPTIMIZE__
        "");
#else
        "  [-O0 build, not comparable]");
#endif

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
        if( viewer->mode == MODE_WORLD )
        {
            /* World y is negative upward, so "closer" means less negative. */
            viewer->world_cam_y += 200;
            break;
        }
        viewer->zoom -= 100;
        if( viewer->zoom < MIN_ZOOM )
            viewer->zoom = MIN_ZOOM;
        break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        if( viewer->mode == MODE_WORLD )
        {
            viewer->world_cam_y -= 200;
            break;
        }
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

    /* --- world mode --- */
    case SDLK_m:
        viewer->mode = (viewer->mode == MODE_WORLD) ? MODE_MODEL : MODE_WORLD;
        viewer->ms_count = 0;
        viewer->ms_head = 0;
        if( viewer->mode == MODE_WORLD )
        {
            if( !viewer->world_loaded )
                world_load(viewer, viewer->map_x, viewer->map_z);
            else
                world_reset_camera(viewer);
            viewer->auto_rotate = 0;
        }
        else
        {
            reset_view(viewer);
        }
        break;

    case SDLK_1:
        if( viewer->mode == MODE_WORLD )
            world_load(viewer, viewer->map_x - 1, viewer->map_z);
        break;
    case SDLK_2:
        if( viewer->mode == MODE_WORLD )
            world_load(viewer, viewer->map_x + 1, viewer->map_z);
        break;
    case SDLK_3:
        if( viewer->mode == MODE_WORLD )
            world_load(viewer, viewer->map_x, viewer->map_z - 1);
        break;
    case SDLK_4:
        if( viewer->mode == MODE_WORLD )
            world_load(viewer, viewer->map_x, viewer->map_z + 1);
        break;

    /* Pan the world camera. WASD is the whole cluster and nothing else claims a
     * letter in it - the difference panel is on `v` and the alpha cycle on `t`
     * precisely so this stays unambiguous. */
    case SDLK_w:
        if( viewer->mode == MODE_WORLD )
            viewer->world_cam_z += 128;
        break;
    case SDLK_s:
        if( viewer->mode == MODE_WORLD )
            viewer->world_cam_z -= 128;
        break;
    case SDLK_a:
        if( viewer->mode == MODE_WORLD )
            viewer->world_cam_x -= 128;
        break;
    case SDLK_d:
        if( viewer->mode == MODE_WORLD )
            viewer->world_cam_x += 128;
        break;

    case SDLK_t:
        viewer->alpha_mode = (viewer->alpha_mode + 1) % ALPHA_MODE_COUNT;
        load_model(viewer, viewer->model_id);
        break;

    case SDLK_SPACE:
        viewer->auto_rotate = !viewer->auto_rotate;
        break;
    case SDLK_x:
        viewer->diff_amplify = !viewer->diff_amplify;
        break;
    case SDLK_r:
        reset_view(viewer);
        break;
    case SDLK_v:
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
    viewer.map_x = DEFAULT_MAP_X;
    viewer.map_z = DEFAULT_MAP_Z;
    reset_view(&viewer);

    /* `world` in place of a model id starts in world mode; TORIRS_SCANLINE_MAP
     * overrides the starting square. */
    int start_in_world = (argc > 2 && strcmp(argv[2], "world") == 0);
    {
        const char* map_env = getenv("TORIRS_SCANLINE_MAP");
        int env_x;
        int env_z;
        if( map_env && sscanf(map_env, "%d,%d", &env_x, &env_z) == 2 )
        {
            viewer.map_x = env_x;
            viewer.map_z = env_z;
            start_in_world = 1;
        }
    }
    if( start_in_world )
        forced_model = -1;

    viewer.runner.io = ToriRS_IO_New();
    viewer.runner.queue = ToriRS_TaskQueue_New();
    viewer.runner.px = PlatformX_IO_New();
    assert(viewer.runner.io && viewer.runner.queue && viewer.runner.px);
    PlatformX_IO_InitDat1Disk(viewer.runner.px, disk);

    viewer.buildcache = dat1_buildcache_new();
    viewer.provider = dat1_buildcache_as_provider(viewer.buildcache);

    /* The decoders branch on cache identity, and the world load asserts it has
     * been stated. cache254 is the rev-254 RS2 dat1 cache (manifest_rs254.ini
     * [cache:boot]); TORIRS_SCANLINE_REV overrides the revision for other dat1
     * caches. */
    {
        const char* rev_env = getenv("TORIRS_SCANLINE_REV");
        int revision = rev_env ? atoi(rev_env) : 254;
        struct RSCache profile = RSCache_ProfileForIdentity(
            RSCACHE_GAME_RS2, RSCACHE_EPOCH_DAT1, revision, RSCACHE_QUIRK_NONE);
        CacheProvider_SetProfile(viewer.provider, &profile);
    }

    viewer.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    assert(viewer.scene);

    viewer.pane_branching = malloc(sizeof(int) * PANE_W * PANE_H);
    viewer.pane_scanline = malloc(sizeof(int) * PANE_W * PANE_H);
    viewer.pane_diff = malloc(sizeof(int) * PANE_W * PANE_H);
    assert(viewer.pane_branching && viewer.pane_scanline && viewer.pane_diff);

    if( start_in_world )
    {
        /* Skip the model sweep; keep one known-good model so `m` can toggle
         * back without a two-minute scan. */
        viewer.candidates[0].model_id = 148;
        viewer.candidate_count = 1;
    }
    else if( forced_model >= 0 )
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

    if( start_in_world )
    {
        viewer.mode = MODE_WORLD;
        viewer.auto_rotate = 0;
        if( !world_load(&viewer, viewer.map_x, viewer.map_z) )
        {
            fprintf(stderr, "world mode unavailable; falling back to model mode\n");
            viewer.mode = MODE_MODEL;
            reset_view(&viewer);
        }
    }

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
        "\nkeys  both modes: arrows look, +/- zoom or height, m model/world,\n"
        "                  v diff panel, x diff scale, r reset, q quit\n"
        "  model mode:     [ ] model, space turntable, t face alpha\n"
        "  world mode:     wasd pan, 1/2 map X, 3/4 map Z\n\n");

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

        render_subject(&viewer);
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
