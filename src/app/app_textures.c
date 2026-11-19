/*
 * Texture streaming: the want -> load -> publish sync the scene models depend
 * on.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include <stddef.h>

/* Scene models reference textures by face id, but the ToriDraw texture map
 * starts empty (reference: textures load on demand and faces skip-render
 * until they land). The ids come from model construction itself
 * (ToriDraw_ModelTextureWantsTake) — whatever built a model reported the
 * textures it needs — so this costs nothing per tick when no geometry was
 * built. Each one not loaded yet is one parallel task (Task_TexturePublish)
 * that awaits it through the assets table (ASSET_KIND_TEXTURE) and publishes
 * it into the scene when it lands. An id that fails is remembered by the
 * table and never asked for again. */
/* #region agent log — TORIRS_TEX_TRACE=1 narrates the whole want -> request ->
 * provider -> publish handoff, one line per id per decision. The gap between a
 * texture the loader created and a texture the raster can see has no other
 * observer: every stage on the way silently `continue`s. */
int
app_tex_trace_enabled(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_TEX_TRACE") ? 1 : 0;
    return enabled;
}

int g_tex_trace_frame = 0;

int
app_tex_trace_frame(void)
{
    return g_tex_trace_frame;
}
/* #endregion */

/* ---- Task_TexturePublish ----------------------------------------------- *
 *
 * One texture a built model wants and the provider does not hold yet: wait
 * for it, then publish it into the scene. Keyed on the texture id; the scene
 * is whatever app->bridge holds when it lands (a texture is not bound to a
 * scene serial: a rebuild wants the same ids again and a published one is
 * kept). A load that ended without it is FAILED, which the table remembers
 * (it was texture_failed[2048], which forgot every id past 2047). It was the
 * table's last callback waiter but the music install's. */
enum
{
    APP_TASK_TEXTURE_PUBLISH = RS_PENDING_KINDS_ASSETS_FIRST + 0,
};

struct Task_TexturePublish
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    int id;
};

static int
Task_TexturePublish_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_TexturePublish* self = (struct Task_TexturePublish*)base;
    struct App* app = self->app;
    int id = self->id;
    (void)io;

    PT_BEGIN(&self->pt);

    /* 1. Wait for it. */
    PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_TEXTURE, id, 0));

    /* 2. Cancelled (a logout). */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* 3. Still wanted: it came, and nothing published it meanwhile. */
    if( !RS_ParallelTask_AssetHere(&self->par) )
    {
        if( app_tex_trace_enabled() )
            TORIRS_ERR("tex_trace: land id=%d -> MARKED FAILED (load ended without it)\n", id);
        PT_EXIT(&self->pt);
    }
    if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
    {
        if( app_tex_trace_enabled() )
            TORIRS_LOG("tex_trace: land id=%d -> dropped (resident)\n", id);
        PT_EXIT(&self->pt);
    }

    /* 4. Apply. In the provider but not publishable (no texels, or past the
     *    scene's texture map): asking again would get the same answer. */
    if( UITreeSceneBridge_PublishTextures(&app->bridge, &id, 1) )
    {
        if( app_tex_trace_enabled() )
            TORIRS_LOG("tex_trace: land id=%d -> published\n", id);
        RedrawRequest_Raise(&app->redraw_request);
    }
    else
        Assets_Fail(app->assets, AssetKey_Make(ASSET_KIND_TEXTURE, id, 0));

    PT_END(&self->pt);
}

static void
Task_TexturePublish_Free(struct ToriRS_Task* base)
{
    struct Task_TexturePublish* self = (struct Task_TexturePublish*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_TexturePublish_VTable = {
    .run = Task_TexturePublish_Run,
    .free = Task_TexturePublish_Free,
};

static void
app_texture_publish_add(
    struct App* app,
    int id)
{
    struct Task_TexturePublish* task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));

    assert(task);
    task->task.vtable = &Task_TexturePublish_VTable;
    strncpy(task->task.name, "TexturePublish", sizeof(task->task.name) - 1);
    task->app = app;
    task->id = id;
    PT_INIT(&task->pt);
    App_ParallelTaskAdd(
        app, &task->task, &task->par, APP_TASK_TEXTURE_PUBLISH, RS_PendingKey_Id(id, 0), -1);
}

int
App_TexturePublishPending(
    struct App const* app,
    int id)
{
    assert(app);
    return RS_Pending_Find(app->pending, APP_TASK_TEXTURE_PUBLISH, RS_PendingKey_Id(id, 0)) != NULL;
}

void
app_sync_textures(struct App* app)
{
    int ids[256];
    int id_count;

    id_count = ToriDraw_ModelTextureWantsTake(ids, 256);
    if( id_count == 0 )
        return;
    App_TexturesWant(app, ids, id_count);
}

void
App_TexturesWant(
    struct App* app,
    int const* ids,
    int id_count)
{
    int ready[256];
    int ready_count = 0;

    assert(app);
    assert(ids);
    assert(id_count <= 256);
    if( getenv("TORIRS_TEX_DEBUG") )
    {
        TORIRS_LOG("tex_wants drained %d:", id_count);
        for( int i = 0; i < id_count; i++ )
            TORIRS_LOG(" %d", ids[i]);
        TORIRS_LOG("\n");
    }
    for( int i = 0; i < id_count; i++ )
    {
        int const id = ids[i];
        struct AssetKey const key = AssetKey_Make(ASSET_KIND_TEXTURE, id, 0);
        enum AssetState const state = Assets_State(app->assets, key);

        if( state == ASSET_STATE_FAILED )
        {
            if( app_tex_trace_enabled() )
                TORIRS_ERR("tex_trace: want id=%d -> skip (already failed)\n", id);
            continue;
        }
        if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> skip (already resident)\n", id);
            continue;
        }

        /* A model may be rebuilt while its first texture request is still in
         * flight: the table holds the one load and its task the one
         * publish, so a rebuild asks nothing more. */
        if( state != ASSET_STATE_NONE || App_TexturePublishPending(app, id) )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> skip (already pending)\n", id);
            continue;
        }

        /* Already decoded — publish it now, in the same tick the geometry that
         * wants it was built. Deferring to a publish task costs a frame,
         * and the frame it costs is the one that first draws the new models: the
         * raster skips every textured face whose texture is not in the scene map
         * yet. The QBD arena load spent that frame skipping ~1000 faces with both
         * of its textures sitting decoded in the provider. Loads that really are
         * in flight are published by their task. */
        if( CacheProvider_TextureHas(app->provider, id) && app->bridge.scene )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> already in provider\n", id);
            ready[ready_count++] = id;
            continue;
        }

        if( app_tex_trace_enabled() )
            TORIRS_ERR("tex_trace: want id=%d -> wanted\n", id);
        app_texture_publish_add(app, id);
    }

    if( ready_count > 0 )
    {
        int published = UITreeSceneBridge_PublishTextures(&app->bridge, ready, ready_count);
        if( app_tex_trace_enabled() )
            TORIRS_LOG(
                "tex_trace: immediate publish %d ready -> %d published\n", ready_count, published);
        /* One the provider holds but the scene cannot take is not asked for
         * again. */
        for( int i = 0; i < ready_count; i++ )
            if( !UITreeSceneBridge_TextureResident(&app->bridge, ready[i]) )
                Assets_Fail(app->assets, AssetKey_Make(ASSET_KIND_TEXTURE, ready[i], 0));
        if( published )
            RedrawRequest_Raise(&app->redraw_request);
    }
}
