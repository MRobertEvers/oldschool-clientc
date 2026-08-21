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
 * Build (or rebuild) the rows.
 *
 * Rebuilt wholesale rather than patched because the plugin list only settles
 * once the script boot task has run: a panel built at App_Init would show the
 * C plugins and the adapter, and never the scripts. ToriRSChrome has no
 * remove-widget call, so a rebuild is a Reset of the whole chrome -- which is
 * why the other panels are re-added here too, and why this runs only when the
 * plugin count actually changes.
 */
static void
app_plugin_panel_build(struct App* app)
{
    int count;

    assert(app);
    if( !app->plugins )
        return;

    count = PluginHost_Count(app->plugins);
    app->plugin_panel_row_count = 0;
    app->plugin_panel = ToriRSChrome_PanelAdd(
        &app->dbg_ui, TORIDBG_PANEL_WINDOW, 8, 72, 0, "Plugins");
    if( app->plugin_panel < 0 )
        return;

    for( int p = 0; p < count; p++ )
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

            switch( item->type )
            {
            case TORIRS_PLUGIN_CFG_BOOL:
                widget = ToriRSChrome_Checkbox(
                    &app->dbg_ui,
                    app->plugin_panel,
                    item->label,
                    atoi(app_plugin_panel_value(app, p, item->key)) != 0);
                break;
            default:
                /* Ints, colours, strings and enums all edit as text. The store
                 * is textual to begin with, so this is the value itself rather
                 * than a rendering of it -- and an enum's choices show up in
                 * the label so the field is not a guess. */
                if( item->type == TORIRS_PLUGIN_CFG_ENUM && item->choices )
                {
                    snprintf(label, sizeof(label), "%s (%s)", item->label, item->choices);
                    widget = ToriRSChrome_TextInput(
                        &app->dbg_ui,
                        app->plugin_panel,
                        label,
                        app_plugin_panel_value(app, p, item->key));
                }
                else
                    widget = ToriRSChrome_TextInput(
                        &app->dbg_ui,
                        app->plugin_panel,
                        item->label,
                        app_plugin_panel_value(app, p, item->key));
                break;
            }
            app_plugin_panel_track(app, widget, p, c);
        }
    }

    ToriRSChrome_PanelSetResizable(&app->dbg_ui, app->plugin_panel, 1);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->plugin_panel, app->plugin_panel_visible);
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
            app->plugin_prefs_dirty = 1;
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
        app->plugin_prefs_dirty = 1;
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

    /* Scripts register asynchronously, so the list the panel was built from
     * can still be growing the first few frames it is open. */
    if( app->plugin_panel_built_for != PluginHost_Count(app->plugins) )
        app_plugin_panel_build(app);
}
