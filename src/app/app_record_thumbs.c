/*
 * Record thumbnails: one renderer for the editor's small pictures of loc,
 * npc and obj records. See app_record_thumbs.h.
 */

#include "app/app_record_thumbs.h"

#include "app/app_internal.h"
#include "editor/editor_preview_camera.h"
#include "editor/panels/edpanels.h"
#include "graphics/pixel_format.h"
#include "mem/torimem_toridraw.h"
#include "toridraw.h"
#include "toridraw_alloc.h"
#include "toridraw_math.h"
#include "toridraw_model_transform.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_sprite.h"

#include <assert.h>
#include <string.h>

#if !defined(__EMSCRIPTEN__)

/** Parts one record's thumbnail holds at most. */
#define THUMB_PARTS_MAX 16

enum ThumbState
{
    THUMB_FREE = 0,
    /** Asked for; the update renders it. */
    THUMB_WANTED,
    THUMB_READY,
    /** The record has no model (a real answer). */
    THUMB_NONE,
};

struct Thumb
{
    enum ThumbState state;
    uint32_t kind;
    int id;
    int px;
    int scene_id;
    /** The ask that last touched it: the least recent is evicted. */
    uint32_t used;
    /** The wait the assets were asked for (-1 config, -2 models), once. */
    int asked_stage;
    int asked_frames;
};

static struct Thumb g_thumbs[APP_RECORD_THUMBS];
static uint32_t g_thumb_clock;

/** Raster `count` parts framed by their joint bounds into a `size` square. */
static void
thumb_raster(
    struct App* app,
    struct ToriDraw_ModelHandle const* parts,
    int count,
    toripixel_t* pixels,
    int size,
    int yaw)
{
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position position;
    int radius = 0;
    int min_y = 0;
    int max_y = 0;
    int extent;
    int zoom;

    for( int i = 0; i < count; i++ )
    {
        struct ToriDraw_BoundsCylinder* b = ToriDraw_ModelGetBoundsCylinder(parts[i]);
        if( !b )
            continue;
        if( b->radius > radius )
            radius = b->radius;
        if( b->min_y < min_y )
            min_y = b->min_y;
        if( b->max_y > max_y )
            max_y = b->max_y;
    }
    /* EditorPreviewCamera_TakeFit's framing, at the thumbnail's size. */
    extent = 2 * radius > max_y - min_y ? 2 * radius : max_y - min_y;
    zoom = (extent * 9) / 2;
    if( zoom < EDITOR_PREVIEW_FIT_ZOOM_MIN )
        zoom = EDITOR_PREVIEW_FIT_ZOOM_MIN;
    if( zoom > EDITOR_PREVIEW_FIT_ZOOM_MAX )
        zoom = EDITOR_PREVIEW_FIT_ZOOM_MAX;

    memset(&view_port, 0, sizeof(view_port));
    view_port.width = size;
    view_port.height = size;
    view_port.clip_right = size;
    view_port.clip_bottom = size;
    view_port.x_center = size / 2;
    view_port.y_center = size / 2;
    view_port.stride = size;

    memset(&camera, 0, sizeof(camera));
    camera.pitch = EDITOR_PREVIEW_DEFAULT_PITCH;
    camera.projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
    camera.projection_scale = EditorPreviewCamera_ProjectionScale(size, size);
    camera.near_plane_z = 1;

    memset(&position, 0, sizeof(position));
    position.yaw = (EDITOR_PREVIEW_DEFAULT_YAW + yaw) & 2047;
    position.y = ((ToriDraw_Sin(camera.pitch) * zoom) >> 16) - (min_y + max_y) / 2;
    position.z = (ToriDraw_Cos(camera.pitch) * zoom) >> 16;
    for( int i = 0; i < count; i++ )
        ToriDraw_RenderModelWithTable(
            parts[i], app->scene, &position, &view_port, &camera, pixels, ToriDraw_KernelGetSpriteBaker());
}

/** Queue `task` on the asset runner (NULL: nothing to queue). */
static void
thumb_queue(
    struct App* app,
    struct ToriRS_Task* task)
{
    if( task )
        ToriRS_TaskQueue_Add(app->frame_runner.queue, task);
}

/** 1 when model `model_id` is resident; else asks for it (when `ask`). */
static int
thumb_model_ready(
    struct App* app,
    int model_id,
    int ask)
{
    if( model_id <= 0 || CacheProvider_ModelHas(app->provider, model_id) )
        return 1;
    if( ask )
        thumb_queue(app, CreateTask_ModelLoad(app->provider, model_id));
    return 0;
}

static struct ToriDraw_Model*
thumb_model(
    struct App* app,
    int model_id)
{
    struct ToriRS_Model* rs_model = CacheProvider_ModelGet(app->provider, model_id);
    struct ToriDraw_Model* model;

    assert(rs_model);
    model = ToriDraw_ModelFromToriRS(rs_model);
    assert(model);
    return model;
}

/**
 * The record's models, built and lit, into `models` (owned by the caller).
 * Returns how many (0: the record has none -- a real answer), or while
 * something is still loading (asked for when `ask`) -1 for its config, -2 for
 * its models: a config that lands turns the wait into a different one, which
 * is asked for in its turn.
 */
static int
thumb_build(
    struct App* app,
    uint32_t kind,
    int id,
    int ask,
    struct ToriDraw_Model** models)
{
    int count = 0;

    if( kind == CACHEPROVIDER_CATALOG_LOC )
    {
        struct ToriRS_Location* cfg = CacheProvider_LocationGet(app->provider, id);
        int row = 0;
        int missing = 0;

        if( !cfg )
        {
            if( ask )
                thumb_queue(app, CreateTask_LocLoad(app->provider, id));
            return -1;
        }
        if( cfg->shapes_and_model_count <= 0 || !cfg->models || !cfg->lengths )
            return 0;
        /* The Preview's row: the centrepiece's when the loc has shapes. */
        if( cfg->shapes )
            for( int i = 0; i < cfg->shapes_and_model_count; i++ )
                if( cfg->shapes[i] == RSCACHE_LOC_SHAPE_SCENERY )
                {
                    row = i;
                    break;
                }
        for( int k = 0; k < cfg->lengths[row]; k++ )
            missing |= !thumb_model_ready(app, cfg->models[row][k], ask);
        if( missing )
            return -2;
        for( int k = 0; k < cfg->lengths[row] && count < THUMB_PARTS_MAX; k++ )
        {
            if( cfg->models[row][k] <= 0 )
                continue;
            models[count] = thumb_model(app, cfg->models[row][k]);
            for( int c = 0; c < cfg->recolor_count; c++ )
                ToriDraw_ModelRecolor(models[count], cfg->recolors_from[c], cfg->recolors_to[c]);
            count++;
        }
        for( int i = 0; i < count; i++ )
        {
            struct ToriDraw_ModelHandle hnd;
            ToriDraw_ModelSetBoundsCylinder(models[i]);
            ToriDraw_ModelDropNonSdTextures(app->provider, models[i]);
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = models[i];
            ToriDraw_LightModelScene(hnd, cfg->contrast, cfg->ambient);
        }
        return count;
    }
    if( kind == CACHEPROVIDER_CATALOG_NPC )
    {
        struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, id);
        struct ToriDraw_Model* parts[THUMB_PARTS_MAX];
        struct ToriDraw_ModelHandle hnd;
        int part_count = 0;
        int missing = 0;

        if( !npc )
        {
            if( ask )
                thumb_queue(app, CreateTask_NpcLoad(app->provider, id));
            return -1;
        }
        for( int i = 0; i < npc->models_count; i++ )
            missing |= !thumb_model_ready(app, npc->models[i], ask);
        if( missing )
            return -2;
        /* An npc body is its parts merged, as the entity path builds it. */
        for( int i = 0; i < npc->models_count && part_count < THUMB_PARTS_MAX; i++ )
            if( npc->models[i] > 0 )
                parts[part_count++] = thumb_model(app, npc->models[i]);
        if( part_count == 0 )
            return 0;
        models[0] = part_count == 1 ? parts[0] : ToriDraw_ModelNewMerge(parts, part_count);
        assert(models[0]);
        if( part_count > 1 )
            for( int i = 0; i < part_count; i++ )
                ToriDraw_ModelFree(parts[i]);
        for( int i = 0; i < npc->recolor_count; i++ )
            ToriDraw_ModelRecolor(models[0], npc->recolors_from[i], npc->recolors_to[i]);
        ToriDraw_ModelSetBoundsCylinder(models[0]);
        ToriDraw_ModelDropNonSdTextures(app->provider, models[0]);
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = models[0];
        ToriDraw_LightModelScene(hnd, npc->contrast, npc->ambient);
        return 1;
    }
    if( kind == CACHEPROVIDER_CATALOG_OBJ )
    {
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, id);
        struct ToriDraw_ModelHandle hnd;

        if( !obj || (obj->inventory_model_id > 0 && !CacheProvider_ModelHas(app->provider, obj->inventory_model_id)) )
        {
            if( ask && ObjModelLoad_NeedsWork(app->provider, id, 1) )
            {
                int const ids[1] = { id };
                int const counts[1] = { 1 };
                thumb_queue(app, CreateTask_ObjModelLoad(app->provider, ids, counts, 1));
            }
            return -1;
        }
        if( obj->inventory_model_id <= 0 )
            return 0;
        models[0] = thumb_model(app, obj->inventory_model_id);
        if( obj->resize_x != 128 || obj->resize_y != 128 || obj->resize_z != 128 )
            ToriDraw_ModelScale(models[0], obj->resize_x, obj->resize_z, obj->resize_y);
        for( int i = 0; i < obj->recolor_count; i++ )
            ToriDraw_ModelRecolor(models[0], obj->recolors_from[i], obj->recolors_to[i]);
        ToriDraw_ModelSetBoundsCylinder(models[0]);
        ToriDraw_ModelDropNonSdTextures(app->provider, models[0]);
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = models[0];
        ToriDraw_LightModelScene(hnd, obj->contrast, obj->ambient);
        return 1;
    }
    return 0;
}

/* ---- the cache ------------------------------------------------------------------ */

static struct Thumb*
thumb_find(
    uint32_t kind,
    int id,
    int px)
{
    for( int i = 0; i < APP_RECORD_THUMBS; i++ )
    {
        struct Thumb* t = &g_thumbs[i];
        if( t->state != THUMB_FREE && t->kind == kind && t->id == id && t->px == px )
            return t;
    }
    return NULL;
}

/** A free entry, else the least recently asked one (its sprite is replaced
 *  when the new picture lands). */
static struct Thumb*
thumb_take(void)
{
    struct Thumb* victim = NULL;

    for( int i = 0; i < APP_RECORD_THUMBS; i++ )
    {
        struct Thumb* t = &g_thumbs[i];
        if( t->state == THUMB_FREE )
            return t;
        if( !victim || t->used < victim->used )
            victim = t;
    }
    assert(victim);
    return victim;
}

int
app_record_thumb(
    struct App* app,
    uint32_t kind,
    int id,
    int px)
{
    struct Thumb* t;

    assert(app);
    assert(id >= 0);
    assert(px > 0);
    if( !app->provider || !app->scene )
        return 0;
    if( kind != CACHEPROVIDER_CATALOG_LOC && kind != CACHEPROVIDER_CATALOG_NPC && kind != CACHEPROVIDER_CATALOG_OBJ )
        return -1;
    t = thumb_find(kind, id, px);
    if( !t )
    {
        t = thumb_take();
        memset(t, 0, sizeof(*t));
        t->state = THUMB_WANTED;
        t->kind = kind;
        t->id = id;
        t->px = px;
    }
    t->used = ++g_thumb_clock;
    if( t->state == THUMB_READY )
        return t->scene_id;
    if( t->state == THUMB_NONE )
        return -1;
    return 0;
}

/** Render entry `index` now: its assets are resident. */
static void
thumb_render(
    struct App* app,
    int index)
{
    struct Thumb* t = &g_thumbs[index];
    struct ToriDraw_Model* models[THUMB_PARTS_MAX];
    struct ToriDraw_ModelHandle parts[THUMB_PARTS_MAX];
    int const size = t->px;
    int count;
    toripixel_t* pixels;
    uint32_t* argb;
    int scene_id;

    count = thumb_build(app, t->kind, t->id, 0, models);
    assert(count > 0);
    for( int i = 0; i < count; i++ )
    {
        memset(&parts[i], 0, sizeof(parts[i]));
        parts[i].kind = TORIDRAWMK_MODEL;
        parts[i].u.model.model = models[i];
    }
    pixels = TORIDRAW_CALLOC((size_t)size * (size_t)size, sizeof(toripixel_t));
    assert(pixels);
    thumb_raster(app, parts, count, pixels, size, 0);
    for( int i = 0; i < count; i++ )
        ToriDraw_ModelFree(models[i]);
    argb = TORIDRAW_MALLOC((size_t)size * (size_t)size * sizeof(uint32_t)); /* the sprite owns it */
    assert(argb);
    for( int i = 0; i < size * size; i++ )
    {
        uint32_t const rgb = toripixel_to_argb8888(pixels[i]) & 0xFFFFFFu;
        argb[i] = rgb ? (rgb | 0xFF000000u) : 0u;
    }
    TORIDRAW_FREE(pixels);
    {
        struct ToriDraw_Sprite** sprites = TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
        assert(sprites);
        sprites[0] = ToriDraw_SpriteNewFromArgbOwned(argb, size, size);
        assert(sprites[0]);
        scene_id = APP_RECORD_THUMB_SCENE_BASE + index;
        if( ToriDraw_SceneSpriteHas(app->scene, scene_id) )
            ToriDraw_SceneSpriteRemove(app->scene, scene_id);
        ToriDraw_SceneSpriteAdd(app->scene, scene_id, sprites, 1);
    }
    t->scene_id = scene_id;
    t->state = THUMB_READY;
    app->need_redraw = 1;
}

void
app_record_thumbs_update(struct App* app)
{
    int rendered = 0;

    assert(app);
    if( !app->editor || !app->editor_panel.visible || !app->editor_panel.ui || !app->scene || !app->provider )
        return;
    for( int i = 0; i < APP_RECORD_THUMBS && rendered < APP_RECORD_THUMBS_PER_FRAME; i++ )
    {
        struct Thumb* t = &g_thumbs[i];
        struct ToriDraw_Model* models[THUMB_PARTS_MAX];
        int count;
        if( t->state != THUMB_WANTED )
            continue;
        count = thumb_build(app, t->kind, t->id, 0, models);
        if( count < 0 && count != t->asked_stage )
        {
            /* A new wait: ask for what it needs, once. */
            t->asked_stage = count;
            count = thumb_build(app, t->kind, t->id, 1, models);
        }
        if( count < 0 )
        {
            if( ++t->asked_frames > APP_RECORD_THUMB_WAIT_FRAMES )
                t->state = THUMB_NONE;
            continue;
        }
        if( count == 0 )
        {
            t->state = THUMB_NONE;
            continue;
        }
        for( int k = 0; k < count; k++ )
            ToriDraw_ModelFree(models[k]);
        thumb_render(app, i);
        rendered++;
    }
}

#else /* __EMSCRIPTEN__ */

int
app_record_thumb(
    struct App* app,
    uint32_t kind,
    int id,
    int px)
{
    (void)app;
    (void)kind;
    (void)id;
    (void)px;
    return -1;
}

void
app_record_thumbs_update(struct App* app)
{
    (void)app;
}

#endif
