/*
 * The developer overlay, its probes (position, height profile, tile flags,
 * bridges), the settings pickers glue, and the XP-drop debug.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/*
 * XP-drop panel probe (TORIRS_XPDROP_DEBUG=1).
 *
 * The panel is entirely client-driven and its failure modes all present the
 * same way — no drops — so the probe prints the three states that separate them
 * and nothing else. The interface is the profile's `[iface:xpdrop]`; the child
 * numbers are that interface's own layout (child 2 statlistener holds the
 * stat-transmit hook, 17 drops_container, 18..24 the seven rows) and travel
 * with it, stable across rev 230 and 239.
 */
#define APP_XPDROP_COM(child) app_iface_com(app, "xpdrop", (child))
#define APP_XPDROP_ROW_COUNT 7

/* Private to this unit, declared up front so definition order is free. */
static void
app_settings_picker_commit(
    void* userdata,
    int varp_id,
    int value);

/* ---- Developer overlay ------------------------------------------------- *
 *
 * One minimenu-styled ToriRSChrome panel (src/ui/README_DEBUG_OVERLAY.md) holding
 * the frame time, averaged over the last APP_DEBUG_FRAME_SAMPLES frames. The
 * App feeds the model; the node that draws it is declared by the manifest
 * (`type=debug_overlay`, docs/debug_overlay.md §2) and answered through
 * UITREE_HOST_GET_DEBUG_OVERLAY.
 *
 * A hidden panel builds no primitives, so the overlay costs one host call and
 * nothing else until the toggle key turns it on. The samples keep accumulating
 * either way — the average is a property of the client, not of whether anyone
 * is looking at it, and a readout that starts at "--" for ten frames after
 * every toggle would be useless for exactly the stutter it is there to catch.
 */

/* Sizes the panel. The title is the widest string the panel will ever hold, so
 * content sizing (fixed_w 0) settles on one width and the panel never resizes
 * as the digits change under it. */
static char const k_app_debug_overlay_title[] = "Frame time (10-frame avg)";

void
app_debug_overlay_init(struct App* app)
{
    assert(app);

    ToriRSChrome_Init(&app->dbg_ui);

    /*
     * Tell the chrome which baked skin images this build actually carries.
     *
     * The chrome cannot ask: it reaches nothing outside the C library, which is
     * the property that lets it draw on a cache that failed to open. So the
     * host, which does link the baked module, reports what it has -- and a
     * build with the skin stubbed out reports nothing and gets the flat look,
     * with no code path here that has to know about that case.
     *
     * TORIRS_CHROME_THEME=flat forces the flat developer palette, for reading a
     * dense readout without the parchment behind it.
     */
    {
        char const* theme = getenv("TORIRS_CHROME_THEME");
        if( theme && strcmp(theme, "flat") == 0 )
            app->dbg_ui.theme = torirs_chrome_theme_default;

        /*
         * Which boolean art the checkboxes wear: the manifest's answer, and
         * TORIRS_CHROME_CHECKBOX over the top of it -- the same order every
         * other chrome option here is resolved in.
         *
         * Set on dbg_ui alone because plugin_ui is COPIED from it a few lines
         * below; going through App_SetChromeCheckStyle would be a call on an
         * instance that does not exist yet.
         */
        {
            char const* pick = getenv("TORIRS_CHROME_CHECKBOX");
            int style = app->cfg.chrome_checkbox;
            if( pick && strcmp(pick, "box") == 0 )
                style = TORIRS_CHROME_CHECK_STYLE_BOX;
            else if( pick && strcmp(pick, "tick") == 0 )
                style = TORIRS_CHROME_CHECK_STYLE_TICK;
            else if( pick )
                TORIRS_LOG("chrome: TORIRS_CHROME_CHECKBOX must be tick|box, got '%s'\n", pick);
            ToriRSChrome_SetCheckStyle(&app->dbg_ui, style);
        }

        for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT && i < ToriRSChromeSkin_Count(); i++ )
            app->dbg_ui.skin_avail |= 1u << i;
        if( app->dbg_ui.skin_avail & (1u << TORIRS_CHROME_SKIN_PANEL_BODY) )
        {
            struct ToriRSChromeSkin_Sprite const* body =
                ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY);
            app->dbg_ui.skin_tile_w = body->w;
            app->dbg_ui.skin_tile_h = body->h;
        }
    }

    /*
     * The plugin window's own instance.
     *
     * Copied wholesale from the developer one rather than initialised
     * separately, so the two cannot drift on theme, scale or which skin slots
     * the build carries -- three things a second Init would have to repeat and
     * a fourth panel would eventually be found not to have.
     */
    app->plugin_ui = app->dbg_ui;
    ToriRSChromeShell_Init(&app->plugin_shell, 320);
    /* File-static view state survives Android recreating main() in one
     * process; the App does not, so a new App must not inherit a selected page
     * whose model belonged to the previous run. */
    g_plugin_page = -1;
    g_plugin_page_built = -1;
    g_plugin_fullscreen = 0;
    g_plugin_fullscreen_built = -1;
    app->plugin_panel = -1;
    app->plugin_panel_built_for = -1;
    app->plugin_panel_built_rev = -1;
    app->plugin_panel_built_model_rev = 0;
    app->plugin_panel_built_generation = 0;
    app->plugin_panel_built_registry_rev = 0;
    app->plugin_panel_intent_sequence = 0;
    ToriRSChromeRailSync_Init(&app->plugin_rail);
    app->plugin_rail_has_layout = 0;
    /* No executor has been reported yet, and BUFFER is a real answer. */
    app->plugin_exec_logged_kind = -1;

    app->dbg_visible = 0;
    FrameTimeRing_Reset(&app->dbg_frame_times);
    app->dbg_panel = ToriRSChrome_PanelAdd(
        &app->dbg_ui, TORIRS_CHROME_PANEL_MENU, 8, 8, 0, k_app_debug_overlay_title);
    app->dbg_frame_row = ToriRSChrome_MenuItem(&app->dbg_ui, app->dbg_panel, "--");
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->dbg_panel, 0);

    app->locedit.panel =
        ToriRSChrome_PanelAdd(&app->dbg_ui, TORIRS_CHROME_PANEL_MENU, 8, 40, 0, "Loc Editor");
    app->locedit.row_target =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "nothing selected");
    app->locedit.row_pos = ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "");
    app->locedit.row_size = ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "");
    app->locedit.row_extra = ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "");
    ToriRSChrome_Separator(&app->dbg_ui, app->locedit.panel);
    /* Rows double as the key reference: chat input is forced off while this
     * panel is open (below), so these letters are always free to use without
     * a message box eating them. Still clickable too -- the key is the fast
     * path, the click is the discoverable one. */
    app->locedit.item_xplus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Move X+1  [D]");
    app->locedit.item_xminus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Move X-1  [A]");
    app->locedit.item_zplus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Move Z+1  [W]");
    app->locedit.item_zminus =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Move Z-1  [S]");
    app->locedit.item_rotate =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Rotate  [R]");
    app->locedit.item_reselect =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Reselect (under cursor)  [Space]");
    app->locedit.item_deselect =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Deselect  [Backspace]");
    app->locedit.item_close =
        ToriRSChrome_MenuItem(&app->dbg_ui, app->locedit.panel, "Close  [9 / Esc]");
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit.panel, 0);
    app->locedit.visible = 0;

    /* The two All Settings pickers, in this same instance for the same reason
     * the loc editor's panel is: they are in-canvas, short-lived, and share
     * the frame-time panel's Build/Prims/emit plumbing for free. */
    UISettingsPickers_Init(&app->settings_pickers, &app->dbg_ui);
    LocEditorSelection_Reset(&app->locedit_selection);

    /* Footprint outline: the env var picks the mode AND the starting state, so
     * an existing `TORIRS_HOVER_FOOTPRINT=1` run is unchanged and the hotkey
     * merely gains the ability to turn it off. Unset means off but armed at
     * mode 1, which is what the hotkey turns on. A negative or unparsable value
     * is off with nothing to restore. */
    {
        char const* env = getenv("TORIRS_HOVER_FOOTPRINT");
        int mode = (env && env[0]) ? (int)strtol(env, NULL, 0) : 0;
        if( mode < 0 )
            mode = 0;
        app->hover_footprint = mode;
        app->hover_footprint_mode = mode > 0 ? mode : 1;
    }
}

/*
 * Toggle the overlay, refresh its readout, rebuild its display list.
 *
 * Runs before the BOOTING early-out in App_RunOnce so the key still latches
 * during a boot, and before the emit rebuild so a changed readout reaches the
 * same frame's display list rather than the next one's.
 */
void
app_debug_overlay_tick(
    struct App* app,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    /* Suppressed while a text line has focus, like the camera keys: typing a
     * message must not flip debug chrome. */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_DEBUG_OVERLAY) )
    {
        app->dbg_visible = !app->dbg_visible;
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->dbg_panel, app->dbg_visible);
    }

    if( app->dbg_visible )
    {
        uint32_t const mean_us = FrameTimeRing_MeanUs(&app->dbg_frame_times);
        char text[TORIRS_CHROME_INPUT_MAX];

        /* Two decimals: the samples are microseconds, and rounding a 3.4 ms
         * frame to "3 ms" throws away the part that moves. `mean_us` is 0
         * both for "no samples yet" and for a run of sub-10us frames, and the
         * readout says "--" for either: there is nothing useful to print. */
        if( mean_us > 0 )
            snprintf(
                text,
                sizeof(text),
                "%u.%02u ms",
                (unsigned)(mean_us / 1000u),
                (unsigned)((mean_us % 1000u) / 10u));
        else
            snprintf(text, sizeof(text), "--");
        /* Compare-then-set: an unchanged readout dirties nothing, so a steady
         * client rebuilds no display list and requests no redraw. */
        ToriRSChrome_SetText(&app->dbg_ui, app->dbg_frame_row, text);
    }

    /* The canvas the readout is drawn onto, so an open dropdown list folds up
     * into it rather than off the bottom edge. Compare-then-set inside, and
     * the canvas can change under a resize, so it is restated per tick rather
     * than captured at init — where the layout root is not resolved yet. */
    ToriRSChrome_SetSurface(&app->dbg_ui, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    /* Build returns 0 on a frame where nothing moved. When it did rebuild the
     * canvas is stale — including the frame the panel was hidden on, whose
     * vacated pixels are still on screen until something repaints them. */
    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}

/*
 * What a picker's chosen value means: the varp the row stores it in.
 *
 * The pickers know a varp id and an integer and nothing about either, which is
 * what lets them be tested without a CS2 host. This is the half that needs
 * one.
 */
static void
app_settings_picker_commit(
    void* userdata,
    int varp_id,
    int value)
{
    struct App* app = (struct App*)userdata;

    assert(app);
    RS_CS2Host_ScriptWriteVarp(&app->host, varp_id, value);
}

/* Open the colour picker when a row asked for one, then drive it. The Take is
 * here because the request comes off the CS2 host; everything it opens is
 * UISettingsPickers'. */
void
app_settings_colour_tick(struct App* app)
{
    struct RS_CS2SettingsColourRequest request;

    assert(app);
    if( RS_CS2Host_TakeSettingsColourRequest(&app->host, &request) )
    {
        UISettingsPickers_OpenColour(
            &app->settings_pickers, &app->dbg_ui, app->tree, &request);
        app->need_redraw = 1;
    }
    if( UISettingsPickers_ColourTick(
            &app->settings_pickers,
            &app->dbg_ui,
            app->tree,
            app_settings_picker_commit,
            app) )
        app->need_redraw = 1;
}

void
app_settings_number_tick(struct App* app)
{
    struct RS_CS2SettingsNumberRequest request;

    assert(app);
    if( RS_CS2Host_TakeSettingsNumberRequest(&app->host, &request) )
    {
        UISettingsPickers_OpenNumber(
            &app->settings_pickers, &app->dbg_ui, app->tree, &request);
        app->need_redraw = 1;
    }
    if( UISettingsPickers_NumberTick(
            &app->settings_pickers,
            &app->dbg_ui,
            app->tree,
            app_settings_picker_commit,
            app) )
        app->need_redraw = 1;
}

/* Cache the WORLD node's emit desc: the mouse gate rect and the viewport the
 * frame emitter draws with (pick/render parity comes from sharing it). Also the
 * "is a world on screen this frame" flag every world subsystem gates on. Uses
 * the previous frame's emit buffer — the world box only changes on relayout. */
/* TORIRS_POS_DEBUG=1: the local player's authoritative tile in every frame of
 * reference at once — scene tile, absolute world tile (compare against the
 * server's ::getcoord), fine draw position, and the camera the painter used.
 * The one-liner for "is the player where the server thinks it is". */
void
app_debug_log_position(struct App* app)
{
    struct WorldEntity_Player* local;

    if( !torirs_env_pos_debug() || !app->world )
        return;
    local = app_local_player(app);
    if( !local )
        return;
    fprintf(
        stderr,
        "pos: scene=%d,%d route0=%d,%d abs=%d,%d level=%d draw=%u,%u y=%d flags=%02x/%02x "
        "cam=%d,%d,%d yaw=%d pitch=%d\n",
        local->grid_position.x,
        local->grid_position.z,
        local->pathing.route_x[0],
        local->pathing.route_z[0],
        app->world->_base_tile_x + local->pathing.route_x[0],
        app->world->_base_tile_z + local->pathing.route_z[0],
        local->grid_position.level,
        local->draw_position.x,
        local->draw_position.z,
        /* Ground y under the player + the land settings of its column at the
         * player's level / level 1 — the bridge bump (LinkBelow 0x02 at
         * level 1) is only visible here. */
        app_world_height(
            app,
            (int)local->draw_position.x,
            (int)local->draw_position.z,
            local->grid_position.level),
        (unsigned)World_TileFlagGet(
            app->world, local->grid_position.x, local->grid_position.z, local->grid_position.level),
        (unsigned)World_TileFlagGet(app->world, local->grid_position.x, local->grid_position.z, 1),
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z,
        app->world_camera.yaw,
        app->world_camera.pitch);
}

/* TORIRS_HPROF=x0,x1,z0,z1: ground height across a rectangle of scene tiles,
 * once per load.
 *
 * Flat terrain and correctly-varying terrain are indistinguishable from a
 * screenshot, and "the heights must be wrong" is the first thing anyone reaches
 * for when scenery and ground intersect oddly. This settles it in one run: the
 * Inferno arena reads a single height across 41x31 tiles and is *supposed* to —
 * its depth is scenery, not relief — while Lumbridge over the same span ramps
 * -464 to -240. Without the second half of that comparison the first half reads
 * as a bug. */
void
app_debug_height_profile(struct App* app)
{
    static unsigned logged = (unsigned)-1;
    const char* env = torirs_env_hprof();
    int x0, x1, z0, z1;
    if( !env || !app->world || !app->world->load_complete )
        return;
    if( app->world->load_seq == logged )
        return;
    logged = app->world->load_seq;
    int lvl = 0;
    if( sscanf(env, "%d,%d,%d,%d,%d", &x0, &x1, &z0, &z1, &lvl) < 4 )
        return;
    for( int z = z1; z >= z0; z-- )
    {
        TORIRS_LOG("hprof L%d z=%3d:", lvl, z);
        for( int x = x0; x <= x1; x++ )
            TORIRS_LOG(" %5d", app_world_height(app, x * 128 + 64, z * 128 + 64, lvl));
        TORIRS_LOG("\n");
    }
}

/* TORIRS_TFLAGS=x0,x1,z0,z1: per-tile terrain settings at every cache level,
 * once per load. BLOCK 0x1, LINK_BELOW 0x2, REMOVE_ROOF 0x4, VIS_BELOW 0x8,
 * FORCE_HIGH_DETAIL 0x10. VIS_BELOW is the one that drags a tile from an upper
 * level down onto level 0's draw pass. */
void
app_debug_tile_flags(struct App* app)
{
    static unsigned logged = (unsigned)-1;
    const char* env = torirs_env_tflags();
    int x0, x1, z0, z1;
    if( !env || !app->world || !app->world->load_complete )
        return;
    if( app->world->load_seq == logged )
        return;
    logged = app->world->load_seq;
    if( sscanf(env, "%d,%d,%d,%d", &x0, &x1, &z0, &z1) != 4 )
        return;
    for( int lv = 0; lv < WORLD_MAP_TERRAIN_LEVELS; lv++ )
    {
        int n_vis = 0, n_link = 0;
        for( int z = z0; z <= z1; z++ )
            for( int x = x0; x <= x1; x++ )
            {
                unsigned f = (unsigned)World_TileFlagGet(app->world, x, z, lv);
                if( f & RSCACHE_FLOFLAG_VIS_BELOW )
                {
                    n_vis++;
                    if( n_vis <= 400 )
                        TORIRS_LOG(
                            "tflags L%d tile=%d,%d VIS_BELOW (0x%02x) terrain_element=%d\n",
                            lv,
                            x,
                            z,
                            f,
                            World_TerrainElementAt(app->world, x, z, lv));
                }
                if( f & RSCACHE_FLOFLAG_LINK_BELOW )
                    n_link++;
            }
        TORIRS_LOG("tflags L%d: vis_below=%d link_below=%d\n", lv, n_vis, n_link);
    }
}

/* TORIRS_TPROJ=x0,x1,z0,z1: project each tile centre to screen, once per load.
 * Turns "which tile is that artifact" from a guess into a lookup. */
void
app_debug_tile_project(struct App* app)
{
    static int ticks = 0;
    const char* env = torirs_env_tproj();
    int x0, x1, z0, z1;
    if( !env || !app->world || !app->world->load_complete || !app->world_view_valid )
        return;
    /* After the camera has settled, not on the load callback: the projection
     * reads app->world_camera, which the load has not written yet. */
    if( ++ticks != 120 )
        return;
    if( sscanf(env, "%d,%d,%d,%d", &x0, &x1, &z0, &z1) != 4 )
        return;
    for( int z = z0; z <= z1; z++ )
        for( int x = x0; x <= x1; x++ )
        {
            int sx = 0, sy = 0;
            if( app_world_project(app, x * 128 + 64, z * 128 + 64, 0, &sx, &sy) )
                TORIRS_LOG("tproj tile=%d,%d screen=%d,%d\n", x, z, sx, sy);
        }
}

/* TORIRS_BRIDGE_DEBUG=1: list every LinkBelow column in the loaded scene with
 * the level-0/level-1 ground heights the getAvH bump chooses between. The
 * "am I standing on the deck or under it" one-liner. Prints once per load. */
void
app_debug_log_bridges(struct App* app)
{
    static unsigned logged_seq = 0;
    int count = 0;

    if( !torirs_env_bridge_debug() || !app->world || !app->world->load_complete )
        return;
    if( app->world->load_seq == logged_seq )
        return;
    logged_seq = app->world->load_seq;

    for( int x = 0; x < app->world->_scene_size; x++ )
    {
        for( int z = 0; z < app->world->_scene_size; z++ )
        {
            if( (World_TileFlagGet(app->world, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) == 0 )
                continue;
            count++;
            if( count > 40 )
                continue;
            TORIRS_LOG(
                "bridge: scene=%d,%d abs=%d,%d y0=%d y1=%d\n",
                x,
                z,
                app->world->_base_tile_x + x,
                app->world->_base_tile_z + z,
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 0),
                heightmap_get_interpolated(app->world->heightmap, x * 128 + 64, z * 128 + 64, 1));
        }
    }
    TORIRS_LOG("bridge: %d link-below columns in scene\n", count);
}

int
app_xpdrop_debug(void)
{
    static int on = -1;
    if( on < 0 )
        on = getenv("TORIRS_XPDROP_DEBUG") ? 1 : 0;
    return on;
}

void
app_xpdrop_debug_tick(struct App* app)
{
    static char last[768];
    static int last_print_cycle = -100000;
    char line[768];
    int n = 0;
    int32_t listener_idx = UITree_FindByComponentId(app->tree, APP_XPDROP_COM(2));
    struct RS_CS2StatTransmitHook const* hook = NULL;

    if( listener_idx < 0 )
        return; /* panel not mounted — nothing to say */

    for( int i = 0; i < app->host.stat_transmit_hook_count; i++ )
    {
        if( app->host.stat_transmit_hooks[i].component_id == APP_XPDROP_COM(2) )
        {
            hook = &app->host.stat_transmit_hooks[i];
            break;
        }
    }

    /* 122:0 (universe) carries the auto-hide timer script998 armed by script997.
     * Its last argument is the clientclock deadline at which it hides the whole
     * panel; once past it, script998 is supposed to disarm itself. */
    {
        int32_t const uni = UITree_FindByComponentId(app->tree, APP_XPDROP_COM(0));
        struct UITreeRuntimeScriptHook const* t =
            uni >= 0 ? &UITree_Hooks(&app->tree->components[uni])->on_timer : NULL;
        n += snprintf(
            line + n,
            sizeof(line) - (size_t)n,
            "uni{timer=%d deadline=%d} ",
            t ? t->script_id : -1,
            (t && t->argc > 0) ? UITree_HookArg(t, t->argc - 1) : -1);
    }

    n += snprintf(
        line + n,
        sizeof(line) - (size_t)n,
        "vc70=%d vc71=%d vc76=%d serial=%u hook{%s seen=%u args=%d hid=%d} "
        "c17hid=%d timers=%d",
        VarCManager_GetInt(&app->varcs, 70),
        VarCManager_GetInt(&app->varcs, 71),
        VarCManager_GetInt(&app->varcs, 76),
        app->host.stat_change_serial,
        hook ? "armed" : "MISSING",
        hook ? hook->last_seen_serial : 0u,
        hook ? hook->int_arg_count : -1,
        UITree_ComponentOrAncestorHidden(app->tree, APP_XPDROP_COM(2)) ? 1 : 0,
        UITree_ComponentOrAncestorHidden(app->tree, APP_XPDROP_COM(17)) ? 1 : 0,
        app->tree->timer_hooks.count);

    for( int r = 0; r < APP_XPDROP_ROW_COUNT; r++ )
    {
        int const com = APP_XPDROP_COM(18 + r);
        int32_t const idx = UITree_FindByComponentId(app->tree, com);
        int kids = 0;
        int timer = 0;
        int hid = 1;
        if( idx >= 0 )
        {
            for( int32_t c = app->tree->components[idx].first_child; c >= 0;
                 c = app->tree->components[c].next_sibling )
                kids++;
            timer = UITree_Hooks(&app->tree->components[idx])->on_timer.script_id;
            hid = UITree_ComponentOrAncestorHidden(app->tree, com) ? 1 : 0;
        }
        n += snprintf(
            line + n,
            sizeof(line) - (size_t)n,
            " r%d=%s/%d/%d/%d",
            r,
            idx < 0 ? "gone" : (hid ? "hid" : "vis"),
            kids,
            timer,
            idx >= 0 ? app->tree->components[idx].behavior.hide : -1);
        if( n >= (int)sizeof(line) )
            break;
    }

    /* Only on change, plus a heartbeat, so a session can be left running. The
     * clock is printed but deliberately not part of the compared state — it
     * changes every tick and would make every line "new". */
    if( strcmp(line, last) == 0 && (int)app->logic_cycle - last_print_cycle < 1500 )
        return;
    snprintf(last, sizeof(last), "%s", line);
    last_print_cycle = (int)app->logic_cycle;
    TORIRS_LOG("xpdrop: t=%d clock=%d %s\n", (int)app->logic_cycle, app->host.client_clock, line);
}
