/*
 * The plugin settings panel.
 *
 * Generated from each plugin's declared config schema rather than hand-built,
 * which is the point of declaring a schema at all: a plugin author writes the
 * key, type, label and default once and gets a settings row for free, in C and
 * in Lua alike. Nothing here knows what any particular plugin does.
 *
 * It is a panel inside the developer chrome (app->dbg_ui) rather than a fourth
 * overlay of its own: ToriRSChrome is already registered, already drawn above
 * everything, and already handles focus, damage and scaling. A second chrome
 * would be a second of all of that for one window.
 *
 * Included into app.c for the same reason as the bridge -- it works on App
 * state and the chrome the App owns.
 */

/* Value shown for a config key: the store's own string, so what the panel
 * displays is exactly what would be written to the ini. */
static char const*
app_plugin_panel_value(struct App* app, int plugin, char const* key)
{
    char const* v = PluginHost_ConfigGet(app->plugins, plugin, key);
    return v ? v : "";
}

static void
app_plugin_panel_track(struct App* app, int widget, int plugin, int cfg_index)
{
    if( widget < 0 )
        return;
    if( app->plugin_panel_row_count >= APP_PLUGIN_PANEL_ROWS_MAX )
        return;

    struct AppPluginPanelRow* row = &app->plugin_panel_rows[app->plugin_panel_row_count++];
    row->widget = widget;
    row->plugin = plugin;
    row->cfg_index = cfg_index;
}

/*
 * Add rows for plugins that do not have any yet.
 *
 * Append rather than rebuild, because ToriRSChrome has no remove-widget call:
 * the only way to "rebuild" is Reset, which would take the developer overlay
 * and both editor panels down with it. Appending works because the plugin list
 * only ever grows -- scripts register asynchronously as their boot task reads
 * them, and nothing is ever unregistered (disabling is a flag, not a removal).
 */
static void
app_plugin_panel_sync(struct App* app)
{
    int count;

    assert(app);
    if( !app->plugins )
        return;

    count = PluginHost_Count(app->plugins);
    if( app->plugin_panel_built_for == count )
        return;

    if( app->plugin_panel < 0 )
    {
        app->plugin_panel = ToriRSChrome_PanelAdd(
            &app->dbg_ui, TORIDBG_PANEL_WINDOW, 8, 72, 0, "Plugins");
        if( app->plugin_panel < 0 )
            return;
        ToriRSChrome_PanelSetResizable(&app->dbg_ui, app->plugin_panel, 1);
        ToriRSChrome_PanelSetVisible(
            &app->dbg_ui, app->plugin_panel, app->plugin_panel_visible);
    }

    for( int p = app->plugin_panel_built_for; p < count; p++ )
    {
        int const cfg_count = PluginHost_ConfigCount(app->plugins, p);
        char label[TORIDBG_INPUT_MAX];
        char const* err = PluginHost_Error(app->plugins, p);

        if( p > 0 )
            ToriRSChrome_Separator(&app->dbg_ui, app->plugin_panel);

        snprintf(label, sizeof(label), "%s", PluginHost_Name(app->plugins, p));
        app_plugin_panel_track(
            app,
            ToriRSChrome_Checkbox(
                &app->dbg_ui,
                app->plugin_panel,
                label,
                PluginHost_IsEnabled(app->plugins, p) ? 1 : 0),
            p,
            -1);

        /* A script that faulted says so where its switch is, rather than only
         * in a log nobody has open. */
        if( err )
        {
            snprintf(label, sizeof(label), "  ! %s", err);
            ToriRSChrome_LabelColored(&app->dbg_ui, app->plugin_panel, label, 0xFFCC5555u);
        }

        for( int c = 0; c < cfg_count; c++ )
        {
            struct ToriRS_PluginConfigItem const* item =
                PluginHost_ConfigItem(app->plugins, p, c);
            int widget = -1;

            /* A key with no label is state the plugin persists for itself --
             * the highlighter's tag list -- not something to hand-edit. */
            if( !item || !item->label )
                continue;

            if( item->type == TORIRS_PLUGIN_CFG_BOOL )
            {
                widget = ToriRSChrome_Checkbox(
                    &app->dbg_ui,
                    app->plugin_panel,
                    item->label,
                    atoi(app_plugin_panel_value(app, p, item->key)) != 0);
            }
            else
            {
                /* Ints, colours, strings and enums all edit as text, because
                 * the store is textual to begin with -- the field shows the
                 * value that would be written, not a rendering of it. An
                 * enum's choices ride in the label so the field is not a
                 * guessing game. */
                if( item->type == TORIRS_PLUGIN_CFG_ENUM && item->choices )
                    snprintf(label, sizeof(label), "%s (%s)", item->label, item->choices);
                else
                    snprintf(label, sizeof(label), "%s", item->label);
                widget = ToriRSChrome_TextInput(
                    &app->dbg_ui,
                    app->plugin_panel,
                    label,
                    app_plugin_panel_value(app, p, item->key));
            }
            app_plugin_panel_track(app, widget, p, c);
        }
    }
    app->plugin_panel_built_for = count;
}

/* Apply one activated widget back to the host. */
static void
app_plugin_panel_apply(struct App* app, int widget)
{
    for( int i = 0; i < app->plugin_panel_row_count; i++ )
    {
        struct AppPluginPanelRow const* row = &app->plugin_panel_rows[i];
        if( row->widget != widget )
            continue;

        if( row->cfg_index < 0 )
        {
            int const on = ToriRSChrome_Checked(&app->dbg_ui, widget);
            PluginHost_SetEnabled(app->plugins, row->plugin, on != 0);
            /* Re-enabling clears the fault note: whatever it said was about
             * the run that just ended. */
            if( on )
                PluginHost_SetError(app->plugins, row->plugin, NULL);
            return;
        }

        struct ToriRS_PluginConfigItem const* item =
            PluginHost_ConfigItem(app->plugins, row->plugin, row->cfg_index);
        if( !item )
            return;

        if( item->type == TORIRS_PLUGIN_CFG_BOOL )
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", ToriRSChrome_Checked(&app->dbg_ui, widget) ? 1 : 0);
            PluginHost_ConfigSet(app->plugins, row->plugin, item->key, buf);
        }
        else
        {
            char const* text = ToriRSChrome_Text(&app->dbg_ui, widget);
            /* Clamp an int here rather than letting a typo through: the store
             * is textual, so nothing downstream would catch "999" for a key
             * declared 0..255, and the plugin would just read a wrong number. */
            if( item->type == TORIRS_PLUGIN_CFG_INT && item->max > item->min )
            {
                int v = atoi(text);
                char buf[16];
                if( v < item->min )
                    v = item->min;
                if( v > item->max )
                    v = item->max;
                snprintf(buf, sizeof(buf), "%d", v);
                PluginHost_ConfigSet(app->plugins, row->plugin, item->key, buf);
                ToriRSChrome_SetText(&app->dbg_ui, widget, buf);
            }
            else
                PluginHost_ConfigSet(app->plugins, row->plugin, item->key, text);
        }
        return;
    }
}

/*
 * Per-frame: the toggle, the lazy build, and one activation.
 *
 * Runs beside the other chrome ticks at the top of App_RunOnce, before the
 * BOOTING early-out, so the panel is usable while a cache is still loading --
 * which is exactly when someone wants to switch a misbehaving plugin off.
 */
static void
app_plugin_panel_tick(struct App* app, struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    if( !app->plugins )
        return;

    /* Same suppression as every other chrome toggle: a focused chat line must
     * not flip developer windows. */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PLUGIN_PANEL) )
    {
        app->plugin_panel_visible = !app->plugin_panel_visible;
        if( app->plugin_panel >= 0 )
            ToriRSChrome_PanelSetVisible(
                &app->dbg_ui, app->plugin_panel, app->plugin_panel_visible);
    }

    if( !app->plugin_panel_visible )
        return;

    /* Scripts register asynchronously, so the list can still be growing the
     * first few frames the panel is open. */
    app_plugin_panel_sync(app);

    /*
     * Peek the activation latch before taking it.
     *
     * The latch is shared by every panel in this chrome, and taking one that
     * belongs to someone else swallows their click -- the exact bug the loc
     * editor's drain carries a comment about, where the map editor's dropdown
     * showed a new value while its tool never changed. Checking ownership
     * first means two open panels cannot eat each other's input.
     */
    {
        int const pending = app->dbg_ui.activated;
        if( pending >= 0 )
        {
            for( int i = 0; i < app->plugin_panel_row_count; i++ )
            {
                if( app->plugin_panel_rows[i].widget != pending )
                    continue;
                app_plugin_panel_apply(app, ToriRSChrome_TakeActivated(&app->dbg_ui));
                break;
            }
        }
    }

    /*
     * Build our own display list, exactly as the developer overlay and the loc
     * editor do at the end of their ticks.
     *
     * Not optional and not someone else's job: Build only rebuilds when
     * something changed, and it is the caller that saw it change who must turn
     * that into need_redraw. Leaving it to the next tick's Build means the
     * frame this panel first appears on is never marked dirty, the shell
     * re-presents the previous framebuffer, and the panel is invisible until
     * something unrelated happens to request a repaint.
     */
    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}
