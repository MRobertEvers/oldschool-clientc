/*
 * Cache id lookups: fonts, interface components, settings, and the scene fonts for hitsplats and the minimenu.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/*
 * Cache font id for a RevConfig `[font:<name>]` section on this cache.
 *
 * -1 when the profile does not declare it, which every caller already handles
 * the same way it handles a font that has not finished loading: draw nothing,
 * or fall back to whatever font a text node already resolved.
 */
int
app_font_cache_id(
    struct App const* app,
    char const* font_name)
{
    assert(app);
    assert(font_name);
    return RevConfigRefs_FontCacheId(
        &app->revconfig_refs, font_name, app->cfg.cache_kind == APP_CACHE_DAT1);
}

int
app_font_b12_cache_id(struct App const* app)
{
    return app_font_cache_id(app, APP_FONT_B12);
}

/*
 * Packed component uid — `(iface << 16) | child` — for a RevConfig
 * `[iface:<name>]` section, or -1 when this profile declares no such interface.
 *
 * The CHILD number stays in C: which component of the XP panel holds the stat
 * listener is a fact about that interface's own layout, and it travels with the
 * interface. Which id the interface HAS does not, so that half is the
 * profile's.
 */
int
app_iface_com(
    struct App const* app,
    char const* iface_name,
    int child)
{
    int iface;
    assert(app);
    assert(iface_name);
    assert(child >= 0);
    iface = RevConfigRefs_Get(&app->revconfig_refs, "iface", iface_name);
    if( iface < 0 )
        return -1;
    return (iface << 16) | child;
}

/** Id of a `[setting:<name>]` row, or -1 when this profile has no such row. */
int
app_setting_id(
    struct App const* app,
    char const* setting_name)
{
    assert(app);
    assert(setting_name);
    return RevConfigRefs_Get(&app->revconfig_refs, "setting", setting_name);
}

/* Scene font for hitsplat numbers; queues the load on a miss the same way
 * app_minimenu_font_scene_id does, and returns -1 until it lands.
 *
 * -1, not 0: scene font ids ARE cache font ids, and dat1 p11 is cache id 0 —
 * the same trap that once left every p11 label invisible. */
int
app_hitsplat_font_scene_id(struct App* app)
{
    int font_cache_id = app_font_cache_id(app, APP_FONT_P11);
    int scene_id;
    if( font_cache_id < 0 )
        return -1;
    scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
    if( scene_id < 0 )
    {
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
    }
    return scene_id;
}

int
app_minimenu_font_scene_id(struct App* app)
{
    int font_cache_id = app_font_b12_cache_id(app);
    int scene_id =
        font_cache_id >= 0 ? UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id) : -1;
    if( scene_id <= 0 && font_cache_id >= 0 )
    {
        /* Queue the load (no blocking drain — the boot task awaits this font
         * before binding the configured overlay models, so at runtime a miss
         * just falls through to the text-node scan below until it lands). */
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
    }
    if( scene_id <= 0 )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( !node->freed && node->type == UIELEM_RS_TEXT && node->u.rs_text.font_id > 0 )
            {
                scene_id = node->u.rs_text.font_id;
                break;
            }
        }
    }
    return scene_id;
}
