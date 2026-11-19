/*
 * Texture streaming: the want -> load -> publish sync the scene models depend
 * on.
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
 * built. Each is wanted through the assets table (ASSET_KIND_TEXTURE), and
 * app_sync_textures_poll publishes them into the scene as the loads land. An
 * id that fails is remembered by the table and never asked for again. */
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

/* ASSET_KIND_TEXTURE's landing, from app_sync_textures_poll: publish the one
 * texture into the scene. A load that ended without it landed FAILED, and the
 * table remembers that (it was texture_failed[2048], which forgot every id
 * past 2047). */
static void
app_texture_landed(
    void* ctx,
    struct AssetKey key,
    struct AssetWaiterHandle handle,
    enum AssetOutcome outcome,
    uint32_t late_ms)
{
    struct App* app = ctx;
    int id = key.id;

    (void)handle;
    (void)late_ms;
    if( outcome != ASSET_OUTCOME_LANDED )
    {
        if( app_tex_trace_enabled() )
            TORIRS_ERR("tex_trace: land id=%d -> MARKED FAILED (load ended without it)\n", id);
        return;
    }
    if( UITreeSceneBridge_TextureResident(&app->bridge, id) )
    {
        if( app_tex_trace_enabled() )
            TORIRS_LOG("tex_trace: land id=%d -> dropped (resident)\n", id);
        return;
    }
    if( UITreeSceneBridge_PublishTextures(&app->bridge, &id, 1) )
    {
        if( app_tex_trace_enabled() )
            TORIRS_LOG("tex_trace: land id=%d -> published\n", id);
        RedrawRequest_Raise(&app->redraw_request);
        return;
    }
    /* In the provider but not publishable (no texels, or past the scene's
     * texture map): asking again would get the same answer. */
    Assets_Fail(app->assets, key);
}

void
app_sync_textures(struct App* app)
{
    int ids[256];
    int ready[256];
    int ready_count = 0;
    int id_count;
    struct AssetWaiter waiter;

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
    memset(&waiter, 0, sizeof(waiter));
    waiter.land = app_texture_landed;
    waiter.ctx = app;

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
         * flight: the table holds the one load and the one landing, so a
         * rebuild asks nothing more. */
        if( state != ASSET_STATE_NONE )
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
         * in flight land through the table below. */
        if( CacheProvider_TextureHas(app->provider, id) && app->bridge.scene )
        {
            if( app_tex_trace_enabled() )
                TORIRS_LOG("tex_trace: want id=%d -> already in provider\n", id);
            ready[ready_count++] = id;
            continue;
        }

        if( app_tex_trace_enabled() )
            TORIRS_ERR("tex_trace: want id=%d -> wanted\n", id);
        (void)Assets_Want(app->assets, key, &waiter);
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

/* Per-frame: land the textures whose loads ended (publishing them, or
 * remembering that they failed). A load still running is simply not landed
 * yet -- the table knows it is in flight, so nothing has to guess from an idle
 * queue that an absent texture has failed. */
void
app_sync_textures_poll(struct App* app)
{
    int landed = Assets_Land(app->assets, ASSET_KIND_BIT(ASSET_KIND_TEXTURE));
    if( landed > 0 && app_tex_trace_enabled() )
        TORIRS_LOG(
            "tex_trace: landed %d (%d still pending)\n",
            landed,
            Assets_Count(app->assets, ASSET_KIND_TEXTURE));
}
