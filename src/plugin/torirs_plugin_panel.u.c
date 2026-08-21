/*
 * The plugin window.
 *
 * One window, tabbed: the first tab is the roster -- every plugin, its switch
 * and its last fault -- and every plugin that asked for one gets a tab of its
 * own holding its settings and whatever controls it declared. That is the
 * sandbox rule in the plan made concrete: plugins share ONE extra window, and
 * api->win_request claims a tab in it rather than a window of its own.
 *
 * Two kinds of row live on a plugin's tab and they behave differently on
 * purpose:
 *
 *  - SETTINGS, generated from the plugin's declared config schema, are STAGED.
 *    Typing in one changes nothing until Save, and Save reloads the plugin so
 *    its on_start sees a coherent config rather than a value that arrived
 *    halfway through a run.
 *  - CONTROLS the plugin declared itself are dispatched the moment they are
 *    used, because a plugin's own button is an action rather than a setting.
 *
 * The chrome IS the staging buffer, which is why no third copy of a pending
 * edit exists anywhere: a retained widget already holds exactly "what the user
 * typed and has not committed", and reading it back at Save is the whole of
 * what staging needs.
 *
 * Included into app.c for the same reason as the bridge -- it works on App
 * state and on the chrome the App owns.
 */

/*
 * Split a `"a|b|c"` choice list into the window's pool and hand back the slice
 * the chrome should borrow.
 *
 * @param out_count how many entries were taken.
 * @return the first pointer of the slice, or NULL when the list was empty or
 * the pool is full -- both of which the caller answers by falling back to a
 * text field, because a dropdown with no options is a control that cannot be
 * used rather than an empty one.
 *
 * The pool is reset once per panel rebuild, so the slices live exactly as long
 * as the widgets pointing into them.
 */
static char const* const*
app_plugin_choices_add(struct App* app, char const* choices, int* out_count)
{
    int const first = app->plugin_choice_count;
    int n = 0;
    char const* at = choices;

    *out_count = 0;
    if( !choices || !choices[0] )
        return NULL;

    while( *at )
    {
        char const* end = strchr(at, '|');
        int len = end ? (int)(end - at) : (int)strlen(at);

        if( app->plugin_choice_count >= APP_PLUGIN_CHOICES_MAX )
            break;
        if( len > (int)sizeof(app->plugin_choice_text[0]) - 1 )
            len = (int)sizeof(app->plugin_choice_text[0]) - 1;
        memcpy(app->plugin_choice_text[app->plugin_choice_count], at, (size_t)len);
        app->plugin_choice_text[app->plugin_choice_count][len] = '\0';
        app->plugin_choice_ptrs[app->plugin_choice_count] =
            app->plugin_choice_text[app->plugin_choice_count];
        app->plugin_choice_count++;
        n++;
        if( !end )
            break;
        at = end + 1;
    }

    if( n == 0 )
        return NULL;
    *out_count = n;
    return &app->plugin_choice_ptrs[first];
}

/**
 * The string a dropdown widget currently shows, or NULL when it is not a
 * dropdown or has nothing selected.
 *
 * Reaches into the widget for its borrowed options array rather than
 * re-splitting the schema: the array the chrome is holding IS the pool slice
 * this window handed it, so reading it back is the only way to be sure the
 * index and the strings agree.
 */
static char const*
app_plugin_dropdown_value(struct App* app, int widget)
{
    struct ToriDbgWidget const* w;
    int sel;

    if( widget < 0 || widget >= app->plugin_ui.widget_count )
        return NULL;
    w = &app->plugin_ui.widgets[widget];
    if( w->kind != TORIDBG_W_DROPDOWN || !w->options )
        return NULL;
    sel = ToriRSChrome_DropdownSelected(&app->plugin_ui, widget);
    if( sel < 0 || sel >= w->option_count )
        return NULL;
    return w->options[sel];
}

/** Index of `value` within a split choice slice, or -1. */
static int
app_plugin_choice_index(char const* const* choices, int count, char const* value)
{
    if( !value )
        return -1;
    for( int i = 0; i < count; i++ )
        if( strcmp(choices[i], value) == 0 )
            return i;
    return -1;
}

/* Value shown for a config key: the store's own string, so what the panel
 * displays is exactly what would be written to the ini. */
static char const*
app_plugin_panel_value(struct App* app, int plugin, char const* key)
{
    char const* v = PluginHost_ConfigGet(app->plugins, plugin, key);
    return v ? v : "";
}

static void
app_plugin_panel_track(
    struct App* app, int widget, int plugin, int kind, int cfg_index, char const* widget_id)
{
    struct AppPluginPanelRow* row;

    if( widget < 0 )
        return;
    if( app->plugin_panel_row_count >= APP_PLUGIN_PANEL_ROWS_MAX )
        return;

    row = &app->plugin_panel_rows[app->plugin_panel_row_count++];
    row->widget = widget;
    row->plugin = plugin;
    row->kind = kind;
    row->cfg_index = cfg_index;
    snprintf(row->widget_id, sizeof(row->widget_id), "%s", widget_id ? widget_id : "");
}

/** Put one config key's stored value into the widget showing it. */
static void
app_plugin_panel_load_row(struct App* app, struct AppPluginPanelRow const* row)
{
    struct ToriRS_PluginConfigItem const* item;

    if( row->kind != APP_PLUGIN_ROW_CONFIG )
        return;
    item = PluginHost_ConfigItem(app->plugins, row->plugin, row->cfg_index);
    if( !item )
        return;
    if( item->type == TORIRS_PLUGIN_CFG_BOOL )
    {
        ToriRSChrome_SetChecked(
            &app->plugin_ui,
            row->widget,
            atoi(app_plugin_panel_value(app, row->plugin, item->key)) != 0);
    }
    else if( item->type == TORIRS_PLUGIN_CFG_ENUM &&
             row->widget < app->plugin_ui.widget_count &&
             app->plugin_ui.widgets[row->widget].kind == TORIDBG_W_DROPDOWN )
    {
        /* A dropdown reverts by SELECTION, not by text: setting its text would
         * write a field it does not draw and leave the visible choice alone. */
        struct ToriDbgWidget const* w = &app->plugin_ui.widgets[row->widget];
        ToriRSChrome_DropdownSetSelected(
            &app->plugin_ui,
            row->widget,
            app_plugin_choice_index(
                w->options, w->option_count,
                app_plugin_panel_value(app, row->plugin, item->key)));
    }
    else
    {
        ToriRSChrome_SetText(
            &app->plugin_ui, row->widget, app_plugin_panel_value(app, row->plugin, item->key));
    }
}

/*
 * Rebuild the whole window from the host's registry.
 *
 * A real rebuild, not an append. The old panel could only ever grow -- the
 * chrome had no remove-widget call, so the only "rebuild" was Reset, which
 * would have taken every other panel down with it. With
 * ToriRSChrome_PanelClearWidgets it can be rebuilt in place, which is what
 * makes a plugin's tab able to CHANGE: a reload, a re-declared control set, a
 * plugin being disabled and taking its tab with it.
 *
 * Driven off (plugin count, window revision) rather than run every frame: the
 * revision moves only when the window's SHAPE moves, so a checkbox being
 * ticked mirrors onto the widget that already exists instead of rebuilding the
 * tab around it -- and a text field being typed into is not torn down under
 * the caret.
 */
static void
app_plugin_panel_sync(struct App* app)
{
    int count;
    int rev;
    int tab;

    assert(app);
    if( !app->plugins )
        return;

    count = PluginHost_Count(app->plugins);
    rev = PluginHost_WinRevision(app->plugins);
    if( app->plugin_panel_built_for == count && app->plugin_panel_built_rev == rev )
        return;

    if( app->plugin_panel < 0 )
    {
        /*
         * Sized in CHROME pixels, so the window holds the same number of rows
         * on a HighDPI display as on an ordinary one.
         *
         * A panel box is absolute screen pixels, but its ROWS grow with the
         * chrome scale -- so a hand-set 260 that fits nine rows at 1x fits four
         * at 2x, and the window silently becomes a third of itself on a Retina
         * machine. Multiplying by the scale is what keeps "how much of my
         * settings can I see" a property of the window rather than of the
         * display.
         */
        int const scale = ToriRSChrome_Scale(&app->plugin_ui);
        app->plugin_panel = ToriRSChrome_PanelAdd(
            &app->plugin_ui, TORIDBG_PANEL_WINDOW, 8 * scale, 72 * scale, 320 * scale,
            "Plugins");
        if( app->plugin_panel < 0 )
            return;
        ToriRSChrome_PanelSetResizable(&app->plugin_ui, app->plugin_panel, 1);
        /* Tall enough to be useful, short enough to leave the game visible.
         * Scrollable because a plugin with a dozen settings will overflow it,
         * and rows below the fold used to be simply dropped. */
        app->plugin_ui.panels[app->plugin_panel].fixed_h = 260 * scale;
        ToriRSChrome_PanelSetScrollable(&app->plugin_ui, app->plugin_panel, 1);
        ToriRSChrome_PanelSetTable(&app->plugin_ui, app->plugin_panel, 1);
        ToriRSChrome_PanelSetVisible(
            &app->plugin_ui, app->plugin_panel, app->plugin_panel_visible);
    }

    /*
     * Ask EVERY plugin to declare its controls, before the walk below reads
     * them -- not only the ones that already have a tab.
     *
     * "Already has a tab" was the obvious gate and it is a deadlock: claiming
     * the tab is the first thing a plugin does inside the build handler, so a
     * plugin with no tab was never asked to build one and could therefore
     * never get one. Asking everyone is also cheap, because a plugin with no
     * on_ui_build handler has nothing subscribed and a tab that already has
     * controls returns immediately.
     */
    for( int p = 0; p < count; p++ )
        PluginHost_WinBuild(app->plugins, p);

    ToriRSChrome_PanelClearWidgets(&app->plugin_ui, app->plugin_panel);
    app->plugin_panel_row_count = 0;
    /* Reset with the widgets that point into it -- the slices handed out below
     * must live exactly as long as the row set they belong to. */
    app->plugin_choice_count = 0;

    /* The tab strip: the roster, then one per plugin that asked for a tab. The
     * titles are borrowed by the strip, so they live in App state. */
    app->plugin_tab_count = 0;
    snprintf(app->plugin_tab_titles[0], sizeof(app->plugin_tab_titles[0]), "Plugins");
    app->plugin_tab_owner[0] = -1;
    app->plugin_tab_count = 1;
    for( int p = 0; p < count && app->plugin_tab_count < TORIRS_PLUGIN_MAX; p++ )
    {
        char const* title;
        if( !PluginHost_WinHasTab(app->plugins, p) )
            continue;
        title = PluginHost_WinTabTitle(app->plugins, p);
        snprintf(
            app->plugin_tab_titles[app->plugin_tab_count],
            sizeof(app->plugin_tab_titles[0]),
            "%s",
            title[0] ? title : PluginHost_Name(app->plugins, p));
        app->plugin_tab_owner[app->plugin_tab_count] = p;
        app->plugin_tab_count++;
    }
    for( int i = 0; i < app->plugin_tab_count; i++ )
        app->plugin_tab_title_ptrs[i] = app->plugin_tab_titles[i];

    app->plugin_panel_tabs = ToriRSChrome_Tabs(
        &app->plugin_ui,
        app->plugin_panel,
        app->plugin_tab_title_ptrs,
        app->plugin_tab_count,
        ToriRSChrome_PanelActiveTab(&app->plugin_ui, app->plugin_panel));

    /* ---- tab 0: the roster ------------------------------------------------ */

    ToriRSChrome_PanelBeginTab(&app->plugin_ui, app->plugin_panel, 0);
    for( int p = 0; p < count; p++ )
    {
        char label[TORIDBG_INPUT_MAX];
        char const* err = PluginHost_Error(app->plugins, p);

        snprintf(label, sizeof(label), "%s", PluginHost_Name(app->plugins, p));
        app_plugin_panel_track(
            app,
            ToriRSChrome_Checkbox(
                &app->plugin_ui,
                app->plugin_panel,
                label,
                PluginHost_IsEnabled(app->plugins, p) ? 1 : 0),
            p,
            APP_PLUGIN_ROW_ENABLE,
            -1,
            NULL);

        /* A script that faulted says so where its switch is, rather than only
         * in a log nobody has open. */
        if( err )
        {
            snprintf(label, sizeof(label), "  ! %s", err);
            ToriRSChrome_LabelColored(
                &app->plugin_ui, app->plugin_panel, label, 0xFFCC5555u);
        }
    }

    /* ---- one tab per plugin ----------------------------------------------- */

    for( tab = 1; tab < app->plugin_tab_count; tab++ )
    {
        int const p = app->plugin_tab_owner[tab];
        int const cfg_count = PluginHost_ConfigCount(app->plugins, p);
        int const win_count = PluginHost_WinWidgetCount(app->plugins, p);
        int has_settings = 0;

        ToriRSChrome_PanelBeginTab(&app->plugin_ui, app->plugin_panel, tab);

        for( int c = 0; c < cfg_count; c++ )
        {
            struct ToriRS_PluginConfigItem const* item =
                PluginHost_ConfigItem(app->plugins, p, c);
            int widget = -1;

            /* A key with no label is state the plugin persists for itself --
             * the highlighter's tag list -- not something to hand-edit. */
            if( !item || !item->label )
                continue;
            has_settings = 1;

            if( item->type == TORIRS_PLUGIN_CFG_BOOL )
            {
                widget = ToriRSChrome_Checkbox(
                    &app->plugin_ui,
                    app->plugin_panel,
                    item->label,
                    atoi(app_plugin_panel_value(app, p, item->key)) != 0);
            }
            else if( item->type == TORIRS_PLUGIN_CFG_ENUM && item->choices )
            {
                /* A declared enum is a real dropdown, not a text field with the
                 * choices printed in its label: the schema already says exactly
                 * what the legal values are, and a field that accepts anything
                 * for a key that accepts three things is a typo waiting to be
                 * saved. */
                int count = 0;
                char const* const* choices =
                    app_plugin_choices_add(app, item->choices, &count);
                char const* value = app_plugin_panel_value(app, p, item->key);

                if( choices )
                    widget = ToriRSChrome_Dropdown(
                        &app->plugin_ui,
                        app->plugin_panel,
                        item->label,
                        choices,
                        count,
                        app_plugin_choice_index(choices, count, value));
                else
                    widget = ToriRSChrome_TextInput(
                        &app->plugin_ui, app->plugin_panel, item->label, value);
            }
            else
            {
                /* Ints, colours and strings edit as text, because the store is
                 * textual to begin with -- the field shows the value that would
                 * be written, not a rendering of it. */
                widget = ToriRSChrome_TextInput(
                    &app->plugin_ui,
                    app->plugin_panel,
                    item->label,
                    app_plugin_panel_value(app, p, item->key));
            }
            app_plugin_panel_track(app, widget, p, APP_PLUGIN_ROW_CONFIG, c, NULL);
        }

        /* The plugin's own controls, under a rule so the two groups read as
         * two groups: above it is the client's settings form, below it is
         * whatever the plugin built. */
        if( has_settings && win_count > 0 )
            ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);

        for( int i = 0; i < win_count; i++ )
        {
            struct ToriRS_PluginWinWidget const* w =
                PluginHost_WinWidgetAt(app->plugins, p, i);
            int widget = -1;

            if( !w )
                continue;
            switch( w->kind )
            {
            case TORIRS_PLUGIN_W_CHECKBOX:
                widget = ToriRSChrome_Checkbox(
                    &app->plugin_ui, app->plugin_panel, w->label, w->checked);
                break;
            case TORIRS_PLUGIN_W_INPUT:
                widget = ToriRSChrome_TextInput(
                    &app->plugin_ui, app->plugin_panel, w->label, w->text);
                break;
            case TORIRS_PLUGIN_W_BUTTON:
                widget = ToriRSChrome_Button(
                    &app->plugin_ui, app->plugin_panel, w->label[0] ? w->label : w->id);
                break;
            case TORIRS_PLUGIN_W_SEPARATOR:
                widget = ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);
                break;
            case TORIRS_PLUGIN_W_DROPDOWN:
            {
                /* Split into the window's own pool: the plugin owns its
                 * "a|b|c" string and the chrome borrows an array that must
                 * outlive the widget, so neither side's storage will do. */
                int count = 0;
                char const* const* choices =
                    app_plugin_choices_add(app, w->choices, &count);
                if( choices )
                    widget = ToriRSChrome_Dropdown(
                        &app->plugin_ui,
                        app->plugin_panel,
                        w->label,
                        choices,
                        count,
                        w->selected >= 0 && w->selected < count
                            ? w->selected
                            : app_plugin_choice_index(choices, count, w->text));
                else
                    widget = ToriRSChrome_TextInput(
                        &app->plugin_ui, app->plugin_panel, w->label, w->text);
                break;
            }
            case TORIRS_PLUGIN_W_LABEL:
            default:
                widget = ToriRSChrome_Label(
                    &app->plugin_ui, app->plugin_panel, w->label[0] ? w->label : w->text);
                break;
            }
            app_plugin_panel_track(
                app, widget, p, APP_PLUGIN_ROW_PLUGIN_WIDGET, -1, w->id);
        }

        /* Save and Revert, only where there is something staged to commit. */
        if( has_settings )
        {
            ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);
            app_plugin_panel_track(
                app,
                ToriRSChrome_Button(&app->plugin_ui, app->plugin_panel, "Save"),
                p,
                APP_PLUGIN_ROW_SAVE,
                -1,
                NULL);
            app_plugin_panel_track(
                app,
                ToriRSChrome_Button(&app->plugin_ui, app->plugin_panel, "Revert"),
                p,
                APP_PLUGIN_ROW_REVERT,
                -1,
                NULL);
        }
    }

    ToriRSChrome_PanelBeginTab(&app->plugin_ui, app->plugin_panel, -1);
    app->plugin_panel_built_for = count;
    app->plugin_panel_built_rev = rev;
}

/*
 * Commit one plugin's staged settings, then reload it.
 *
 * The reload is the point rather than a courtesy: a plugin reads its config in
 * on_start and caches what it found, so writing a key underneath a running
 * plugin leaves it running on the old value with the panel showing the new one.
 * Restarting it is what makes "Save" mean what it says.
 */
static void
app_plugin_panel_save(struct App* app, int plugin)
{
    assert(app);

    for( int i = 0; i < app->plugin_panel_row_count; i++ )
    {
        struct AppPluginPanelRow const* row = &app->plugin_panel_rows[i];
        struct ToriRS_PluginConfigItem const* item;

        if( row->plugin != plugin || row->kind != APP_PLUGIN_ROW_CONFIG )
            continue;
        item = PluginHost_ConfigItem(app->plugins, plugin, row->cfg_index);
        if( !item )
            continue;

        if( item->type == TORIRS_PLUGIN_CFG_BOOL )
        {
            char buf[4];
            snprintf(
                buf, sizeof(buf), "%d",
                ToriRSChrome_Checked(&app->plugin_ui, row->widget) ? 1 : 0);
            PluginHost_ConfigSet(app->plugins, plugin, item->key, buf);
        }
        else if( item->type == TORIRS_PLUGIN_CFG_ENUM )
        {
            /* The chosen OPTION, not the widget's text field -- a dropdown's
             * text is empty, so reading it here wrote every enum key blank on
             * Save. */
            char const* chosen = app_plugin_dropdown_value(app, row->widget);
            if( chosen )
                PluginHost_ConfigSet(app->plugins, plugin, item->key, chosen);
            else
                PluginHost_ConfigSet(
                    app->plugins, plugin, item->key,
                    ToriRSChrome_Text(&app->plugin_ui, row->widget));
        }
        else
        {
            char const* text = ToriRSChrome_Text(&app->plugin_ui, row->widget);
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
                PluginHost_ConfigSet(app->plugins, plugin, item->key, buf);
                ToriRSChrome_SetText(&app->plugin_ui, row->widget, buf);
            }
            else
                PluginHost_ConfigSet(app->plugins, plugin, item->key, text);
        }
    }

    /* The prefs write is the existing settle-delayed task, driven off the
     * host's dirty flag by app_logic_tick -- nothing to do here but leave it
     * dirty, which ConfigSet already did. */
    PluginHost_Reload(app->plugins, plugin);
}

/** Throw away this tab's staged edits by reloading every row from the store. */
static void
app_plugin_panel_revert(struct App* app, int plugin)
{
    for( int i = 0; i < app->plugin_panel_row_count; i++ )
        if( app->plugin_panel_rows[i].plugin == plugin )
            app_plugin_panel_load_row(app, &app->plugin_panel_rows[i]);
}

/* Apply one activated widget. */
static void
app_plugin_panel_apply(struct App* app, int widget)
{
    for( int i = 0; i < app->plugin_panel_row_count; i++ )
    {
        struct AppPluginPanelRow const* row = &app->plugin_panel_rows[i];
        if( row->widget != widget )
            continue;

        switch( row->kind )
        {
        case APP_PLUGIN_ROW_ENABLE:
        {
            int const on = ToriRSChrome_Checked(&app->plugin_ui, widget);
            PluginHost_SetEnabled(app->plugins, row->plugin, on != 0);
            /* Re-enabling clears the fault note: whatever it said was about
             * the run that just ended. */
            if( on )
                PluginHost_SetError(app->plugins, row->plugin, NULL);
            return;
        }

        case APP_PLUGIN_ROW_SAVE:
            app_plugin_panel_save(app, row->plugin);
            return;

        case APP_PLUGIN_ROW_REVERT:
            app_plugin_panel_revert(app, row->plugin);
            return;

        case APP_PLUGIN_ROW_PLUGIN_WIDGET:
        {
            /* Straight to the owning plugin, with whatever the control now
             * says. Not staged: a plugin's own control is an action. */
            int const kind = ToriRSChrome_Checked(&app->plugin_ui, widget);
            char const* text = ToriRSChrome_Text(&app->plugin_ui, widget);
            struct ToriRS_PluginWinWidget const* w = NULL;

            for( int j = 0; j < PluginHost_WinWidgetCount(app->plugins, row->plugin); j++ )
            {
                struct ToriRS_PluginWinWidget const* c =
                    PluginHost_WinWidgetAt(app->plugins, row->plugin, j);
                if( c && strcmp(c->id, row->widget_id) == 0 )
                {
                    w = c;
                    break;
                }
            }
            if( !w )
                return;

            switch( w->kind )
            {
            case TORIRS_PLUGIN_W_CHECKBOX:
                PluginHost_WinDispatch(
                    app->plugins, row->plugin, row->widget_id, TORIRS_PLUGIN_UI_TOGGLE, kind,
                    NULL);
                break;
            case TORIRS_PLUGIN_W_INPUT:
                PluginHost_WinDispatch(
                    app->plugins, row->plugin, row->widget_id, TORIRS_PLUGIN_UI_TEXT, -1, text);
                break;

            case TORIRS_PLUGIN_W_DROPDOWN:
            {
                /* PICK, carrying both the index and the string: a plugin that
                 * switches on the value should not have to keep its own copy
                 * of the list to turn an index back into one. */
                char const* chosen = app_plugin_dropdown_value(app, widget);
                PluginHost_WinDispatch(
                    app->plugins,
                    row->plugin,
                    row->widget_id,
                    TORIRS_PLUGIN_UI_PICK,
                    ToriRSChrome_DropdownSelected(&app->plugin_ui, widget),
                    chosen ? chosen : "");
                break;
            }
            default:
                PluginHost_WinDispatch(
                    app->plugins, row->plugin, row->widget_id, TORIRS_PLUGIN_UI_ACTIVATE, -1,
                    NULL);
                break;
            }
            return;
        }

        case APP_PLUGIN_ROW_CONFIG:
        default:
            /* Staged. Nothing happens until Save reads the widget back. */
            return;
        }
    }
}



/* ---- the sidebar Plugin button --------------------------------------------
 *
 * A fourth entry in the gameframe's own popout strip -- interface 728
 * `popout`, the launcher column down the right edge that carries XP Tracker,
 * Loot Tools and Hiscores. Clicking it opens the plugin window in that strip's
 * panel slot, framed by the strip's own chrome.
 *
 * WHY THE CLIENT ADDS IT, not the content. The three shipped entries come from
 * the cache: `enum_4067` maps a 1-based slot to a struct carrying param_1412
 * (icon graphic) and param_1413 (label), and the server arms the range and
 * answers `[if_button1,popout:buttons]`. A fourth authored the same way would
 * work -- and would mean the plugin window exists only against a server that
 * has been taught about plugins. Plugins are a CLIENT feature; the same client
 * pointed at any other server must still have them. So the client appends its
 * own button and answers its own click, and the cache is untouched.
 *
 * Everything it needs is already on screen by then, which is why this waits:
 * `popout:buttons` (728:6) holds the three icons as 30x30 children on a 36px
 * pitch, and `popout:container` (728:9) is the slot the panels mount into.
 */

/** interface 728 `popout`, and the two components of it this uses. */
#define APP_POPOUT_IFACE 728
#define APP_POPOUT_BUTTONS ((APP_POPOUT_IFACE << 16) | 6)
#define APP_POPOUT_CONTAINER ((APP_POPOUT_IFACE << 16) | 9)
/** Slot 3: the strip ships 0..2 (XP Tracker, Loot Tools, Hiscores). */
#define APP_POPOUT_SLOT_PLUGINS 3
/** Geometry read off the live strip: 30x30 icons on a 36px pitch. */
#define APP_POPOUT_BTN 30
#define APP_POPOUT_PITCH 36
/**
 * `sideicons_interface_11` -- the wrench the game already uses for settings.
 *
 * 19x28, so it sits in the strip's 30x30 slot at native size. That matters:
 * a UI sprite downscaled at draw time speckles, because the outline is baked
 * before the scale -- which is why `icon_tools` (3031), crossed hammer and axe
 * and the better metaphor, is the wrong answer at 62x54.
 *
 * The strip's own unused icons are branding letters (a red R, a blue B) and
 * say nothing; `sideicons_interface_22` is a cog but has a person in it and
 * reads as "account". A bare wrench is what this game already means by
 * "settings", so it needs no explaining.
 */
#define APP_POPOUT_ICON 785
/** `^clientscript_popout_layout` -- the strip's layout owner. */
#define APP_POPOUT_LAYOUT_SCRIPT 5354

/**
 * Scene sprite for a cache graphic id, REQUESTING the load if it is not here.
 *
 * UITreeSceneBridge_EnsureSprite only serves sprites the provider already
 * holds, and a provider only holds what something asked for. Every sprite on
 * screen was pulled in by the interface that references it, so an icon this
 * client picked out of the cache on its own has never been requested by
 * anybody: the first call returns -1 and the button draws nothing at all.
 *
 * So: ask, and let the next frame try again. That is the same
 * request-then-retry the world's own sprite path uses; the alternative is a
 * button that is silently blank on every cache whose interfaces happen not to
 * use this icon.
 */
static int
app_plugin_button_sprite(void* ud, int graphic_id)
{
    struct App* app = ud;
    struct ToriRS_Sprite* sprite;
    int scene_id;

    if( graphic_id <= 0 )
        return -1;

    sprite = CacheProvider_SpriteGet(app->provider, graphic_id);
    if( !sprite || sprite->frame_count <= 0 )
    {
        struct ToriRS_Task* task = CreateTask_SpriteLoad(app->provider, graphic_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
        return -1;
    }
    scene_id = UITreeSceneBridge_EnsureSprite(&app->bridge, graphic_id);
    return scene_id > 0 ? scene_id : -1;
}

/** Is the button we added still in the tree? A gameframe rebuild takes it, and
 *  the node index alone cannot say so. */
static int
app_plugin_button_alive(struct App const* app)
{
    if( !app->tree || app->plugin_button_node < 0 )
        return 0;
    if( (uint32_t)app->plugin_button_node >= app->tree->component_count )
        return 0;
    return app->tree->components[app->plugin_button_node].component_id ==
           TORIRS_CHROME_PLUGIN_BUTTON_ID;
}

static void
app_plugin_button_sync(struct App* app)
{
    struct UIBuildComponent comp;
    int32_t buttons;
    int32_t node;

    if( !app->tree || !ToriRSChrome_TreeAcceptsChrome(app->tree) )
        return;
    if( app_plugin_button_alive(app) )
        return;

    /* Only once the gameframe has mounted the strip. Nothing offline can say
     * when that is -- the server mounts 728 under the toplevel at login -- so
     * the presence of the component IS the readiness test. */
    buttons = UITree_FindByComponentId(app->tree, APP_POPOUT_BUTTONS);
    if( buttons < 0 )
        return;

    /* Not until the icon is resolvable. Building the component first and
     * letting the sprite arrive later would need the graphic patched in after
     * the fact; the load is requested by the probe below, so this is one
     * frame's wait and no extra machinery. */
    if( app_plugin_button_sprite(app, APP_POPOUT_ICON) < 0 )
        return;

    memset(&comp, 0, sizeof(comp));
    comp.id = TORIRS_CHROME_PLUGIN_BUTTON_ID;
    comp.type = UIBUILD_GRAPHIC;
    comp.parent_id = -1;
    comp.base_x = 0;
    comp.base_y = APP_POPOUT_SLOT_PLUGINS * APP_POPOUT_PITCH;
    comp.base_width = APP_POPOUT_BTN;
    comp.base_height = APP_POPOUT_BTN;
    comp.if3 = 1;
    comp.graphic = APP_POPOUT_ICON;
    comp.graphic_active = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    /* Op 1, so the strip's own hover line names it -- the shipped buttons set
     * "Open"/"Close" the same way (script5356's cc_setop) and a launcher that
     * says nothing on hover is the one thing in the column that does not. */
    snprintf(comp.ops[0], sizeof(comp.ops[0]), "Show Plugin Settings");
    node = UITree_PushBuildComponent(
        app->tree, buttons, &comp, app_plugin_button_sprite, NULL, app);
    if( node < 0 )
        return;

    app->plugin_button_node = node;
    UITree_ApplyClickMask(app->tree, TORIRS_CHROME_PLUGIN_BUTTON_ID, 1);
    UITree_MarkAllDirty(app->tree);
}

/**
 * Drive the strip's own layout pass.
 *
 * clientscript 5354 is `^clientscript_popout_layout` -- the cache's own
 * `~script5355(~script900)` wrapper, and the ONLY thing that widens the strip
 * when a panel opens or collapses it when one closes. It decides by asking
 * `if_hassub(popout:container)`, so the mount registration below has to happen
 * first; running it without that collapses the strip back to 42px with the
 * panel still inside, which is the panel squeezed to nothing.
 *
 * Queued through the same path a server-sent RUNCLIENTSCRIPT takes, rather than
 * run inline: it is held to the tick fence, which is where the rest of the
 * frame's interface work already lands.
 */
static void
app_plugin_popout_relayout(struct App* app)
{
    struct PktRunClientScript req;

    memset(&req, 0, sizeof(req));
    req.script_id = APP_POPOUT_LAYOUT_SCRIPT;
    req.argc = 0;
    App_RunClientScript(app, &req);
}

/**
 * Open or close the plugin panel IN THE STRIP, the way the shipped three do.
 *
 * Not a modal, and not a floating window: the panel mounts into
 * `popout:container` (728:9) and the strip widens around it, which is what
 * makes it read as a fourth tracker rather than as something bolted on.
 *
 * The registration in the InterfaceParent map is what makes that happen.
 * `if_hassub` -- the test 5355 branches on -- is implemented as
 * `UITree_InterfaceParentFind(tree, component_id) >= 0`, so a client-built
 * panel has to appear in that map exactly as a server-mounted group would.
 * Registering our own chrome group there is the whole trick: everything
 * downstream (the widening, the frame, the collapse on close) is the cache's
 * own code doing what it already does.
 *
 * @return 1 when the click was the Plugin button, so the caller stops.
 */
static void
app_plugin_window_set_open(struct App* app, int open)
{
    app->plugin_panel_visible = open ? 1 : 0;
    if( app->plugin_panel >= 0 )
        ToriRSChrome_PanelSetVisible(
            &app->plugin_ui, app->plugin_panel, app->plugin_panel_visible);

    if( !app->tree )
        return;
    if( UITree_FindByComponentId(app->tree, APP_POPOUT_CONTAINER) < 0 )
        return;

    if( app->plugin_panel_visible )
        UITree_InterfaceParentSet(
            app->tree, APP_POPOUT_CONTAINER, TORIRS_CHROME_CS2_GROUP, 1);
    else
        UITree_InterfaceParentClear(app->tree, APP_POPOUT_CONTAINER);
    app_plugin_popout_relayout(app);
}

static int
app_plugin_button_click(struct App* app, int component_id)
{
    if( component_id != TORIRS_CHROME_PLUGIN_BUTTON_ID )
        return 0;
    app_plugin_window_set_open(app, !app->plugin_panel_visible);
    return 1;
}

/*
 * Bind the window to a presentation, the first time it is opened.
 *
 * Lazily, not at boot: an executor that opens an OS window must not open one
 * for a window the user never asked to see, and most sessions never open this
 * at all. Which one is asked for comes from TORIRS_CHROME_EXECUTOR, alongside
 * TORIRS_CHROME_THEME which the developer chrome already reads.
 *
 * An executor that will not start is not an error. ToriRSChromeExec_ForKind
 * already answers "not compiled in on this platform" with the buffer executor,
 * and a refused begin() is answered here with the same thing -- so a blocked
 * popup, a display that will not give a second window, or a lane with no
 * native executor all end at in-canvas chrome rather than at no chrome.
 */
static void
app_plugin_exec_bind(struct App* app)
{
    if( app->plugin_exec.live )
        return;

    /*
     * The CS2 executor is bound here rather than by the shell, because it is
     * the one whose target is not a platform: it needs the interface tree and
     * a node to mount under, and both are the App's. The shell has neither,
     * which is why ToriRSChromeExec_ForKind answers "cs2" with the buffer
     * executor and leaves this to the only place that can.
     *
     * The main overlay slot is the mount: it is where the gameframe puts
     * things that float over the world without taking the modal, which is
     * exactly what a window a player opened beside the game is.
     */
    /*
     * CS2 is the plugin window's presentation, not one option among five.
     *
     * The window belongs in the gameframe's popout strip beside XP Tracker and
     * the rest, which means it has to BE interface components -- chrome drawn
     * over the game would be a floating panel wherever the strip happened to
     * expand to. So the strip's presence, not a config key, is what selects
     * this: where there is a container to mount into, that is where the window
     * goes. `[chrome] executor=` still picks among the others for a lane whose
     * gameframe has no strip.
     */
    if( app->tree && UITree_FindByComponentId(app->tree, APP_POPOUT_CONTAINER) >= 0 )
        app->plugin_exec_kind = TORIRS_CHROME_EXEC_CS2;

    if( app->plugin_exec_kind == TORIRS_CHROME_EXEC_CS2 && app->tree )
    {
        /*
         * The main overlay slot if the layout declares one, the tree root if
         * not.
         *
         * Not every gameframe has an overlay slot -- the rev-239 layout
         * declares chat, main_modal and side_modal and no more -- and falling
         * back to the root is honest rather than lucky: the root is what a
         * floating window with no slot of its own belongs to, and it is the
         * one node every tree has.
         */
        /*
         * Mounted as its own ROOT (-1), not inside a slot.
         *
         * A slot was the obvious choice and is wrong twice over: not every
         * gameframe declares an overlay slot (the rev-239 layout has chat,
         * main_modal and side_modal and no more), and a panel inside the first
         * root draws UNDER every later root -- so the gameframe covers it.
         *
         * A -1 parent appends a root after the existing ones and the emit walk
         * takes roots in order, so the window lands on top of the game and
         * under nothing. That is the same answer the in-canvas chrome gets from
         * running its pass last, reached the interface tree's own way.
         */
        /* The game's own p12 body face, the one interface text is set in --
         * asked of the bridge that uploaded it, because a scene font id is
         * what a TEXT component needs and only the bridge knows it. */
        int const font = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIDBG_FONT_BODY);
        /*
         * Mounted INTO the strip's panel slot, not as a root of its own.
         *
         * `popout:container` is where xptracker, loottools and hiscores go, and
         * their roots are authored pure fill-parent with no chrome -- the
         * nine-slice frame is a sibling under `popout:frame`. Building there
         * means the plugin panel gets the strip's frame, sizing and collapse
         * behaviour for free, and reads as a fourth tracker rather than as a
         * window floating over the game.
         */
        int32_t const mount = UITree_FindByComponentId(app->tree, APP_POPOUT_CONTAINER);
        struct ToriRSChromeExec cs2 = ToriRSChromeExec_Cs2(app->tree, mount, font);
        if( mount < 0 )
        {
            /* No strip on this gameframe -- stay in the canvas rather than
             * build a panel into nothing. */
            struct ToriRSChromeExec buffer = ToriRSChromeExec_Buffer();
            ToriRSChromeSync_Init(&app->plugin_exec, &buffer);
            app->plugin_exec_kind = TORIRS_CHROME_EXEC_BUFFER;
            return;
        }
        if( ToriRSChromeSync_Init(&app->plugin_exec, &cs2) )
            return;
        fprintf(
            stderr, "chrome: the cs2 executor would not start; staying in the canvas\n");
        app->plugin_exec_kind = TORIRS_CHROME_EXEC_BUFFER;
    }

    /* Nothing was handed over: in-canvas, which is also what every lane with
     * no native executor gets. */
    if( !app->plugin_exec_pending.begin && !app->plugin_exec_pending.apply )
    {
        struct ToriRSChromeExec buffer = ToriRSChromeExec_Buffer();
        ToriRSChromeSync_Init(&app->plugin_exec, &buffer);
        app->plugin_exec_kind = TORIRS_CHROME_EXEC_BUFFER;
        return;
    }

    if( !ToriRSChromeSync_Init(&app->plugin_exec, &app->plugin_exec_pending) )
    {
        struct ToriRSChromeExec buffer = ToriRSChromeExec_Buffer();
        fprintf(
            stderr,
            "chrome: the '%s' executor would not start; the plugin window stays in the "
            "canvas\n",
            ToriRSChromeExec_KindName(app->plugin_exec_kind));
        ToriRSChromeSync_Init(&app->plugin_exec, &buffer);
        app->plugin_exec_kind = TORIRS_CHROME_EXEC_BUFFER;
    }
}

/*
 * Per-frame: the toggle, the lazy build, input, and one activation.
 *
 * Runs beside the other chrome ticks at the top of App_RunOnce, before the
 * BOOTING early-out, so the window is usable while a cache is still loading --
 * which is exactly when someone wants to switch a misbehaving plugin off.
 */
static void
app_plugin_panel_tick(struct App* app, struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    if( !app->plugins )
        return;

    /* The button is kept alive whether or not the window is open -- it is how
     * the window is OPENED, so it cannot be gated on the window being up. */
    app_plugin_button_sync(app);

    /* Same suppression as every other chrome toggle: a focused chat line must
     * not flip windows. */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PLUGIN_PANEL) )
    {
        /* The same path the strip's button takes. Two entry points that opened
         * the window two different ways is how one of them ends up not
         * expanding the strip. */
        app_plugin_window_set_open(app, !app->plugin_panel_visible);
    }

    if( !app->plugin_panel_visible )
        return;

    app_plugin_exec_bind(app);

    /* Scripts register asynchronously, so the list can still be growing the
     * first few frames the window is open. */
    app_plugin_panel_sync(app);

    /*
     * Input, from whichever surface this window is on.
     *
     * A surface executor hands back the gesture in ITS coordinates and the
     * chrome takes it unchanged -- the panels were laid out in that space, and
     * the chrome has no idea a window moved. The in-canvas case is the game's
     * own pointer, which is already in the right space for the same reason.
     */
    if( app->plugin_exec.exec.is_surface )
    {
        struct ToriRSChromeSurfaceInput gesture;
        if( app->plugin_exec.exec.surface_input &&
            app->plugin_exec.exec.surface_input(app->plugin_exec.exec.user, &gesture) )
        {
            ToriRSChrome_MouseMove(&app->plugin_ui, gesture.mouse_x, gesture.mouse_y);
            if( gesture.mouse_down )
                ToriRSChrome_MouseDown(&app->plugin_ui, gesture.mouse_x, gesture.mouse_y);
            if( gesture.mouse_up )
                ToriRSChrome_MouseUp(&app->plugin_ui, gesture.mouse_x, gesture.mouse_y);
            if( gesture.wheel )
                ToriRSChrome_MouseWheel(
                    &app->plugin_ui, gesture.mouse_x, gesture.mouse_y, gesture.wheel);
            for( int i = 0; gesture.text[i]; i++ )
                ToriRSChrome_KeyChar(&app->plugin_ui, (unsigned char)gesture.text[i]);
            if( gesture.edit_key != TORIDBG_KEY_NONE )
                ToriRSChrome_KeyEdit(&app->plugin_ui, gesture.edit_key);
        }
    }
    else if( ToriRSChrome_HasVisiblePanel(&app->plugin_ui) )
    {
        /* Its own instance, so its own routing -- and no ownership dance with
         * the developer chrome's activation latch, because it no longer shares
         * one. That shared latch is what made the old panel peek before taking,
         * and two panels could still eat each other's clicks. */
        app_chrome_route_input(app, &app->plugin_ui, input);
    }

    /* Intents from a native-widget executor land on the model the same way a
     * click would, so the drain below sees both without knowing which. */
    ToriRSChromeSync_Pump(&app->plugin_exec, &app->plugin_ui);

    {
        int const activated = ToriRSChrome_TakeActivated(&app->plugin_ui);
        if( activated >= 0 )
            app_plugin_panel_apply(app, activated);
    }

    /*
     * Build our own display list, exactly as the developer overlay and the loc
     * editor do at the end of their ticks.
     *
     * Not optional and not someone else's job: Build only rebuilds when
     * something changed, and it is the caller that saw it change who must turn
     * that into need_redraw. Leaving it to the next tick's Build means the
     * frame this window first appears on is never marked dirty, the shell
     * re-presents the previous framebuffer, and the window is invisible until
     * something unrelated happens to request a repaint.
     */
    if( ToriRSChrome_Build(&app->plugin_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->plugin_ui);
    }

    /*
     * Hand the frame to the executor, whichever kind it is.
     *
     * Sync is run for both: a surface executor ignores the commands, but
     * running it anyway keeps the shadow current, so a window that is later
     * REBOUND to a native-widget executor is caught up from the model rather
     * than from a stale diff. Cheap on a quiet frame -- that is what the
     * shadow is for.
     */
    ToriRSChromeSync_Run(&app->plugin_exec, &app->plugin_ui);

    if( app->plugin_exec.exec.is_surface && app->plugin_exec.exec.present )
    {
        int count = 0;
        struct ToriDbgPrim const* prims = ToriRSChrome_Prims(&app->plugin_ui, &count);
        app->plugin_exec.exec.present(app->plugin_exec.exec.user, prims, count);
    }
}
