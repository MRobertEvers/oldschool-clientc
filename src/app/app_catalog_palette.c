/*
 * The Catalog palette's thumbnails. See app_catalog_palette.h.
 *
 * The models are built as the Preview builds them (app_map_editor.c
 * app_map_editor_preview_update: a loc's centrepiece row -- every model of
 * it --, an npc's parts merged, an obj's inventory model resized; each
 * recoloured and lit with its config's contrast and ambient) and rastered
 * with the Preview's default pitch and yaw, framed by their bounds the way
 * EditorPreviewCamera_TakeFit frames them, into one EDITOR_CATALOG_PALETTE_
 * THUMB_PX square per cache entry. The panel caches the answers per record;
 * this file keeps nothing but what it asked the cache for.
 */

#include "app/app_catalog_palette.h"

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
#define CATPAL_PARTS_MAX 16
/** Frames a wanted thumbnail may wait on its assets before it is answered
 *  with none (a record whose model never comes must not hold up the rest). */
#define CATPAL_WAIT_FRAMES 600

/** Raster `count` parts framed by their joint bounds into a `size` square. */
static void
catpal_raster(
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
catpal_queue(
    struct App* app,
    struct ToriRS_Task* task)
{
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);
}

/** 1 when model `model_id` is resident; else asks for it (when `ask`). */
static int
catpal_model_ready(
    struct App* app,
    int model_id,
    int ask)
{
    if( model_id <= 0 || CacheProvider_ModelHas(app->provider, model_id) )
        return 1;
    if( ask )
        catpal_queue(app, CreateTask_ModelLoad(app->provider, model_id));
    return 0;
}

static struct ToriDraw_Model*
catpal_model(
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
catpal_build(
    struct App* app,
    int kind,
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
                catpal_queue(app, CreateTask_LocLoad(app->provider, id));
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
            missing |= !catpal_model_ready(app, cfg->models[row][k], ask);
        if( missing )
            return -2;
        for( int k = 0; k < cfg->lengths[row] && count < CATPAL_PARTS_MAX; k++ )
        {
            if( cfg->models[row][k] <= 0 )
                continue;
            models[count] = catpal_model(app, cfg->models[row][k]);
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
        struct ToriDraw_Model* parts[CATPAL_PARTS_MAX];
        struct ToriDraw_ModelHandle hnd;
        int part_count = 0;
        int missing = 0;

        if( !npc )
        {
            if( ask )
                catpal_queue(app, CreateTask_NpcLoad(app->provider, id));
            return -1;
        }
        for( int i = 0; i < npc->models_count; i++ )
            missing |= !catpal_model_ready(app, npc->models[i], ask);
        if( missing )
            return -2;
        /* An npc body is its parts merged, as the entity path builds it. */
        for( int i = 0; i < npc->models_count && part_count < CATPAL_PARTS_MAX; i++ )
            if( npc->models[i] > 0 )
                parts[part_count++] = catpal_model(app, npc->models[i]);
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
                catpal_queue(app, CreateTask_ObjModelLoad(app->provider, ids, counts, 1));
            }
            return -1;
        }
        if( obj->inventory_model_id <= 0 )
            return 0;
        models[0] = catpal_model(app, obj->inventory_model_id);
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

void
app_catalog_palette_update(struct App* app)
{
    /* The record whose assets were asked for, the wait they were asked for
     * (config, models) and since when: each wait asked once, the record given
     * up on after CATPAL_WAIT_FRAMES. */
    static int asked_kind = -1;
    static int asked_id = -1;
    static int asked_stage = 0;
    static int asked_frames = 0;
    struct EditorUi* ui;
    struct ToriDraw_Model* models[CATPAL_PARTS_MAX];
    struct ToriDraw_ModelHandle parts[CATPAL_PARTS_MAX];
    int const size = EDITOR_CATALOG_PALETTE_THUMB_PX;
    int kind;
    int id;
    int slot;
    int count;
    int scene_id;
    toripixel_t* pixels;
    uint32_t* argb;

    assert(app);
    if( !app->editor || !app->editor_panel.visible || !app->editor_panel.ui || !app->scene || !app->provider )
        return;
    ui = app->editor_panel.ui;
    if( !Editor_CatalogPalette_ThumbWanted(ui, &kind, &id, &slot) )
        return;

    if( kind != asked_kind || id != asked_id )
    {
        asked_kind = kind;
        asked_id = id;
        asked_stage = 0;
        asked_frames = 0;
    }
    count = catpal_build(app, kind, id, 0, models);
    if( count < 0 && count != asked_stage )
    {
        /* A new wait: ask for what it needs, once. */
        asked_stage = count;
        count = catpal_build(app, kind, id, 1, models);
    }
    if( count < 0 )
    {
        if( ++asked_frames > CATPAL_WAIT_FRAMES )
            Editor_CatalogPalette_ThumbDone(ui, slot, 0);
        return;
    }
    if( count == 0 )
    {
        Editor_CatalogPalette_ThumbDone(ui, slot, 0);
        return;
    }

    for( int i = 0; i < count; i++ )
    {
        memset(&parts[i], 0, sizeof(parts[i]));
        parts[i].kind = TORIDRAWMK_MODEL;
        parts[i].u.model.model = models[i];
    }
    pixels = TORIDRAW_CALLOC((size_t)size * (size_t)size, sizeof(toripixel_t));
    assert(pixels);
    catpal_raster(app, parts, count, pixels, size, 0);
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
        scene_id = EDITOR_CATALOG_PALETTE_SCENE_BASE + slot;
        if( ToriDraw_SceneSpriteHas(app->scene, scene_id) )
            ToriDraw_SceneSpriteRemove(app->scene, scene_id);
        ToriDraw_SceneSpriteAdd(app->scene, scene_id, sprites, 1);
    }
    asked_kind = -1;
    asked_id = -1;
    Editor_CatalogPalette_ThumbDone(ui, slot, scene_id);
    app->need_redraw = 1;
}

#else /* __EMSCRIPTEN__ */

void
app_catalog_palette_update(struct App* app)
{
    (void)app;
}

#endif
