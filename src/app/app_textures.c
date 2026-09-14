/*
 * Texture streaming: the want -> load -> publish sync the scene models depend on.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Scene models reference textures by face id, but the ToriDraw texture map
 * starts empty (reference: textures load on demand and faces skip-render
 * until they land). The ids come from model construction itself
 * (ToriDraw_ModelTextureWantsTake) — whatever built a model reported the
 * textures it needs — so this costs nothing per tick when no geometry was
 * built. Queue the loads and remember the ids; app_sync_textures_poll
 * publishes them into the scene as the loads land. Ids that fail stay marked
 * in the bridge and are never re-requested. */
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

void
app_sync_textures(struct App* app)
{
    int ids[256];
    int ready[256];
    int ready_count = 0;
    int id_count;

    id_count = ToriDraw_ModelTextureWantsTake(ids, 256);
    if( id_count == 0 )
        return;
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
        int already_pending = 0;

        if( id >= 0 && id < 2048 && app->bridge.texture_failed[id] )
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
         * flight. Do the pending-set test before creating the task; the old
         * order queued another decoder for every rebuild and only deduplicated
         * the publish list afterwards. */
        already_pending = AsyncPendingTextures_Has(&app->tex_pending, id);
        if( already_pending )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> skip (already pending)\n", id);
            continue;
        }

        /* Already decoded — publish it now, in the same tick the geometry that
         * wants it was built. Deferring to app_sync_textures_poll costs a frame,
         * and the frame it costs is the one that first draws the new models: the
         * raster skips every textured face whose texture is not in the scene map
         * yet. The QBD arena load spent that frame skipping ~1000 faces with both
         * of its textures sitting decoded in the provider. Loads that really are
         * in flight still go through the pending list below. */
        if( CacheProvider_TextureHas(app->provider, id) && app->bridge.scene )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> already in provider\n", id);
            ready[ready_count++] = id;
            continue;
        }

        {
            struct ToriRS_Task* task = CreateTask_TextureLoad(app->provider, id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
            if( app_tex_trace_enabled() )
                TORIRS_ERR(
                    "tex_trace: want id=%d -> load task %s\n",
                    id,
                    task ? "queued" : "REFUSED (provider returned no task)");
        }
        if( !AsyncPendingTextures_Add(&app->tex_pending, id) && app_tex_trace_enabled() )
            TORIRS_LOG("tex_trace: want id=%d -> DROPPED (pending list full)\n", id);
    }

    if( ready_count > 0 )
    {
        int published = UITreeSceneBridge_PublishTextures(&app->bridge, ready, ready_count);
        if( app_tex_trace_enabled() )
            TORIRS_LOG(
                "tex_trace: immediate publish %d ready -> %d published\n", ready_count, published);
        if( published )
            app->need_redraw = 1;
    }
}

/* Per-frame: publish any pending textures that finished loading; keep only
 * the ones still in flight (present in neither the provider nor the bridge's
 * failed set). */
void
app_sync_textures_poll(struct App* app)
{
    int ready[512];
    int ready_count = 0;
    int kept = 0;
    int const queue_idle = !app->runner.queue || !app->runner.queue->head;

    if( app->tex_pending.count == 0 )
        return;

    for( int i = 0; i < app->tex_pending.count; i++ )
    {
        int id = app->tex_pending.ids[i];

        if( id < 0 || id >= 2048 || app->bridge.texture_failed[id] )
        {
            if( app_tex_trace_enabled() )
                TORIRS_ERR("tex_trace: poll id=%d -> dropped (failed/out of range)\n", id);
            continue;
        }
        if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: poll id=%d -> dropped (resident)\n", id);
            continue;
        }
        if( CacheProvider_TextureHas(app->provider, id) )
        {
            ready[ready_count++] = id;
            continue;
        }

        /* A missing provider entry does not mean a failed texture while its
         * async load is still queued. Publishing it here used to mark it
         * failed on the very next frame, before a busy task runner reached the
         * request; every affected model face was then skipped forever. Once
         * the queue drains, absence is a real terminal load failure. */
        if( queue_idle )
        {
            app->bridge.texture_failed[id] = 1;
            if( app_tex_trace_enabled() )
                TORIRS_ERR(
                    "tex_trace: poll id=%d -> MARKED FAILED (queue idle, not in provider)\n", id);
        }
        else
        {
            app->tex_pending.ids[kept++] = id;
        }
    }
    AsyncPendingTextures_Keep(&app->tex_pending, kept);

    if( ready_count > 0 )
    {
        int published = UITreeSceneBridge_PublishTextures(&app->bridge, ready, ready_count);
        if( app_tex_trace_enabled() )
            TORIRS_LOG(
                "tex_trace: publish %d ready -> %d published (%d still pending)\n",
                ready_count,
                published,
                app->tex_pending.count);
        if( published )
            app->need_redraw = 1;
    }
}
