/*
 * The plugin window.
 *
 * One window, PAGED: the roster lists every plugin as a row carrying its
 * switch, its last fault and a way into its own page, and that page holds its
 * settings and whatever controls it declared. That is the sandbox rule in the
 * plan made concrete: plugins share ONE extra window, and api->win_request
 * claims a page in it rather than a window of its own.
 *
 * Pages rather than a tab per plugin, which is what this was first: a strip
 * lays its destinations out across one row, so eight plugins already compress
 * their captions past reading and the ninth has nowhere to go. A list scrolls
 * to any length, and a row has room for the switch as well as the name --
 * which is how RuneLite's plugin panel answers the same problem, and why this
 * borrows its navigation while drawing in the game's own chrome.
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
/** Debug clock for TORIRS_CHROME_DEBUG traces: counts panel ticks. */
static int g_plugin_panel_ticks;

/*
 * Which page the window is showing: -1 is the roster, otherwise the plugin
 * whose settings are open.
 *
 * PAGES, not tabs. A tab strip is a fixed set of destinations laid out across
 * one row, which is the wrong shape for a roster that grows with every plugin
 * installed: eight of them already compress their captions past reading, and
 * the strip has nowhere to put the ninth. RuneLite's plugin panel answers the
 * same problem the same way -- a scrolling list of rows, each with its switch,
 * and a drill-down into the one you asked about -- so that is the navigation
 * this uses, drawn in the game's own chrome.
 *
 * File-static rather than App state because it is this window's own view
 * state: nothing outside these functions can act on it, and the plugin host
 * has no opinion about which page is up.
 */
static int g_plugin_page = -1;
/** The page the widgets on screen were built for; a mismatch rebuilds. */
static int g_plugin_page_built = -1;
/** Handle of the page's Back button, or -1 on the roster. It belongs to no
 *  plugin, so it is remembered here rather than tracked as a row. */
static int g_plugin_back_widget = -1;

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
    struct ToriRSChromeWidget const* w;
    int sel;

    if( widget < 0 || widget >= app->plugin_ui.widget_count )
        return NULL;
    w = &app->plugin_ui.widgets[widget];
    if( w->kind != TORIRS_CHROME_W_DROPDOWN || !w->options )
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

/**
 * Does this plugin have a page worth opening -- settings to edit, or controls
 * it declared itself?
 *
 * A key with NO LABEL does not count: those are state the plugin persists for
 * itself (the highlighter's tag list), not something to hand-edit, and a row
 * offering a page that turns out to be empty is worse than one that offers
 * nothing.
 */
static int
app_plugin_has_page(struct App* app, int plugin)
{
    int const cfg_count = PluginHost_ConfigCount(app->plugins, plugin);

    if( PluginHost_WinWidgetCount(app->plugins, plugin) > 0 )
        return 1;
    for( int c = 0; c < cfg_count; c++ )
    {
        struct ToriRS_PluginConfigItem const* item =
            PluginHost_ConfigItem(app->plugins, plugin, c);
        if( item && item->label )
            return 1;
    }
    return 0;
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
/**
 * The packed HSL16 a stored colour value names.
 *
 * The store is textual and the picker's unit is a palette index, so this is
 * the one conversion between them. An unparseable value falls back to WHITE
 * rather than to black: a key whose text is broken should look wrong, and
 * black is what a legitimately-black colour also looks like.
 */
static int
app_plugin_color_hsl(char const* text)
{
    uint32_t rgb = 0xFFFFFFu;
    int expr = 0;

    /* Hex first, because the picker writes "#RRGGBB" and every round trip
     * comes back through here. Anything else the store may hold -- rgb(),
     * hsl16(), an arithmetic expression -- is what cfg_color reads it as, so
     * the swatch has to read it the same way or the panel would show white
     * for a colour the plugin is drawing correctly. */
    if( !ToriRSChrome_ParseHexRgb(text, &rgb) &&
        revconfig_parse_int_expr(text, NULL, &expr) )
        rgb = (uint32_t)expr & 0xFFFFFFu;
    /* NEAREST, so a key written by Save and read back on the next open comes
     * back as the same colour. The reference quantiser moves it a hue step
     * every trip, which is a marker that drifts a shade per session. */
    return ToriRSChrome_Hsl16NearestRgb(rgb);
}

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
             app->plugin_ui.widgets[row->widget].kind == TORIRS_CHROME_W_DROPDOWN )
    {
        /* A dropdown reverts by SELECTION, not by text: setting its text would
         * write a field it does not draw and leave the visible choice alone. */
        struct ToriRSChromeWidget const* w = &app->plugin_ui.widgets[row->widget];
        ToriRSChrome_DropdownSetSelected(
            &app->plugin_ui,
            row->widget,
            app_plugin_choice_index(
                w->options, w->option_count,
                app_plugin_panel_value(app, row->plugin, item->key)));
    }
    else if( item->type == TORIRS_PLUGIN_CFG_COLOR )
    {
        /* By VALUE, for the same reason a dropdown reverts by selection: the
         * hex a colour row shows is a rendering of its palette entry, so
         * writing the text alone would leave the swatch on the old colour. */
        ToriRSChrome_ColorPickSet(
            &app->plugin_ui,
            row->widget,
            app_plugin_color_hsl(app_plugin_panel_value(app, row->plugin, item->key)));
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

    assert(app);
    if( !app->plugins )
        return;

    count = PluginHost_Count(app->plugins);
    rev = PluginHost_WinRevision(app->plugins);
    if( app->plugin_panel_built_for == count && app->plugin_panel_built_rev == rev &&
        g_plugin_page_built == g_plugin_page )
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
            &app->plugin_ui, TORIRS_CHROME_PANEL_WINDOW, 8 * scale, 72 * scale, 320 * scale,
            "Plugins");
        if( app->plugin_panel < 0 )
            return;
        ToriRSChrome_PanelSetResizable(&app->plugin_ui, app->plugin_panel, 1);
        /* Tall enough to be useful, short enough to leave the game visible.
         * Scrollable because a plugin with a dozen settings will overflow it,
         * and rows below the fold used to be simply dropped. */
        app->plugin_ui.panels[app->plugin_panel].fixed_h = 260 * scale;
        ToriRSChrome_PanelSetScrollable(&app->plugin_ui, app->plugin_panel, 1);
        /* The interfaces' own nine-slice border rather than the minimenu's
         * rails. This is the one panel a PLAYER sees, and it has to look like
         * the game's own furniture whichever executor is bound to it. */
        ToriRSChrome_PanelSetFramed(&app->plugin_ui, app->plugin_panel, 1);
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

    /* A page that named a plugin the host no longer has is a page with nothing
     * to build; the roster is the honest answer to that. */
    if( g_plugin_page >= count )
        g_plugin_page = -1;

    /* The title says where you are, the way a page's own header would -- there
     * is no tab strip to say it any more. */
    ToriRSChrome_PanelSetTitle(
        &app->plugin_ui, app->plugin_panel,
        g_plugin_page < 0 ? "Plugins" : PluginHost_Title(app->plugins, g_plugin_page));

    ToriRSChrome_PanelClearWidgets(&app->plugin_ui, app->plugin_panel);
    g_plugin_back_widget = -1;
    app->plugin_panel_row_count = 0;
    /* Reset with the widgets that point into it -- the slices handed out below
     * must live exactly as long as the row set they belong to. */
    app->plugin_choice_count = 0;

    /* ---- the roster page -------------------------------------------------- */

    if( g_plugin_page < 0 )
    {
        /*
         * Two passes, essential rows first.
         *
         * Registration order already puts the client's own settings at the
         * top -- the registry lists it first on purpose -- but the roster is
         * not only that table: the Lua adapter registers a further plugin per
         * script it loads, and a build that grows a second static plugin ahead
         * of it would move it without anyone noticing. The pass is what makes
         * "first" a property of the row rather than of a table somewhere else.
         */
        for( int pass = 0; pass < 2; pass++ )
        {
            for( int p = 0; p < count; p++ )
            {
                char label[TORIRS_CHROME_INPUT_MAX];
                char const* err = PluginHost_Error(app->plugins, p);
                bool const essential = PluginHost_IsEssential(app->plugins, p);

                if( essential != (pass == 0) )
                    continue;

                /*
                 * An ADAPTER is machinery, and a working one has no row.
                 *
                 * The Lua adapter is registered beside the scripts it runs -- that
                 * uniformity is the whole design -- so it also appeared in the
                 * roster, called "lua", sitting among them and looking like a peer
                 * with nothing a user does to it. Its scripts are the rows; they
                 * speak for it.
                 *
                 * It comes back the moment it has something to say, and the two
                 * conditions below are the two states you cannot get out of
                 * otherwise: a fault has to be visible somewhere or a broken Lua
                 * layer is a client with no plugins and no explanation, and a
                 * switched-off adapter has to have a switch or it can never come
                 * back on.
                 */
                if( PluginHost_IsAdapter(app->plugins, p) && !err &&
                    PluginHost_IsEnabled(app->plugins, p) )
                    continue;

                /*
                 * A HIDDEN builtin has no row at all, faulting or not.
                 *
                 * Unlike an adapter, there is nothing here for the user to do
                 * about it: it is a feature of the client whose switch is in the
                 * cache's own All Settings panel, and a row here would be a second
                 * switch over the same thing. See ToriRS_PluginDef::hidden.
                 */
                if( PluginHost_IsHidden(app->plugins, p) )
                    continue;

                /* The TITLE, not the name: the name is the ini key, and a roster
                 * of kebab-case ids reads as a config file that got onto the
                 * screen. Nothing here keys off the string -- the row carries the
                 * plugin index. */
                snprintf(label, sizeof(label), "%s", PluginHost_Title(app->plugins, p));
                app_plugin_panel_track(
                    app,
                    /* An essential plugin's row carries no switch: it has one
                     * state, and a toggle drawn over it would be the only control
                     * on the screen that does nothing. */
                    essential ? ToriRSChrome_ListRowLocked(
                                    &app->plugin_ui, app->plugin_panel, label)
                              : ToriRSChrome_ListRow(
                                    &app->plugin_ui,
                                    app->plugin_panel,
                                    label,
                                    PluginHost_IsEnabled(app->plugins, p) ? 1 : 0,
                                    app_plugin_has_page(app, p)),
                    p,
                    APP_PLUGIN_ROW_ENABLE,
                    -1,
                    NULL);

                /* A script that faulted says so where its switch is, rather than
                 * only in a log nobody has open. */
                if( err )
                {
                    snprintf(label, sizeof(label), "  ! %s", err);
                    ToriRSChrome_LabelColored(
                        &app->plugin_ui, app->plugin_panel, label, 0xFFCC5555u);
                }
            }
        }
    }
    /* ---- one plugin's page ------------------------------------------------ */
    else
    {
        int const p = g_plugin_page;
        int const cfg_count = PluginHost_ConfigCount(app->plugins, p);
        int const win_count = PluginHost_WinWidgetCount(app->plugins, p);
        int has_settings = 0;

        /* Back first, so the way out is the first thing on the page and in the
         * same place on every page. Its handle is remembered rather than
         * tracked as a row: it belongs to no plugin. */
        g_plugin_back_widget =
            ToriRSChrome_Button(&app->plugin_ui, app->plugin_panel, "< Plugins");
        ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);

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
            else if( item->type == TORIRS_PLUGIN_CFG_COLOR )
            {
                /*
                 * A declared colour is a real picker, for the same reason a
                 * declared enum is a real dropdown: the schema already says
                 * what kind of value this is, and a field that accepts
                 * anything for a key that accepts colours is a typo waiting to
                 * be saved.
                 *
                 * It picks in HSL16 rather than RGB because that is what a
                 * model face is actually coloured in -- the plugin api's own
                 * hsl_from_rgb exists because every colour handed to the
                 * engine is quantised onto that palette. Choosing on those
                 * axes means the value in the field is the value that will be
                 * drawn, instead of one that quietly becomes a neighbour of
                 * itself somewhere downstream.
                 *
                 * The hex stays typeable, so a colour out of a wiki page or
                 * another client still arrives the way it always did.
                 */
                widget = ToriRSChrome_ColorPick(
                    &app->plugin_ui,
                    app->plugin_panel,
                    item->label,
                    app_plugin_color_hsl(app_plugin_panel_value(app, p, item->key)));
            }
            else if( item->type == TORIRS_PLUGIN_CFG_TEXT )
            {
                /*
                 * A declared LIST is a multiline box, for the same reason a
                 * declared enum is a real dropdown: the schema already says
                 * what shape this value has.
                 *
                 * The one that made this necessary is the ground-items
                 * highlight and hide lists -- comma-separated runs of item
                 * names that the user is expected to maintain. In a 60px
                 * one-line field about a word and a half of that is on screen
                 * at a time, so changing one entry means arrowing sideways
                 * through the rest. The reference client gives exactly these
                 * two lists a box several lines tall (interface 650), and this
                 * is the schema saying so.
                 */
                widget = ToriRSChrome_TextArea(
                    &app->plugin_ui,
                    app->plugin_panel,
                    item->label,
                    app_plugin_panel_value(app, p, item->key),
                    item->rows);
            }
            else
            {
                /* Ints and strings edit as text, because the store is textual
                 * to begin with -- the field shows the value that would be
                 * written, not a rendering of it. */
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
            int save_widget;
            ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);
            /* The only way to commit this page, now that the title bar's Ok is
             * gone: closing DISCARDS, which is what closing a form means. */
            save_widget = ToriRSChrome_Button(&app->plugin_ui, app->plugin_panel, "Save");
            app_plugin_panel_track(
                app,
                save_widget,
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

    /*
     * The window's own way out: the interfaces' window X in the title bar.
     *
     * Opt-in per panel -- the developer tools beside this one are toggled by
     * hotkeys and do not want a close box. Set on every rebuild rather than at
     * PanelAdd because the panel is cleared and rebuilt per page, and the flag
     * has to survive that.
     */
    ToriRSChrome_PanelSetClosable(&app->plugin_ui, app->plugin_panel, 1);

    app->plugin_panel_built_for = count;
    app->plugin_panel_built_rev = rev;
    g_plugin_page_built = g_plugin_page;
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
    /* The page's own navigation first: Back belongs to no plugin, so it is not
     * in the row table the loop below walks. */
    if( widget >= 0 && widget == g_plugin_back_widget )
    {
        g_plugin_page = -1;
        return;
    }

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

            /*
             * A roster row has two outcomes and the chrome says which one
             * fired: the ACTION zone opens this plugin's page, the switch
             * enables it. Asked of the model rather than inferred from the
             * checked state, because "open" must not depend on whether the
             * plugin happens to be on.
             */
            if( ToriRSChrome_ActivationWasAction(&app->plugin_ui) )
            {
                g_plugin_page = row->plugin;
                return;
            }
            /* Cleared BEFORE the start and not after: whatever it said was
             * about the run that just ended, and a plugin that looks at the
             * lane and stands down again writes its reason from inside the
             * start below -- clearing afterwards would wipe the one line that
             * explains why the switch bounced back. */
            if( on )
                PluginHost_SetError(app->plugins, row->plugin, NULL);
            PluginHost_SetEnabled(app->plugins, row->plugin, on != 0);
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



/* ---- the "Manage Plugins" button -------------------------------------------
 *
 * The one way into the plugin window, and it is the same control on every
 * gameframe: a wide stone plate at the bottom of the LOGOUT tab reading
 * "Manage Plugins", beside the client-owned things a player already expects to
 * find there.
 *
 * WHY THE CLIENT ADDS IT, not the content. Plugins are a CLIENT feature; the
 * same client pointed at any other server must still have them, so a button
 * the cache ships and the server arms is the one shape this cannot take. The
 * client appends its own and answers its own click, and the cache is untouched.
 *
 * WHY IT IS BUILT HERE ONLY ON SOME LANES. A profile that authors the whole
 * gameframe simply authors the button too -- `[component:manage_plugins_*]`
 * plus a layout entry inside the logout tab, which is what every dat1 lane
 * does (revconfig/rs245_2lc, read by 254, 289 and 377). Those lanes state no
 * `[chrome]` block and nothing below this line runs for them. This is for the
 * lanes whose frame comes out of a CS2 toplevel and has no authored records to
 * add one to.
 *
 * WHERE IT GOES IS THE PROFILE'S, not this file's. Which interface the logout
 * tab is, which of its children the plate hangs off, and where in that child it
 * sits -- every one of those is a fact about a revision. They are the RevConfig
 * `[chrome]` block; see struct RevConfigChromeItem.
 *
 * THE ART IS THE CHROME'S, not the cache's -- the same reason the authored
 * version uses `sprite=chrome:*`. This button opens the client's own furniture,
 * so it cannot be drawn with art that only some caches contain. The three
 * baked slots are the interfaces' own wide stone button (two 36px caps and a
 * 20px tile between them) and the caption is set in the baked bold face, so the
 * plate draws identically on a cache that carries neither.
 *
 * Everything the button needs is on screen only once the server has mounted
 * that interface, which is why the build below waits for the parent to appear.
 */

/** The `[chrome]` block of the running revision's profile. */
#define APP_PLUGIN_CHROME (&app->revconfig_profile.chrome)

/*
 * The plate is three overlapping graphics, at the offsets the cache's own
 * button uses: the tile is laid OVER each cap's inner edge, not beside it.
 * Butting them edge to edge instead leaves the bevels showing as two seams
 * across the plate. Derived from the stated width so a profile can make the
 * button wider without the pieces coming apart.
 */
#define APP_PLUGIN_BTN_CAP 36
#define APP_PLUGIN_BTN_MID_INSET 26

/** The caption, which is also the name of the thing it opens. */
#define APP_PLUGIN_BUTTON_TEXT "Manage Plugins"

/**
 * Is this revision's plugin-button mount fully stated?
 *
 * All of it or none of it. The mount is ONE description -- an interface, one of
 * its children, and a box inside that child -- and half of it is not half a
 * button: a `[chrome]` block that names the interface but forgets the height
 * puts a zero-tall plate in the logout tab, which reads as a missing button and
 * is far harder to find than an absent one.
 *
 * Saying nothing at all is the ordinary case (an authored lane puts the button
 * in its own records), so that is silent. A HALF-stated block is a mistake in
 * the profile, and gets one line on stderr.
 */
static int
app_plugin_button_declared(struct App const* app)
{
    struct RevConfigChromeItem const* chrome;
    int stated;
    int complete;

    assert(app);
    chrome = &app->revconfig_profile.chrome;
    stated = chrome->plugin_iface[0] != '\0' || chrome->plugin_button_parent >= 0 ||
             chrome->plugin_button_x >= 0 || chrome->plugin_button_y >= 0 ||
             chrome->plugin_button_w >= 0 || chrome->plugin_button_h >= 0;
    complete = chrome->plugin_iface[0] != '\0' && chrome->plugin_button_parent >= 0 &&
               chrome->plugin_button_x >= 0 && chrome->plugin_button_y >= 0 &&
               chrome->plugin_button_w > 0 && chrome->plugin_button_h > 0;
    if( stated && !complete )
    {
        static int complained = 0;
        if( !complained )
        {
            complained = 1;
            fprintf(
                stderr,
                "chrome: [chrome] states only part of the plugin button mount "
                "(plugin_button_iface, plugin_button_parent, plugin_button_x, "
                "plugin_button_y, plugin_button_w, plugin_button_h); "
                "no Manage Plugins button is built\n");
        }
    }
    return complete;
}

/**
 * Packed uid of the declared parent component, or -1.
 *
 * -1 for an undeclared or half-declared mount as well as for an interface this
 * cache does not have, because the callers all ask the same question of it --
 * "is there somewhere to mount?" -- and one answer for "no" is what keeps them
 * from disagreeing about it.
 */
static int
app_plugin_button_mount_com(struct App const* app)
{
    assert(app);
    if( !app_plugin_button_declared(app) )
        return -1;
    return app_iface_com(
        app,
        app->revconfig_profile.chrome.plugin_iface,
        app->revconfig_profile.chrome.plugin_button_parent);
}

/*
 * Can the plugin system still be reached at all?
 *
 * The whole plugin lane -- the manifest, every script it names, every shipped
 * asset -- rides the same transport a cache read does (TORIRS_IOK_SCRIPT, see
 * task_plugin_io.c). When that transport is down the panel can list nothing,
 * load nothing and save nothing, so the ways INTO it are switched off rather
 * than left to open an empty window.
 *
 * Asked of the platform every time rather than latched, because it recovers:
 * the operator restarts the io server, the next batch lands, and the launcher
 * has to come back on its own. A latched flag would need a second mechanism to
 * clear it, and the platform already tracks exactly this.
 *
 * Every desktop lane answers "reachable" always -- there is no server between
 * the client and its disk -- so nothing below this line ever fires there. See
 * Platform_IO_ServerReachable.
 */
static int
app_plugin_io_down(struct App const* app)
{
    assert(app);
    if( !app->runner.px )
        return 0;
    return !Platform_IO_ServerReachable(app->runner.px);
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

/** One piece of the plate: a graphic holding one baked skin slot, no ops. */
static void
app_plugin_button_piece(
    struct App* app,
    int32_t parent,
    int id,
    int skin,
    int slot,
    int x,
    int w,
    int h,
    int tiled)
{
    struct UIBuildComponent comp;

    memset(&comp, 0, sizeof(comp));
    comp.id = id;
    comp.type = UIBUILD_GRAPHIC;
    comp.parent_id = -1;
    comp.base_x = x;
    comp.base_y = 0;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_scene_id = skin;
    comp.graphic_atlas_index = slot;
    comp.graphic_active = -1;
    /* `tiled` repeats the 20px middle across the box rather than blitting it
     * once, which is how the cache's own button is authored; without it the
     * plate is two caps with a hole in it. */
    comp.tiled = (uint8_t)tiled;
    /* -1, not the memset's 0: zero is a component id, so leaving it there
     * makes the hover walk report component 0 as hovered whenever the pointer
     * is on this piece. */
    comp.over_layer_id = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    UITree_PushBuildComponent(app->tree, parent, &comp, NULL, NULL, app);
}

/* TEMP DEBUG PROBE -- remove. Dumps the logout interface's live boxes. */
static void
app_plugin_button_probe(struct App* app)
{
    static int last = -1;
    int iface;
    int frame;
    if( !getenv("TORIRS_PLUGIN_BTN_PROBE") || !app->tree )
        return;
    frame = (int)(g_plugin_panel_ticks / 20);
    if( frame == last )
        return;
    last = frame;
    iface = RevConfigRefs_Get(&app->revconfig_refs, "iface",
        app->revconfig_profile.chrome.plugin_iface);
    fprintf(stderr, "PROBE tick=%d iface=%d layout=%d wmode=%d canvas=%dx%d\n",
        g_plugin_panel_ticks, iface, app->host.client_layout_mode,
        app->host.window_mode, app->host.viewport_w, app->host.viewport_h);
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &app->tree->components[i];
        if( c->freed || ((c->component_id >> 16) & 0xFFFF) != iface )
            continue;
        fprintf(stderr, "PROBE   com=%d|%d type=%d hide=%d abs=%d,%d %dx%d parent=%d text='%s'\n",
            (c->component_id >> 16) & 0xFFFF, c->component_id & 0xFFFF,
            (int)c->type, (int)c->behavior.hide,
            c->position.abs_x, c->position.abs_y, c->position.abs_w, c->position.abs_h,
            c->parent,
            c->type == UIELEM_RS_TEXT && c->u.rs_text.text ? c->u.rs_text.text : "");
    }
}

static void
app_plugin_button_sync(struct App* app)
{
    struct RevConfigChromeItem const* chrome;
    struct UIBuildComponent comp;
    int32_t mount;
    int32_t node;
    int skin;
    int w;
    int h;
    int const down = app_plugin_io_down(app);

    app_plugin_button_probe(app);
    if( !app->tree || !ToriRSChrome_TreeAcceptsChrome(app->tree) )
        return;

    /*
     * A launcher already in the tree follows the transport up and down.
     *
     * HIDDEN, not merely unclickable. The plate has no greyed variant, so a
     * disabled button that still looks exactly like a live one is a button
     * people press and get nothing from. Its menu row is suppressed
     * independently (RS_MinimenuBuildCtx::plugin_io_down), so neither the click
     * nor the right-click menu can reach the panel while it is gone.
     *
     * On the CONTAINER only: the three graphics and the label hang off it, and
     * a hidden ancestor already keeps a subtree off the screen. The click mask
     * goes with it, rather than relying on a hidden component being unhittable:
     * the two are separate pieces of state and this does not need to know how
     * the hit walk treats the first.
     */
    if( app_plugin_button_alive(app) )
    {
        if( down != app->plugin_button_disabled )
        {
            UITree_ApplyHide(app->tree, TORIRS_CHROME_PLUGIN_BUTTON_ID, down);
            UITree_ApplyClickMask(app->tree, TORIRS_CHROME_PLUGIN_BUTTON_ID, !down);
            app->plugin_button_disabled = down;
            UITree_MarkAllDirty(app->tree);
        }
        return;
    }

    /*
     * Not built at all while the lane is down, so a gameframe that mounts its
     * logout tab during an outage does not get a launcher that has to be hidden
     * a frame later. This runs every frame, so it is built the moment the
     * server answers again.
     */
    if( down )
        return;

    /*
     * Only once the gameframe has mounted the interface the profile named.
     * Nothing offline can say when that is -- the server mounts the logout
     * panel at login -- so the presence of the component IS the readiness test.
     *
     * A lane that states no `[chrome]` block therefore builds nothing, and
     * opens the window from the `plugin_panel_toggle` action a manifest binds
     * or from the button its own profile authored.
     */
    mount = app_plugin_button_mount_com(app);
    if( mount < 0 )
        return;
    mount = UITree_FindByComponentId(app->tree, mount);
    if( mount < 0 )
        return;

    /* The plate and the caption both come from the BAKED chrome, so there is
     * nothing to wait for and nothing to get wrong -- no archive has to land
     * first and no cache has to contain the art. */
    skin = UITreeSceneBridge_EnsureChromeSkin(&app->bridge);
    if( skin <= 0 )
        return;

    chrome = APP_PLUGIN_CHROME;
    w = chrome->plugin_button_w;
    h = chrome->plugin_button_h;

    /*
     * The container. It carries the menu row and nothing else -- the pieces
     * under it are decoration and deliberately carry no ops, because two
     * op-bearing nodes under one pointer is two rows saying the same thing.
     */
    memset(&comp, 0, sizeof(comp));
    comp.id = TORIRS_CHROME_PLUGIN_BUTTON_ID;
    comp.type = UIBUILD_LAYER;
    comp.parent_id = -1;
    comp.base_x = chrome->plugin_button_x;
    comp.base_y = chrome->plugin_button_y;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_active = -1;
    comp.over_layer_id = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    /* Op 1, so the hover line names it. The wording is `[chrome]
     * plugin_button_op=`, because it is text a player reads. */
    if( chrome->plugin_button_op[0] )
        snprintf(comp.ops[0], sizeof(comp.ops[0]), "%s", chrome->plugin_button_op);
    node = UITree_PushBuildComponent(app->tree, mount, &comp, NULL, NULL, app);
    if( node < 0 )
        return;

    app_plugin_button_piece(
        app, node, TORIRS_CHROME_PLUGIN_CAP_LEFT_ID, skin, TORIRS_CHROME_SKIN_BUTTON_LEFT,
        0, APP_PLUGIN_BTN_CAP, h, 0);
    app_plugin_button_piece(
        app, node, TORIRS_CHROME_PLUGIN_CAP_MID_ID, skin, TORIRS_CHROME_SKIN_BUTTON_MID,
        APP_PLUGIN_BTN_MID_INSET, w - 2 * APP_PLUGIN_BTN_MID_INSET, h, 1);
    app_plugin_button_piece(
        app, node, TORIRS_CHROME_PLUGIN_CAP_RIGHT_ID, skin, TORIRS_CHROME_SKIN_BUTTON_RIGHT,
        w - APP_PLUGIN_BTN_CAP, APP_PLUGIN_BTN_CAP, h, 0);

    /*
     * The caption, over the three graphics and the full size of the plate.
     *
     * Centred against the WHOLE button, not against a hand-placed line box:
     * ascent and descent differ per face, so a 13px box positioned by eye sits
     * right in one font and low in the next. The cache's own captions are
     * authored exactly this way -- full height, halign 1, valign 1.
     *
     * The face is the baked bold one (the chrome's own 496), pinned at 1x:
     * these glyphs land in INTERFACE pixels, which the gameframe lays out and
     * the shell scales afterwards, so a chrome-scale face would come out
     * double-sized on any HighDPI display.
     *
     * Red under the pointer, which is what the logout button above it does.
     */
    memset(&comp, 0, sizeof(comp));
    comp.id = TORIRS_CHROME_PLUGIN_LABEL_ID;
    comp.type = UIBUILD_TEXT;
    comp.parent_id = -1;
    comp.base_x = 0;
    comp.base_y = 0;
    comp.base_width = w;
    comp.base_height = h;
    comp.if3 = 1;
    comp.graphic = -1;
    comp.graphic_active = -1;
    comp.over_layer_id = -1;
    comp.model_active_id = -1;
    comp.model_seq_id = -1;
    comp.text = APP_PLUGIN_BUTTON_TEXT;
    comp.font_id = UITreeSceneBridge_EnsureDebugFont1x(&app->bridge, TORIRS_CHROME_FONT_MENU);
    comp.color = 0xF7F0DF;
    comp.over_color = 0xFF0000;
    comp.text_h_align = 1;
    comp.text_v_align = 1;
    comp.shadowed = 1;
    UITree_PushBuildComponent(app->tree, node, &comp, NULL, NULL, app);

    app->plugin_button_node = node;
    /* Freshly built means freshly enabled -- `down` is false to have got here.
     * Recording it is what keeps the edge test above from reading a stale
     * "disabled" left over from the last gameframe's button. */
    app->plugin_button_disabled = 0;
    UITree_ApplyClickMask(app->tree, TORIRS_CHROME_PLUGIN_BUTTON_ID, 1);
    UITree_MarkAllDirty(app->tree);
}


static void
app_plugin_exec_bind(struct App* app);

static void
app_plugin_window_set_open(struct App* app, int open)
{
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome: plugin window set_open(%d) tick=%d\n", open ? 1 : 0,
            g_plugin_panel_ticks);
    app->plugin_panel_visible = open ? 1 : 0;
    if( app->plugin_panel >= 0 )
        ToriRSChrome_PanelSetVisible(
            &app->plugin_ui, app->plugin_panel, app->plugin_panel_visible);

    /*
     * Lazy, and lazy in the way that matters: this runs on the frame the
     * window opens, so a session that never opens it never binds and never
     * opens a second OS window.
     */
    if( app->plugin_panel_visible )
        app_plugin_exec_bind(app);
    else
        /*
         * The other half of that bind, and the reason it can be lazy.
         *
         * A presentation is not merely where the panel is drawn: for every
         * executor but the in-canvas one it is a WINDOW -- an OS window, a
         * page's container -- and hiding the panel inside one leaves the window
         * itself standing, empty, while the client insists it is closed. That
         * was exactly the report: Close dismissed the panel and left the
         * plugin window on screen.
         *
         * So the window closes the way it opened: through the executor's own
         * end(). Which one is bound decides what that means -- SDL destroys
         * its aux window, GDI its HWND, the web one calls the page's close
         * hook -- and the host needs to know none of it. The next open re-binds
         * from scratch, which is also what makes a window taken down by ITS
         * side (a title-bar X) openable again: `live` is the guard the bind
         * reads.
         */
        ToriRSChromeSync_Shutdown(&app->plugin_exec);
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
app_plugin_exec_bind_inner(struct App* app)
{
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

static void
app_plugin_exec_bind(struct App* app)
{
    if( app->plugin_exec.live )
        return;
    app_plugin_exec_bind_inner(app);
    /*
     * What actually got bound, which is not always what was asked for: an
     * executor can refuse to start. Wrapped rather than printed inside,
     * because the selection has several exits and a line reached by only some
     * of them reports the absence of a decision as the absence of a bind.
     *
     * Once per ANSWER, not once per open: closing the window takes the
     * executor down with it, so every show re-binds, and a line per bind would
     * make a session that toggles the window a session that logs the same
     * sentence twenty times.
     */
    if( app->plugin_exec_kind != app->plugin_exec_logged_kind )
    {
        app->plugin_exec_logged_kind = app->plugin_exec_kind;
        fprintf(
            stderr,
            "chrome: plugin window executor = %s (%s)\n",
            ToriRSChromeExec_KindName(app->plugin_exec_kind),
            app->plugin_exec_explicit ? "configured" : "default");
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

    g_plugin_panel_ticks++;
    if( !app->plugins )
        return;

    /* The button is kept alive whether or not the window is open -- it is how
     * the window is OPENED, so it cannot be gated on the window being up. */
    app_plugin_button_sync(app);

    /* Same suppression as every other chrome toggle: a focused chat line must
     * not flip windows.
     *
     * Latched on the key's own down/held STATE, not on the per-frame edge the
     * other debug hotkeys use. This tick runs before App_RunOnce's early-outs
     * -- deliberately, so the window works while a cache loads -- which means
     * a frame withheld by the UI-transaction latch re-runs it against the SAME
     * input frame, edge included. An edge-triggered toggle then fires once per
     * re-run, and the window flaps open/shut for as long as the turbulence
     * lasts. */
    {
        static int toggle_was_down = 0;
        int const down =
            !app_text_input_focused(app) &&
            (app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PLUGIN_PANEL) ||
             app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PLUGIN_PANEL));
        if( down && !toggle_was_down )
        {
            /* The same path the button takes. Two entry points that opened
             * the window two different ways is how one of them ends up doing
             * only half of it. */
            app_plugin_window_set_open(app, !app->plugin_panel_visible);
        }
        toggle_was_down = down;
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
            if( gesture.edit_key != TORIRS_CHROME_KEY_NONE )
                ToriRSChrome_KeyEdit(&app->plugin_ui, gesture.edit_key);
        }
    }
    else if( ToriRSChrome_HasVisiblePanel(&app->plugin_ui) )
    {
        /* Its own instance, so its own routing -- and no ownership dance with
         * the developer chrome's activation latch, because it no longer shares
         * one. That shared latch is what made the old panel peek before taking,
         * and two panels could still eat each other's clicks.
         *
         * A native-widget executor (cs2) gets the KEYBOARD only: its clicks
         * arrive as component clicks through the interface tree, and routing
         * the mouse here as well would hand them to the in-canvas window's
         * ghost -- laid out and hit-testable at its floating position even
         * though nothing draws it. The keyboard must still flow, because the
         * text fields being typed into are the model's. */
        if( app->plugin_exec_kind == TORIRS_CHROME_EXEC_BUFFER )
            app_chrome_route_input(app, &app->plugin_ui, input);
        else
            app_chrome_route_keys(app, &app->plugin_ui, input);
    }

    /*
     * A presentation with a window of its own gets the panel STRETCHED over
     * it: there is nothing else in that window for a floating box to float
     * over, so its margins would just be background, and the window growing
     * would grow the background rather than the settings.
     *
     * After the input above, not before it: a resize is applied to the surface
     * as it is drained, so asking here reports the size the user just dragged
     * to instead of the one from before it. In-canvas -- and every native
     * executor, which lays its own controls out -- this is a no-op and the
     * panel keeps the floating geometry it was built with.
     */
    if( app->plugin_panel >= 0 )
        ToriRSChromeSync_FillSurface(&app->plugin_exec, &app->plugin_ui, app->plugin_panel);

    /* Intents from a native-widget executor land on the model the same way a
     * click would, so the drain below sees both without knowing which. */
    ToriRSChromeSync_Pump(&app->plugin_exec, &app->plugin_ui);

    {
        int const activated = ToriRSChrome_TakeActivated(&app->plugin_ui);
        if( activated >= 0 )
            app_plugin_panel_apply(app, activated);
    }

    /*
     * The window's own Ok or Close hid the panel; the host has to hear about
     * it.
     *
     * The MODEL is what a close acts on -- CLOSE and CONFIRM both end in
     * PanelSetVisible(0), which is what keeps every presentation agreeing --
     * and this flag is the host's separate idea of whether the window is up.
     * Left unreconciled they disagree, and the sidebar button's next press
     * "closes" an already-closed window instead of reopening it. Read after
     * the drain so an Ok commits its page before the window is torn down.
     */
    if( app->plugin_panel_visible && app->plugin_panel >= 0 &&
        !app->plugin_ui.panels[app->plugin_panel].visible )
        app_plugin_window_set_open(app, 0);

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
     * Where the window may be grabbed, for a presentation that took its OS
     * frame off and now has to answer for what the title bar used to do.
     *
     * AFTER the build, unlike the fill above, and the order is the whole point:
     * these are laid-out boxes. Publishing them before the build that produced
     * them is a frame of lag on every resize -- a drag band sitting where the
     * panel was a moment ago, which on a window the user is actively dragging
     * wider is a band that is never where it looks.
     *
     * Every frame and unconditionally: an executor whose window kept its frame
     * has no set_drag_region and this costs a null test, and one that HAS a
     * frameless window must hear about an empty region as clearly as a full
     * one -- a panel that shrank has to stop swallowing presses over where
     * it used to be.
     */
    if( app->plugin_panel >= 0 )
        ToriRSChromeSync_PublishDragRegion(
            &app->plugin_exec, &app->plugin_ui, app->plugin_panel);

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
        struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&app->plugin_ui, &count);
        app->plugin_exec.exec.present(app->plugin_exec.exec.user, prims, count);
    }
}
