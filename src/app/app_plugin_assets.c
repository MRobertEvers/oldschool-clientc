/*
 * Plugin assets on disk and screenshots, including the capture fallback
 * render.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_plugin_asset_saved_path(
    struct App* app,
    char const* plugin,
    char const* name,
    char* out,
    size_t out_size);
static void
app_plugin_screenshot_path(
    struct App* app,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out,
    size_t out_size);
static int
app_capture_fallback_render(
    struct App* app,
    int* pixels,
    int width,
    int height);
static void
app_plugin_screenshots_write(
    struct App* app,
    int const* pixels,
    int width,
    int height);

/* -------------------------------------------------------- plugin assets */

/*
 * Where a plugin's SAVED asset lives: beside plugin_prefs.ini, under a
 * directory of the plugin's own.
 *
 * Derived from the prefs path rather than declared separately so that the two
 * cannot drift: TORIRS_PLUGIN_PREFS moves the client's plugin state somewhere
 * else, and a plugin's saved files are part of that state. An empty prefs path
 * means persistence is off for this run (a headless test), and the empty
 * result it produces is what makes a read fall straight through to the shipped
 * copy and a write refuse -- neither of which should leave a file behind.
 */
static void
app_plugin_asset_saved_path(
    struct App* app,
    char const* plugin,
    char const* name,
    char* out,
    size_t out_size)
{
    char const* prefs;
    char const* slash;

    assert(app);
    assert(plugin);
    assert(name);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    prefs = app->plugin_prefs_path;
    if( !prefs || !*prefs )
        return;

    slash = strrchr(prefs, '/');
    if( slash )
        snprintf(
            out,
            out_size,
            "%.*s/%s/%s/%s",
            (int)(slash - prefs),
            prefs,
            PLUGIN_ASSET_SAVED_DIR,
            plugin,
            name);
    else
        snprintf(out, out_size, "%s/%s/%s", PLUGIN_ASSET_SAVED_DIR, plugin, name);
}

int
app_plugin_asset_read(
    void* user,
    char const* plugin,
    char const* name)
{
    struct App* app = (struct App*)user;
    char saved[TORIRS_IOITEM_MAX_PATH];

    assert(app);
    assert(plugin);
    assert(name);

    if( !app->plugins )
        return 0;
    app_plugin_asset_saved_path(app, plugin, name, saved, sizeof(saved));
    ToriRS_TaskQueue_Add(
        app->runner.queue, CreateTask_PluginAssetRead(app->plugins, plugin, name, saved));
    return 1;
}

int
app_plugin_asset_write(
    void* user,
    char const* plugin,
    char const* name,
    void const* data,
    int size)
{
    struct App* app = (struct App*)user;
    char saved[TORIRS_IOITEM_MAX_PATH];

    assert(app);
    assert(plugin);
    assert(name);
    assert(data || size == 0);

    app_plugin_asset_saved_path(app, plugin, name, saved, sizeof(saved));
    if( !saved[0] )
    {
        /* Persistence is switched off for this run. Refusing loudly rather
         * than inventing a path: a client told not to write files must not
         * start writing them because a plugin asked. */
        TORIRS_ERR(
            "plugin: %s cannot save asset '%s'; plugin persistence is off for "
            "this run (TORIRS_PLUGIN_PREFS is empty)\n",
            plugin,
            name);
        return 0;
    }
    ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_PluginAssetWrite(saved, data, size));
    return 1;
}

/* ---------------------------------------------------------- screenshots */

/*
 * A plugin asked for a frame. Record it; App_RunOnce takes it.
 *
 * Nothing is rendered here, and that is the point -- see the queue's own
 * comment in app.h. Refusing loudly when the queue is full rather than
 * silently dropping the request: a plugin that fills it is asking for four
 * pictures of one instant, and the only way it finds that out is being told.
 */
/*
 * Where a capture lands, as one path.
 *
 * An absolute destination is the user's own folder and is used as given. A
 * relative one -- and that includes the common case of no destination at all
 * -- lands under the plugin's saved-asset directory, so "Bob/Levels" sorts a
 * browser run's captures the same way it sorts a desktop one. The browser lane
 * has no path to name; without this it would have no way to organise them
 * either.
 *
 * `out` is emptied when there is nowhere to write: persistence off for this
 * run AND a destination that is not absolute. That is the one case with no
 * answer, and it is an answer.
 */
static void
app_plugin_screenshot_path(
    struct App* app,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out,
    size_t out_size)
{
    assert(app);
    assert(plugin);
    assert(dir);
    assert(name);
    assert(out);
    assert(out_size > 0);

    if( dir[0] == '/' )
    {
        snprintf(out, out_size, "%s/%s", dir, name);
        return;
    }

    char base[TORIRS_IOITEM_MAX_PATH];

    app_plugin_asset_saved_path(app, plugin, "", base, sizeof(base));
    if( base[0] && dir[0] )
    {
        /* asset_saved_path ends in the trailing separator plus the empty name
         * it was handed, so the slash is already there. */
        snprintf(out, out_size, "%s%s/%s", base, dir, name);
    }
    else if( base[0] )
        snprintf(out, out_size, "%s%s", base, name);
    else
        out[0] = '\0';
}

/*
 * Ask for a picture of the NEXT frame, from whatever is drawing it.
 *
 * This is the only capture worth the name. TORIRS_EXIT_BMP renders a fresh
 * frame with App_Render into a malloc'd buffer -- that is the SOFTWARE
 * rasteriser, whatever --d3d9-zbuffer or --gl3 says on the command line --
 * so it can never show what a GPU lane actually put on the screen, and a
 * capture taken that way is silently useless for any question about GPU
 * state. A request made here is fulfilled in App_DrawComplete out of the
 * renderer's own read-back: glReadPixels on the GL lanes,
 * GetRenderTargetData on D3D9, the canvas itself on soft3d.
 *
 * Returns 0 and leaves out_path empty when every slot is taken or writing
 * files is refused.
 */
int
App_RequestScreenshot(
    struct App* app,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size)
{
    return app_plugin_screenshot(app, "client", dir, name, out_path, out_path_size);
}

int
app_plugin_screenshot(
    void* user,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size)
{
    struct App* app = (struct App*)user;

    assert(app);
    assert(plugin);
    assert(name);
    assert(out_path);
    assert(out_path_size > 0);

    out_path[0] = '\0';
    for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        struct AppPluginScreenshot* shot = &app->plugin_screenshots[i];
        size_t len;

        if( shot->in_use )
            continue;

        snprintf(shot->plugin, sizeof(shot->plugin), "%s", plugin);
        snprintf(shot->dir, sizeof(shot->dir), "%s", dir ? dir : "");
        snprintf(shot->name, sizeof(shot->name), "%s", name);
        /* The format is not the plugin's choice -- this writes PNG -- so a
         * name that does not say so is completed rather than trusted. A name
         * that already carries an extension is left alone, because a plugin
         * that wrote "kill-42.png" meant that file and not "kill-42.png.png". */
        len = strlen(shot->name);
        if( !strchr(shot->name, '.') && len + 4 < sizeof(shot->name) )
            snprintf(shot->name + len, sizeof(shot->name) - len, ".png");

        /* Resolved now rather than at the write, so the caller can be told
         * where its picture is going while it is still in a position to say
         * so. Refused here for the same reason app_plugin_asset_write refuses:
         * a client told not to write files must not start writing them because
         * a plugin asked -- and a refusal before the frame is spent is better
         * than one after it. */
        app_plugin_screenshot_path(
            app, shot->plugin, shot->dir, shot->name, shot->path, sizeof(shot->path));
        if( !shot->path[0] )
        {
            TORIRS_ERR(
                "plugin: %s cannot save screenshot '%s'; plugin persistence is off for "
                "this run (TORIRS_PLUGIN_PREFS is empty), so there is no folder to put it "
                "under and the destination is not an absolute path\n",
                shot->plugin,
                shot->name);
            return 0;
        }

        shot->in_use = 1;
        snprintf(out_path, (size_t)out_path_size, "%s", shot->path);
        return 1;
    }

    TORIRS_LOG(
        "plugin: %s asked for more than %d screenshots in one frame; '%s' was dropped\n",
        plugin,
        APP_PLUGIN_SCREENSHOTS_MAX,
        name);
    return 0;
}

/*
 * Where a capture's pixels come from when the lane could not supply any.
 *
 * A software re-render of the frame the emit buffer is still holding. It is
 * the same scene through the client's own rasteriser, which is NOT the same
 * thing as the frame that was presented: a GPU lane may have drawn it with
 * different textures, filtering and draw distance, and none of that is in
 * here. It exists so a lane with no readback (D3D9) and a run with no
 * renderer at all (headless) still produce a picture rather than nothing.
 *
 * Every lane that can read its own frame back should, and does.
 */
static int
app_capture_fallback_render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    int saved_pick;

    assert(app);
    assert(pixels);

    /*
     * Disarm the world pick for the duration.
     *
     * App_Render arms it from the live mouse position and hands the hits to
     * App_PickFinish, which is how the click paths learn what is under the
     * pointer. This render is not the one they are reading, and letting it
     * publish a second pickset for the same frame would make a screenshot a
     * thing that can affect what a click does.
     */
    saved_pick = app->world_mouse_in_viewport;
    app->world_mouse_in_viewport = 0;
    App_Render(app, pixels, width, height);
    app->world_mouse_in_viewport = saved_pick;
    return 1;
}

/*
 * Encode one frame and hand every waiting capture a copy.
 *
 * One encode for all of them: the pending queue holds requests made during the
 * same frame, so they are requests for the same picture under different names.
 */
static void
app_plugin_screenshots_write(
    struct App* app,
    int const* pixels,
    int width,
    int height)
{
    unsigned char* rgb;
    void* png;
    size_t png_size = 0;

    assert(app);
    assert(pixels);

    /* Three channels, not four: the client's canvas has no alpha to carry
     * (the high byte is padding), and a PNG that claimed one would be half
     * again as large for nothing. */
    rgb = malloc((size_t)width * (size_t)height * 3);
    assert(rgb);
    for( int i = 0; i < width * height; i++ )
    {
        rgb[i * 3 + 0] = (unsigned char)((pixels[i] >> 16) & 0xFF);
        rgb[i * 3 + 1] = (unsigned char)((pixels[i] >> 8) & 0xFF);
        rgb[i * 3 + 2] = (unsigned char)(pixels[i] & 0xFF);
    }

    png = tdefl_write_image_to_png_file_in_memory_ex(rgb, width, height, 3, &png_size, 6, MZ_FALSE);
    free(rgb);
    assert(png);

    for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        struct AppPluginScreenshot* shot = &app->plugin_screenshots[i];

        if( !shot->in_use )
            continue;
        shot->in_use = 0;

        /* The destination was resolved -- and refused, when there was none --
         * back when the request was made, so a queued capture always has a
         * path to go to. */
        assert(shot->path[0]);
        ToriRS_TaskQueue_Add(
            app->runner.queue, CreateTask_PluginAssetWrite(shot->path, png, (int)png_size));
    }

    mz_free(png);
}

void
App_DrawComplete(
    struct App* app,
    App_FrameSupplier supplier,
    void* supplier_user)
{
    int const width = UITREE_LAYOUT_ROOT_W;
    int const height = UITREE_LAYOUT_ROOT_H;
    int pending = 0;
    int* pixels;

    assert(app);

    /*
     * Nobody waiting, nothing to do -- and this test comes FIRST, before the
     * supplier is so much as called. That ordering is the whole design: it is
     * what lets a lane hand over a glReadPixels here and pay for it only on
     * the frames a capture was asked for.
     */
    for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
        pending += app->plugin_screenshots[i].in_use;
    if( pending == 0 )
        return;

    /* A capture of the loading bar is not a capture of anything. Requests
     * survive the wait; a plugin cannot ask for one before it has started
     * anyway, so this only covers a boot that re-enters. */
    if( App_IsBooting(app, NULL) )
        return;

    pixels = malloc((size_t)width * (size_t)height * sizeof(int));
    assert(pixels);

    if( !supplier || !supplier(supplier_user, pixels, width, height) )
        app_capture_fallback_render(app, pixels, width, height);

    app_plugin_screenshots_write(app, pixels, width, height);
    free(pixels);
}
