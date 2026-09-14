/*
 * The developer chrome instance: fonts, scale, check style, and the in-canvas fallback for retained panel primitives.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_panel_overlay_to_chrome(
    struct App const* app,
    int index,
    struct ToriRSChromePrim* out);
static void
app_chrome_fonts_resolve(struct App* app);

/** Convert one retained panel primitive for the in-canvas fallback. The
 * translation is ToriRSChromePanelDraw_ToChromePrim's; this is the spelling
 * that asks whether the primitive is showing at all. */
static int
app_panel_overlay_to_chrome(
    struct App const* app,
    int index,
    struct ToriRSChromePrim* out)
{
    struct UITreeEntityOverlay visible;

    assert(app);
    assert(out);
    if( !app_plugin_panel_overlay_visible(app, index, &visible) )
        return 0;
    return ToriRSChromePanelDraw_ToChromePrim(&visible, out);
}

/**
 * Both chrome instances' display lists, then panel-local custom primitives.
 *
 * Rebuilt only when the pair actually differs from what was merged last: the
 * prim arrays are handed downstream by pointer and a steady frame must stay a
 * pointer copy, which is the property the whole retained design is for. The
 * cheap comparison is the two counts plus the two damage states, and Build
 * having already decided nothing changed is what makes both stable.
 *
 * The plugin window goes SECOND, so it draws over the developer readout: it is
 * the one a player opened, and a frame-time counter on top of it would be a
 * developer tool covering a user's window.
 */
struct ToriRSChromePrim const*
app_chrome_merged_prims(
    struct App* app,
    int* out_count)
{
    int dbg_count = 0;
    int win_count = 0;
    struct ToriRSChromePrim const* dbg = ToriRSChrome_Prims(&app->dbg_ui, &dbg_count);
    struct ToriRSChromePrim const* win = ToriRSChrome_Prims(&app->plugin_ui, &win_count);

    assert(out_count);

    /*
     * Nothing to merge: hand the developer chrome's own array straight out, so
     * the common case -- no plugin window open -- costs exactly what it did
     * before this existed.
     *
     * WEB/BROWSER rebuild the window as DOM controls, so putting its prims in
     * the canvas as well would draw it twice. BUFFER is the only internal
     * presentation that consumes the display list here.
     */
    if( win_count == 0 || app->plugin_exec_kind != TORIRS_CHROME_EXEC_BUFFER )
    {
        *out_count = dbg_count;
        return dbg;
    }

    /* Exact, not a heuristic: the serials move on every rebuild, including one
     * that changed a string without changing the prim count. */
    if( app->chrome_merged_dbg != app->dbg_ui.build_serial ||
        app->chrome_merged_win != app->plugin_ui.build_serial ||
        app->chrome_merged_panel != app->panel_overlay_revision )
    {
        int n = dbg_count < APP_CHROME_PRIMS_MAX ? dbg_count : APP_CHROME_PRIMS_MAX;
        memcpy(app->chrome_merged, dbg, (size_t)n * sizeof(*dbg));
        if( n < APP_CHROME_PRIMS_MAX )
        {
            int const take =
                win_count < APP_CHROME_PRIMS_MAX - n ? win_count : APP_CHROME_PRIMS_MAX - n;
            memcpy(&app->chrome_merged[n], win, (size_t)take * sizeof(*win));
            n += take;
        }
        for( int i = 0; i < app->panel_overlay_count && n < APP_CHROME_PRIMS_MAX; i++ )
            if( app_panel_overlay_to_chrome(app, i, &app->chrome_merged[n]) )
                n++;
        app->chrome_merged_dbg = app->dbg_ui.build_serial;
        app->chrome_merged_win = app->plugin_ui.build_serial;
        app->chrome_merged_panel = app->panel_overlay_revision;
        app->chrome_merged_count = n;
    }
    *out_count = app->chrome_merged_count;
    return app->chrome_merged;
}

/*
 * Point the tree's overlay components at the faces baked for the current
 * chrome scale.
 *
 * Split out because it runs twice: once when the scale is set, and again after
 * a tree rebuild, which resolves the ids itself at bake time but from whatever
 * scale the bridge is holding. Both paths end at the same three ids.
 */
static void
app_chrome_fonts_resolve(struct App* app)
{
    int small;
    int menu;
    int body;

    assert(app);
    /* Before the tree exists there is nothing to point at, and the bake will
     * resolve these itself from the scale the bridge is now holding. Not a
     * contract violation: App_SetChromeScale is legitimately called at boot,
     * ahead of the first build. */
    if( !app->tree )
        return;
    small = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_SMALL);
    menu = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_MENU);
    body = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_BODY);
    UITree_DebugOverlaySetFontIds(app->tree, small, menu, body);
}

int
App_SetChromeScale(
    struct App* app,
    int scale)
{
    assert(app);
    if( scale < TORIRS_CHROME_SCALE_MIN )
        scale = TORIRS_CHROME_SCALE_MIN;
    /* Clamped, not asserted: this number comes from the DISPLAY, and a 4x
     * monitor is a fact about the world rather than a caller's bug. Chrome one
     * baked size below the display's density is a little small; an assert here
     * would be a crash on a machine nobody tested on. */
    if( scale > TORIRS_CHROME_SCALE_MAX )
        scale = TORIRS_CHROME_SCALE_MAX;
    if( ToriRSChrome_Scale(&app->dbg_ui) == scale )
        return 0;

    ToriRSChrome_SetScale(&app->dbg_ui, scale);
    /* Both instances, because there is one scale: the font ids resolved below
     * are shared, so a plugin window left at 1x would lay its rows out for a
     * face the renderer draws at 2x -- text overflowing boxes sized for a
     * smaller font, which is the exact failure SetScale exists to prevent. */
    ToriRSChrome_SetScale(&app->plugin_ui, scale);
    UITreeSceneBridge_SetChromeScale(&app->bridge, scale);
    app_chrome_fonts_resolve(app);
    return 1;
}

int
App_ChromeScale(struct App const* app)
{
    assert(app);
    return ToriRSChrome_Scale(&app->dbg_ui);
}

int
App_SetChromeCheckStyle(
    struct App* app,
    int style)
{
    assert(app);
    if( ToriRSChrome_CheckStyle(&app->dbg_ui) == style )
        return 0;
    /* Both instances, because there is one answer to "what does a checkbox
     * look like here" -- the same rule App_SetChromeScale keeps, and for a
     * sharper reason: the two panels are commonly on screen together. */
    ToriRSChrome_SetCheckStyle(&app->dbg_ui, style);
    ToriRSChrome_SetCheckStyle(&app->plugin_ui, style);
    return 1;
}

int
App_ChromeCheckStyle(struct App const* app)
{
    assert(app);
    return ToriRSChrome_CheckStyle(&app->dbg_ui);
}
