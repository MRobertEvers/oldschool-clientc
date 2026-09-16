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

#include "plugin/torirs_plugin_panel_route.h"

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
int g_plugin_page = -1;
/** The page the widgets on screen were built for; a mismatch rebuilds. */
int g_plugin_page_built = -1;
/**
 * The page the scroll offset belongs to.
 *
 * Separate from `g_plugin_page_built`, which is about the WIDGETS: the scroll
 * survives a rebuild of the same page and must not survive a move to another
 * one, and those two questions are answered at different moments. Carrying a
 * place across a page change would drop the reader half way down a list they
 * had not opened yet.
 */
static int g_plugin_page_scrolled = -1;
/** Handle of the page's Back button, or -1 on the roster. It belongs to no
 *  plugin, so it is remembered here rather than tracked as a row. */
static int g_plugin_back_widget = -1;

/*
 * WHICH of a plugin's two faces the page is showing.
 *
 * A plugin that registers a panel has two things a person might have come for
 * and they are not the same thing: what it is SAYING right now -- the loot it
 * has recorded, the xp it has watched -- and how it is CONFIGURED. Showing
 * both stacked on one page made the second answer arrive by scrolling past the
 * first, and made the roster's row and the rail's icon lead to identical
 * screens, so neither destination meant anything.
 *
 * So the ENTRY POINT chooses, because the entry point is where the intent
 * already is: the rail is the plugin's own stone and opens what it has to say;
 * the roster is a list of settings pages and opens the settings. A plugin
 * with no panel of its own has only the second face and gets it either way.
 */
enum AppPluginPageView
{
    /** The plugin's declared semantic page -- its active screen. */
    APP_PLUGIN_VIEW_PAGE = 0,
    /** The generated settings form plus any win_* controls. */
    APP_PLUGIN_VIEW_SETTINGS
};

static int g_plugin_page_view = APP_PLUGIN_VIEW_SETTINGS;
/** The view the widgets on screen were built for; a mismatch rebuilds. */
static int g_plugin_page_view_built = -1;

/* Defined beside the custom presenter below; panel sync may run before a host
 * exists and must still retire any retained pixels from the prior host. */
static void
app_plugin_panel_overlay_reset(struct App* app, uint32_t generation);

/* Defined beside the rail below; opening the window publishes the rail before
 * the executor comes up, so begin() sizes the page for the entry being shown. */
static void
app_plugin_rail_publish(struct App* app);

/* plugin/torirs_plugin_popout_nav.u.c, included after this file. */
static void
app_plugin_popout_nav_tick(struct App* app);

/* Change the ONE page selection and its generation together. A platform event
 * queued by the page being left is stale from this statement onward. */
static void
app_plugin_page_select(struct App* app, int page, int view)
{
    assert(app);

    /*
     * The SETTINGS view of a plugin that has a panel is still a page with no
     * panel selection: the host builds a model only for the selected plugin,
     * so asking for the settings must not also run its page build. Closing is
     * what keeps "opened from the roster" free of the page's per-frame work.
     */
    if( page < 0 )
        view = APP_PLUGIN_VIEW_SETTINGS;
    g_plugin_page_view = view;

    /* The roster is also the portable rail until a platform exposes distinct
     * icons. Selecting a registered entry goes through the host transaction:
     * that is the boundary which unmounts the previous model before building
     * this one. Host-generated schema-only pages still use the same detail
     * view without fabricating a panel selection for a plugin that did not
     * register one. */
    if( app->plugins )
    {
        if( page >= 0 && PluginHost_PanelHasPage(app->plugins, page) )
        {
            /*
             * BOTH faces are mountings, so both go through the host -- the
             * settings face is not "no selection". A plugin that wants to add
             * its own controls to its settings form declares them from the
             * same on_ui_build, told which face it is answering.
             * @see enum ToriRS_PanelView.
             */
            int const want = view == APP_PLUGIN_VIEW_PAGE
                                 ? TORIRS_PANEL_VIEW_PAGE
                                 : TORIRS_PANEL_VIEW_SETTINGS;
            if( PluginHost_PanelActive(app->plugins) != page ||
                PluginHost_PanelView(app->plugins) != want )
                (void)PluginHost_PanelSelectView(app->plugins, page, want);
            /* One application pane has one allocation. A plugin descriptor's
             * preferred width is presentation metadata, not permission to
             * resize the game every time the player changes rail tabs. */
            ToriRSChromeShell_SetPanelWidth(
                &app->plugin_shell,
                TORIRS_PANEL_WIDTH_DEFAULT,
                TORIRS_PANEL_WIDTH_DEFAULT,
                TORIRS_PANEL_WIDTH_DEFAULT);
        }
        else if( PluginHost_PanelActive(app->plugins) >= 0 )
            (void)PluginHost_PanelClose(app->plugins);
    }
    /*
     * The page IDENTITY moved, so whatever the executor is holding is about to
     * be discarded and the shadow that describes it is now a description of
     * nothing.
     *
     * A page-retaining executor drops its DOM when it sees the new generation
     * -- it must, because a delta authored for one page cannot patch another's
     * -- and without this the next Run would emit only the DIFFERENCE between
     * the old page and the new one. Two plugin pages built from the same
     * vocabulary are routinely alike row for row, and that difference then
     * carries no WIDGET_ADD at all: the executor mounts a "snapshot" of three
     * label changes and shows an empty pane. Switching straight from one
     * plugin's page to another's is exactly the gesture that produces it.
     *
     * Unconditional, because every exit from this function is a page boundary
     * -- roster to page, page to roster, one plugin to another, and one face
     * of a plugin to its other one. Restating a page costs one transaction on
     * a gesture a person just made.
     */
    if( app->plugin_exec.live )
        ToriRSChromeSync_Invalidate(
            &app->plugin_exec, PluginHost_PanelSelectionGeneration(app->plugins));

    g_plugin_page = page;
    /* The shell stores a RAIL destination, not merely the plugin whose data
     * backs the page. A generated settings face belongs to Manage even though
     * `g_plugin_page` still names the plugin being configured. Keeping the
     * plugin index here made its rail stone look selected and behave like a
     * close toggle while the settings face was visible, so clicking XP never
     * navigated to the tracker. */
    ToriRSChromeShell_Select(
        &app->plugin_shell,
        page >= 0 && view == APP_PLUGIN_VIEW_PAGE
            ? page
            : TORIRS_CHROME_SHELL_PAGE_MANAGE);
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome: page_select page=%d view=%s -> host active=%d view=%d has_page=%d\n",
            page, view == APP_PLUGIN_VIEW_PAGE ? "page" : "settings",
            app->plugins ? PluginHost_PanelActive(app->plugins) : -2,
            app->plugins ? PluginHost_PanelView(app->plugins) : -2,
            app->plugins && page >= 0 ? (int)PluginHost_PanelHasPage(app->plugins, page) : -2);
}

/*
 * The window is filling the canvas.
 *
 * A phone is the reason this exists. The floating geometry below is authored
 * against a desktop window, where a settings box that leaves the game visible
 * behind it is the right shape; on a 765x503 canvas scaled onto a handset the
 * same box is a stamp in the corner with rows too small to hit. Fullscreen is
 * the same panel with the canvas for its box -- no second layout, no second
 * widget set, and the rows grow with the panel because they always did.
 */
int g_plugin_fullscreen;
/** The state the current display list was built for, so a toggle rebuilds. */
int g_plugin_fullscreen_built = -1;
/*
 * The scale and canvas the panel's BOX was last placed against.
 *
 * The box is a function of both -- 8*scale/320*scale windowed, the whole
 * canvas fullscreen -- and neither is a term the build gate below tests, so a
 * scale or interface-scaling change while the window was open used to leave
 * the old box standing. That is not cosmetic: the close X hangs off the
 * panel's RIGHT edge, and the platform clamps the pointer into the canvas, so
 * a box wider than the shrunken canvas is a window that can never be closed
 * (and one broad enough to swallow the Manage Plugins button under it, which
 * was the other way out). Tracked separately from the build gate so a resize
 * re-places the box WITHOUT tearing down the rows -- the gate exists so a
 * field being typed into is not rebuilt under the caret, and a resize is not
 * a reason to lose that.
 */
static int g_plugin_geom_scale_built = -1;
static int g_plugin_geom_canvas_w_built = -1;
static int g_plugin_geom_canvas_h_built = -1;
/** Handle of the Fullscreen/Windowed row, or -1. Belongs to no plugin, so it
 *  is remembered here rather than tracked as one of the panel's rows. */
static int g_plugin_fullscreen_widget = -1;

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

    if( PluginHost_PanelHasPage(app->plugins, plugin) )
        return 1;
    for( int c = 0; c < cfg_count; c++ )
    {
        struct ToriRS_ConfigItem const* item =
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
    memset(row, 0, sizeof(*row));
    row->widget = widget;
    row->plugin = plugin;
    row->kind = kind;
    row->cfg_index = cfg_index;
    snprintf(row->widget_id, sizeof(row->widget_id), "%s", widget_id ? widget_id : "");
    row->widget_serial = 0;
    row->widget_kind = -1;
}

/** Track one control mirrored from the generation-scoped ABI-21 panel model. */
static void
app_plugin_panel_track_semantic(
    struct App* app,
    int widget,
    int plugin,
    int model_index,
    struct ToriRS_PanelWidget const* model)
{
    struct AppPluginPanelRow* row;
    int row_index;

    assert(app);
    if( widget < 0 || !model || model->serial == 0 || model_index < 0 ||
        model_index >= TORIRS_PLUGIN_WIDGETS_MAX ||
        app->plugin_panel_row_count >= APP_PLUGIN_PANEL_ROWS_MAX )
        return;

    row_index = app->plugin_panel_row_count++;
    app->plugin_panel_model_rows[model_index] = row_index + 1;
    row = &app->plugin_panel_rows[row_index];
    memset(row, 0, sizeof(*row));
    row->widget = widget;
    row->plugin = plugin;
    row->kind = APP_PLUGIN_ROW_PANEL_WIDGET;
    row->cfg_index = -1;
    row->model_index = model_index;
    row->widget_serial = model->serial;
    row->widget_kind = model->kind;
    snprintf(row->widget_id, sizeof(row->widget_id), "%s", model->id);
    if( model->kind == TORIRS_PANEL_WIDGET_CUSTOM &&
        app->plugin_panel_custom_row_count < TORIRS_PLUGIN_WIDGETS_MAX )
        app->plugin_panel_custom_rows[app->plugin_panel_custom_row_count++] =
            row_index;
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
    struct ToriRS_ConfigItem const* item;

    if( row->kind != APP_PLUGIN_ROW_CONFIG )
        return;
    item = PluginHost_ConfigItem(app->plugins, row->plugin, row->cfg_index);
    if( !item )
        return;
    if( item->type == TORIRS_CONFIG_BOOL )
    {
        ToriRSChrome_SetChecked(
            &app->plugin_ui,
            row->widget,
            atoi(app_plugin_panel_value(app, row->plugin, item->key)) != 0);
    }
    else if( item->type == TORIRS_CONFIG_ENUM &&
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
    else if( item->type == TORIRS_CONFIG_COLOR )
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
/*
 * Place the window's box for the CURRENT scale and canvas, and remember which
 * ones it was placed for. @see g_plugin_geom_scale_built.
 *
 * Fullscreen is not a second panel: it is this one with the CANVAS for its
 * box. Stating both cases here, together, is what keeps them the same window
 * -- a fullscreen path that built its own panel would have its own scroll
 * position, its own focus and its own rows to keep in step, and the toggle
 * would lose the page you were on every time you pressed it.
 *
 * The canvas and not the display: the panel is drawn into the game's own
 * 765x503 (or whatever the profile is) and scaled to the screen with it, so a
 * window that fills the canvas fills the screen, and one measured in device
 * pixels would hang off the side of a scaled canvas.
 *
 * The windowed box is CLAMPED into the canvas for the reason the geometry is
 * re-applied at all: the close X rides the panel's right edge and the pointer
 * cannot leave the canvas, so any part of the box past the edge is a control
 * that cannot be pressed.
 */
static void
app_plugin_panel_place(struct App* app)
{
    int const scale = ToriRSChrome_Scale(&app->plugin_ui);

    assert(app);
    assert(app->plugin_panel >= 0);

    /*
     * The same canvas the box below is clamped into, told to the chrome, so
     * that an open dropdown list folds up into it as well.
     *
     * The list is not a panel and nothing places it: it hangs off the row that
     * opened it and is meant to escape the 320-wide window. It is not meant to
     * escape the CANVAS -- past that edge the pointer cannot follow it -- and
     * at fullscreen the window's last rows sit on that edge.
     */
    ToriRSChrome_SetSurface(&app->plugin_ui, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    if( g_plugin_fullscreen )
    {
        ToriRSChrome_PanelMove(&app->plugin_ui, app->plugin_panel, 0, 0);
        ToriRSChrome_PanelSetFixedWidth(&app->plugin_ui, app->plugin_panel, UITREE_LAYOUT_ROOT_W);
        ToriRSChrome_PanelSetFixedHeight(
            &app->plugin_ui, app->plugin_panel, UITREE_LAYOUT_ROOT_H);
    }
    else
    {
        int x = 8 * scale;
        int y = 72 * scale;
        int const logical_w = TORIRS_PANEL_WIDTH_DEFAULT;
        int w;
        int h = 260 * scale;
        w = logical_w * scale;

        if( w > UITREE_LAYOUT_ROOT_W )
            w = UITREE_LAYOUT_ROOT_W;
        if( h > UITREE_LAYOUT_ROOT_H )
            h = UITREE_LAYOUT_ROOT_H;
        if( x + w > UITREE_LAYOUT_ROOT_W )
            x = UITREE_LAYOUT_ROOT_W - w;
        if( y + h > UITREE_LAYOUT_ROOT_H )
            y = UITREE_LAYOUT_ROOT_H - h;

        ToriRSChrome_PanelMove(&app->plugin_ui, app->plugin_panel, x, y);
        ToriRSChrome_PanelSetFixedWidth(&app->plugin_ui, app->plugin_panel, w);
        ToriRSChrome_PanelSetFixedHeight(&app->plugin_ui, app->plugin_panel, h);
    }

    g_plugin_geom_scale_built = scale;
    g_plugin_geom_canvas_w_built = UITREE_LAYOUT_ROOT_W;
    g_plugin_geom_canvas_h_built = UITREE_LAYOUT_ROOT_H;
}

/**
 * Spell a semantic readout without losing either its accessible label or its
 * live text. ToriRSChrome copies constructor strings, so this stack buffer is
 * intentionally short-lived.
 */
static void
app_plugin_panel_readout(
    char* out,
    size_t out_size,
    char const* label,
    char const* text,
    char const* empty)
{
    char const* const lhs = label ? label : "";
    char const* const rhs = text ? text : "";

    assert(out);
    assert(out_size > 0);
    if( lhs[0] && rhs[0] )
        snprintf(out, out_size, "%s: %s", lhs, rhs);
    else if( lhs[0] )
        snprintf(out, out_size, "%s", lhs);
    else if( rhs[0] )
        snprintf(out, out_size, "%s", rhs);
    else
        snprintf(out, out_size, "%s", empty ? empty : "");
}

static int
app_plugin_panel_select_inputs(
    struct ToriRS_PanelWidget const* model,
    struct ToriRSChromeSelectOptionInput* out,
    int capacity)
{
    int count;

    assert(model);
    assert(out);
    count = model->select_option_count;
    if( !model->structured_select || count < 0 || count > capacity ||
        (count > 0 && !model->select_options) )
        return -1;
    for( int i = 0; i < count; i++ )
    {
        out[i].value = model->select_options[i].value;
        out[i].label = model->select_options[i].label;
        out[i].enabled = model->select_options[i].enabled ? 1 : 0;
        out[i].detail = model->select_options[i].detail;
    }
    return count;
}

/**
 * Materialize one ABI-21 semantic node as a styled retained-chrome control.
 *
 * This is deliberately a translation, not a second UI toolkit. Every target
 * kind already crosses the internal-canvas/WEB/BROWSER presentation seam,
 * which keeps a plugin ignorant of the platform presenting it. Read-only
 * kinds stay distinct here: the browser must receive enough information to
 * create a heading, paragraph, key/value component, progress meter, or alert,
 * rather than flattening every one to the same text span. CUSTOM remains the
 * explicit low-level drawing well; ordinary bundled pages do not use it.
 */
static int
app_plugin_panel_build_semantic(
    struct App* app,
    struct ToriRS_PanelWidget const* model)
{
    char text[TORIRS_CHROME_INPUT_MAX];
    int widget = -1;

    assert(app);
    assert(model);

    switch( model->kind )
    {
    case TORIRS_PANEL_WIDGET_SECTION:
        app_plugin_panel_readout(
            text, sizeof(text), model->label, model->text, model->id);
        widget = ToriRSChrome_Heading(
            &app->plugin_ui, app->plugin_panel, text);
        break;

    case TORIRS_PANEL_WIDGET_PARAGRAPH:
        app_plugin_panel_readout(
            text, sizeof(text), model->label, model->text, model->id);
        widget = ToriRSChrome_Paragraph(
            &app->plugin_ui, app->plugin_panel, text);
        break;

    case TORIRS_PANEL_WIDGET_LABEL:
        app_plugin_panel_readout(
            text, sizeof(text), model->label, model->text, model->id);
        widget = ToriRSChrome_Label(
            &app->plugin_ui, app->plugin_panel, text);
        break;

    case TORIRS_PANEL_WIDGET_KEY_VALUE:
        widget = ToriRSChrome_KeyValue(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : model->id,
            model->text);
        break;

    case TORIRS_PANEL_WIDGET_CHECKBOX:
    case TORIRS_PANEL_WIDGET_TOGGLE:
        widget = ToriRSChrome_Checkbox(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : model->id,
            model->value ? 1 : model->checked);
        break;

    case TORIRS_PANEL_WIDGET_INPUT:
        widget = ToriRSChrome_TextInput(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : model->id,
            model->text);
        break;

    case TORIRS_PANEL_WIDGET_TEXTAREA:
        widget = ToriRSChrome_TextArea(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : model->id,
            model->text,
            0);
        break;

    case TORIRS_PANEL_WIDGET_DROPDOWN:
    {
        if( model->structured_select )
        {
            struct ToriRSChromeSelectOptionInput
                options[TORIRS_CHROME_SELECT_OPTIONS_MAX];
            int const count = app_plugin_panel_select_inputs(
                model, options, TORIRS_CHROME_SELECT_OPTIONS_MAX);

            if( count >= 0 )
                widget = ToriRSChrome_DropdownStructured(
                    &app->plugin_ui,
                    app->plugin_panel,
                    model->label[0] ? model->label : model->id,
                    options,
                    count,
                    model->selected_value);
            break;
        }
        int count = 0;
        char const* const* choices =
            app_plugin_choices_add(app, model->choices, &count);
        if( choices )
            widget = ToriRSChrome_Dropdown(
                &app->plugin_ui,
                app->plugin_panel,
                model->label[0] ? model->label : model->id,
                choices,
                count,
                model->selected >= 0 && model->selected < count
                    ? model->selected
                    : app_plugin_choice_index(choices, count, model->text));
        else
            widget = ToriRSChrome_TextInput(
                &app->plugin_ui,
                app->plugin_panel,
                model->label[0] ? model->label : model->id,
                model->text);
        break;
    }

    case TORIRS_PANEL_WIDGET_BUTTON:
        widget = ToriRSChrome_Button(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : model->id);
        /* A button's `value` IS its availability -- what the builder's
         * `enabled` argument writes. It used to be stored and never read, so
         * a page that offered a command it could not service drew it as
         * live. @see ToriRSChromeWidget::disabled. */
        ToriRSChrome_SetDisabled(&app->plugin_ui, widget, !model->value);
        break;

    case TORIRS_PANEL_WIDGET_SEPARATOR:
        widget = ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);
        break;

    case TORIRS_PANEL_WIDGET_LIST_ROW:
        app_plugin_panel_readout(
            text, sizeof(text), model->label, model->text, model->id);
        widget = ToriRSChrome_ListRow(
            &app->plugin_ui, app->plugin_panel, text,
            model->value ? 1 : model->checked, 1);
        break;

    case TORIRS_PANEL_WIDGET_ACTION_ROW:
        widget = ToriRSChrome_ActionRow(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : model->id,
            model->text);
        break;

    case TORIRS_PANEL_WIDGET_IMAGE:
        app_plugin_panel_readout(
            text, sizeof(text), model->label, model->text, "Image");
        {
            char shown[TORIRS_CHROME_INPUT_MAX];
            snprintf(shown, sizeof(shown), "[Image] %.183s", text);
            widget = ToriRSChrome_LabelColored(
                &app->plugin_ui, app->plugin_panel, shown, 0xFFB8A276u);
        }
        break;

    case TORIRS_PANEL_WIDGET_PROGRESS:
        widget = ToriRSChrome_Progress(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : model->id,
            model->text,
            model->value);
        break;

    case TORIRS_PANEL_WIDGET_ERROR:
        widget = ToriRSChrome_Error(
            &app->plugin_ui,
            app->plugin_panel,
            model->label[0] ? model->label : "Plugin error",
            model->text);
        break;

    case TORIRS_PANEL_WIDGET_CUSTOM:
        /* A custom id routes draw and input callbacks; it is not a caption.
         * The shorthand builder deliberately authors no label, while a
         * general CUSTOM node can still opt into one. */
        widget = ToriRSChrome_Custom(
            &app->plugin_ui,
            app->plugin_panel,
            model->label,
            model->preferred_height * ToriRSChrome_Scale(&app->plugin_ui));
        break;

    default:
        return -1;
    }

    if( widget >= 0 )
        ToriRSChrome_WidgetSetIntentSerial(
            &app->plugin_ui, widget, model->serial);
    return widget;
}

/** Materialize one semantic node AND record it as a new page row. */
static int
app_plugin_panel_add_semantic(
    struct App* app,
    int plugin,
    int model_index,
    struct ToriRS_PanelWidget const* model)
{
    int widget;

    assert(app);
    assert(model);
    widget = app_plugin_panel_build_semantic(app, model);
    app_plugin_panel_track_semantic(app, widget, plugin, model_index, model);
    return widget;
}

/** Does the dropdown's borrowed slice still spell the model's option list? */
static int
app_plugin_panel_options_match(
    struct ToriRSChromeWidget const* widget,
    char const* choices)
{
    char const* at = choices ? choices : "";
    int index = 0;

    if( widget->kind != TORIRS_CHROME_W_DROPDOWN )
        return !at[0];
    if( !at[0] )
        return widget->option_count == 0;
    while( *at )
    {
        char const* end = strchr(at, '|');
        size_t const len = end ? (size_t)(end - at) : strlen(at);
        if( index >= widget->option_count || !widget->options ||
            strlen(widget->options[index]) != len ||
            strncmp(widget->options[index], at, len) != 0 )
            return 0;
        index++;
        if( !end )
            break;
        at = end + 1;
    }
    return index == widget->option_count;
}

/**
 * Restate a dropdown's option list on the retained page.
 *
 * Compare-then-set, and the comparison is not an optimisation: the chrome
 * keeps POINTERS into an app-owned arena that is only reset by a build, so
 * re-adding an unchanged list on every refresh would fill it. A list that
 * really did change is worth one arena entry.
 */
static void
app_plugin_panel_set_options(struct App* app, int widget, char const* choices)
{
    char const* const* options;
    int count = 0;

    assert(app);
    if( app_plugin_panel_options_match(&app->plugin_ui.widgets[widget], choices) )
        return;
    options = app_plugin_choices_add(app, choices, &count);
    if( options )
        ToriRSChrome_DropdownSetOptions(
            &app->plugin_ui, widget, options, count,
            ToriRSChrome_DropdownSelected(&app->plugin_ui, widget));
}

/** Chrome primitive kind used for one semantic model kind. */
static int
app_plugin_panel_semantic_chrome_kind(struct ToriRS_PanelWidget const* model)
{
    switch( model->kind )
    {
    case TORIRS_PANEL_WIDGET_SECTION:
    case TORIRS_PANEL_WIDGET_PARAGRAPH:
    case TORIRS_PANEL_WIDGET_LABEL:
    case TORIRS_PANEL_WIDGET_KEY_VALUE:
    case TORIRS_PANEL_WIDGET_IMAGE:
    case TORIRS_PANEL_WIDGET_PROGRESS:
    case TORIRS_PANEL_WIDGET_ERROR:
        return TORIRS_CHROME_W_LABEL;
    case TORIRS_PANEL_WIDGET_CUSTOM:
        return TORIRS_CHROME_W_CUSTOM;
    case TORIRS_PANEL_WIDGET_CHECKBOX:
    case TORIRS_PANEL_WIDGET_TOGGLE:
        return TORIRS_CHROME_W_CHECKBOX;
    case TORIRS_PANEL_WIDGET_INPUT:
        return TORIRS_CHROME_W_TEXTINPUT;
    case TORIRS_PANEL_WIDGET_TEXTAREA:
        return TORIRS_CHROME_W_TEXTAREA;
    case TORIRS_PANEL_WIDGET_DROPDOWN:
        return model->structured_select || model->choices[0]
                   ? TORIRS_CHROME_W_DROPDOWN
                   : TORIRS_CHROME_W_TEXTINPUT;
    case TORIRS_PANEL_WIDGET_BUTTON:
        return TORIRS_CHROME_W_BUTTON;
    case TORIRS_PANEL_WIDGET_SEPARATOR:
        return TORIRS_CHROME_W_SEPARATOR;
    case TORIRS_PANEL_WIDGET_LIST_ROW:
    case TORIRS_PANEL_WIDGET_ACTION_ROW:
        return TORIRS_CHROME_W_LISTROW;
    default:
        return TORIRS_CHROME_W_FREE;
    }
}

/* Defined with the rest of the custom-region bookkeeping, below. */
static void
app_plugin_panel_custom_pending_set(
    struct App* app,
    struct AppPluginPanelRow* row,
    int pending);

/**
 * Retire the retained custom run belonging to one identity.
 *
 * Not a general erasure: a reidentified well's last bitmap was drawn against
 * the mapping that just changed, so it is the one run that must NOT survive.
 * Everything else in the pool keeps its owner and its pixels, which is the
 * whole difference between this and the page rebuild it replaces.
 */
static void
app_plugin_panel_overlay_retire(struct App* app, uint32_t serial)
{
    int out = 0;

    assert(app);
    if( serial == 0 )
        return;
    for( int i = 0; i < app->panel_overlay_count; i++ )
    {
        if( app->panel_overlay_owner[i] == serial )
            continue;
        if( out != i )
        {
            app->panel_overlays[out] = app->panel_overlays[i];
            app->panel_overlay_owner[out] = app->panel_overlay_owner[i];
            app->panel_overlay_row[out] = app->panel_overlay_row[i];
        }
        out++;
    }
    if( out == app->panel_overlay_count )
        return;
    app->panel_overlay_count = out;
    app->panel_overlay_revision++;
    if( app->panel_overlay_revision == 0 )
        app->panel_overlay_revision++;
}

/**
 * Replace ONE row's presentation node in place, keeping the rows around it.
 *
 * The host reminted this row's serial, so the node the executor holds belongs
 * to an identity that no longer exists: it has to be removed and built again,
 * or a queued intent naming the old serial would still be delivered. What it
 * must NOT cost is the rest of the page -- the chrome adds at the END of the
 * panel's row list, so the fresh node is moved back to the slot the old one
 * occupied. The panel's scroll is untouched (only PanelClearWidgets zeroes
 * it) and every other row's retained custom run is untouched.
 */
static int
app_plugin_panel_reidentify_row(
    struct App* app,
    int row_index,
    struct ToriRS_PanelWidget const* model)
{
    struct AppPluginPanelRow* row;
    uint32_t retired;
    int before;
    int fresh;

    assert(app);
    assert(model);
    assert(row_index >= 0);
    assert(row_index < app->plugin_panel_row_count);

    row = &app->plugin_panel_rows[row_index];
    retired = row->widget_serial;
    before = ToriRSChrome_WidgetPrev(&app->plugin_ui, row->widget);
    ToriRSChrome_WidgetRemove(&app->plugin_ui, row->widget);
    fresh = app_plugin_panel_build_semantic(app, model);
    if( fresh < 0 )
    {
        /* The old node is already gone, so the page and the model disagree;
         * say so and let the caller take the rebuild path. */
        row->widget = -1;
        return 0;
    }
    ToriRSChrome_WidgetMoveAfter(&app->plugin_ui, fresh, before);

    row->widget = fresh;
    row->widget_serial = model->serial;
    row->widget_kind = model->kind;
    if( model->kind == TORIRS_PANEL_WIDGET_CUSTOM )
    {
        app_plugin_panel_overlay_retire(app, retired);
        /* Its geometry is resolved by the next layout pass against the new
         * node; until then nothing may claim the old region for it. */
        row->custom_layout_valid = 0;
        app_plugin_panel_custom_pending_set(app, row, 0);
    }
    return 1;
}

/** Apply one exact host-model mutation to its already-retained chrome row. */
static int
app_plugin_panel_patch_row(
    struct App* app,
    uint32_t generation,
    struct ToriRS_PluginPanelChange const* change)
{
    struct AppPluginPanelRow const* row;
    struct ToriRS_PanelWidget const* model;
    char text[TORIRS_CHROME_INPUT_MAX];
    int mapped;

    assert(app);
    assert(change);
    if( change->widget_index < 0 ||
        change->widget_index >= TORIRS_PLUGIN_WIDGETS_MAX )
        return 0;
    mapped = app->plugin_panel_model_rows[change->widget_index] - 1;
    if( mapped < 0 || mapped >= app->plugin_panel_row_count )
        return 0;
    row = &app->plugin_panel_rows[mapped];
    model = PluginHost_PanelWidgetAt(
        app->plugins, generation, change->widget_index);
    if( row->kind != APP_PLUGIN_ROW_PANEL_WIDGET ||
        row->model_index != change->widget_index || !model ||
        model->serial != change->widget_serial ||
        model->kind != row->widget_kind ||
        strcmp(model->id, row->widget_id) != 0 || row->widget < 0 ||
        row->widget >= app->plugin_ui.widget_count ||
        app->plugin_ui.widgets[row->widget].kind !=
            app_plugin_panel_semantic_chrome_kind(model) )
        return 0;

    /* Identity first, and alone: the row it names is about to stop existing,
     * so patching properties onto the node being replaced would be writing to
     * a handle the rebuild then frees. Any property the same journal entry
     * also carried is already in the model the fresh node is built from. */
    if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_IDENTITY )
        return app_plugin_panel_reidentify_row(app, mapped, model);

    /* Every other change is a property of the node the row already holds, so
     * the retained identity has to still be the model's. */
    if( model->serial != row->widget_serial )
        return 0;

    switch( model->kind )
    {
    case TORIRS_PANEL_WIDGET_SECTION:
    case TORIRS_PANEL_WIDGET_PARAGRAPH:
    case TORIRS_PANEL_WIDGET_LABEL:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
        {
            app_plugin_panel_readout(
                text, sizeof(text), model->label, model->text, model->id);
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, text);
        }
        break;
    case TORIRS_PANEL_WIDGET_KEY_VALUE:
    case TORIRS_PANEL_WIDGET_ERROR:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, model->text);
        /* The NAME half. A key/value row is two strings, and before this only
         * the reading could be patched: renaming the key rebuilt the page. */
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_LABEL )
            ToriRSChrome_SetLabel(
                &app->plugin_ui, row->widget,
                model->label[0] ? model->label : model->id);
        break;
    case TORIRS_PANEL_WIDGET_IMAGE:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
        {
            char shown[TORIRS_CHROME_INPUT_MAX];
            app_plugin_panel_readout(
                text, sizeof(text), model->label, model->text, "Image");
            snprintf(shown, sizeof(shown), "[Image] %.183s", text);
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, shown);
        }
        break;
    case TORIRS_PANEL_WIDGET_PROGRESS:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, model->text);
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_VALUE )
            ToriRSChrome_SetProgress(&app->plugin_ui, row->widget, model->value);
        break;
    case TORIRS_PANEL_WIDGET_CUSTOM:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_HEIGHT )
            ToriRSChrome_SetCustomHeight(
                &app->plugin_ui,
                row->widget,
                model->preferred_height * ToriRSChrome_Scale(&app->plugin_ui));
        break;
    case TORIRS_PANEL_WIDGET_CHECKBOX:
    case TORIRS_PANEL_WIDGET_TOGGLE:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_VALUE )
            ToriRSChrome_SetChecked(
                &app->plugin_ui, row->widget,
                model->value ? 1 : model->checked);
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_LABEL )
            ToriRSChrome_SetLabel(
                &app->plugin_ui, row->widget,
                model->label[0] ? model->label : model->id);
        break;
    case TORIRS_PANEL_WIDGET_INPUT:
    case TORIRS_PANEL_WIDGET_TEXTAREA:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, model->text);
        break;
    case TORIRS_PANEL_WIDGET_DROPDOWN:
        if( model->structured_select )
        {
            if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_OPTIONS )
            {
                struct ToriRSChromeSelectOptionInput
                    options[TORIRS_CHROME_SELECT_OPTIONS_MAX];
                int const count = app_plugin_panel_select_inputs(
                    model, options, TORIRS_CHROME_SELECT_OPTIONS_MAX);
                if( count < 0 )
                    return 0;
                ToriRSChrome_DropdownSetStructuredOptions(
                    &app->plugin_ui,
                    row->widget,
                    options,
                    count,
                    model->selected_value);
            }
            else if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_VALUE )
                ToriRSChrome_DropdownSetSelected(
                    &app->plugin_ui, row->widget, model->selected);
        }
        else if( model->choices[0] )
        {
            if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_OPTIONS )
                app_plugin_panel_set_options(
                    app, row->widget, model->choices);
            if( change->flags & (TORIRS_PLUGIN_PANEL_CHANGE_OPTIONS |
                                 TORIRS_PLUGIN_PANEL_CHANGE_VALUE) )
                ToriRSChrome_DropdownSetSelected(
                    &app->plugin_ui, row->widget, model->selected);
        }
        else if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, model->text);
        /* Outside the structured/legacy split above, because a dropdown's
         * CAPTION is the same string whichever way its options are carried,
         * and the degraded field the option pool falls back to still has one. */
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_LABEL )
            ToriRSChrome_SetLabel(
                &app->plugin_ui, row->widget,
                model->label[0] ? model->label : model->id);
        break;
    case TORIRS_PANEL_WIDGET_BUTTON:
        /* A caption set through panel.set_text lands on the button: the
         * tracker's Pause/Unpause flips here. The host journals the change
         * and used to apply nothing, reporting OK. */
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, model->text);
        /* And its availability, which panel.set_value journals as a VALUE
         * change: a command that became servable has to become pressable on
         * the page that is already there, not on the next rebuild. */
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_VALUE )
            ToriRSChrome_SetDisabled(
                &app->plugin_ui, row->widget, !model->value);
        break;
    case TORIRS_PANEL_WIDGET_LIST_ROW:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
        {
            app_plugin_panel_readout(
                text, sizeof(text), model->label, model->text, model->id);
            ToriRSChrome_SetLabel(&app->plugin_ui, row->widget, text);
        }
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_VALUE )
            ToriRSChrome_SetChecked(
                &app->plugin_ui, row->widget,
                model->value ? 1 : model->checked);
        break;
    case TORIRS_PANEL_WIDGET_ACTION_ROW:
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_TEXT )
            ToriRSChrome_SetText(&app->plugin_ui, row->widget, model->text);
        if( change->flags & TORIRS_PLUGIN_PANEL_CHANGE_LABEL )
            ToriRSChrome_SetLabel(
                &app->plugin_ui, row->widget,
                model->label[0] ? model->label : model->id);
        break;
    case TORIRS_PANEL_WIDGET_SEPARATOR:
    default:
        break;
    }
    return 1;
}

/**
 * Drain the host's exact retained mutation journal.
 *
 * Structural declaration changes return false and take the existing complete
 * rebuild path. Ordinary updates never validate or restate an unrelated row:
 * the generation-scoped model slot maps directly to the one chrome handle it
 * created, and the host's per-slot flags select only the affected properties.
 */
static int
app_plugin_panel_patch_semantic(
    struct App* app,
    int active,
    uint32_t generation)
{
    struct ToriRS_PluginPanelChange change;
    uint32_t const revision = PluginHost_PanelModelRevision(app->plugins);
    int result;
    int changed = 0;

    if( active < 0 || active != g_plugin_page ||
        generation != app->plugin_panel_built_generation )
        return 0;
    while( (result = PluginHost_PanelChangeNext(
                app->plugins, generation, &change)) > 0 )
    {
        if( !app_plugin_panel_patch_row(app, generation, &change) )
            return 0;
        changed++;
    }
    if( result < 0 )
        return 0;
    /* A revision without a row is an unjournalled/structural mutation. Never
     * bless it merely because the queue happened to be empty. */
    return changed > 0 || revision == app->plugin_panel_built_model_rev;
}

static void
app_plugin_panel_sync(struct App* app)
{
    int count;
    int rev;
    int panel_active;
    uint32_t panel_generation;
    uint32_t panel_model_rev;

    assert(app);
    if( !app->plugins )
    {
        app_plugin_panel_overlay_reset(app, 0);
        return;
    }

    /* A scale or canvas change re-places the BOX without rebuilding the rows:
     * the build gate below deliberately keeps a typed-into field alive, and a
     * resize is not a reason to lose the caret -- but it is a reason to move
     * the window back where it can be reached. */
    if( app->plugin_panel >= 0 &&
        (g_plugin_geom_scale_built != ToriRSChrome_Scale(&app->plugin_ui) ||
         g_plugin_geom_canvas_w_built != UITREE_LAYOUT_ROOT_W ||
         g_plugin_geom_canvas_h_built != UITREE_LAYOUT_ROOT_H) )
        app_plugin_panel_place(app);

    count = PluginHost_Count(app->plugins);
    panel_active = PluginHost_PanelActive(app->plugins);
    panel_generation = PluginHost_PanelSelectionGeneration(app->plugins);
    /* panel_clear outside on_ui_build marks the selected model for one
     * rebuild. This call is cheap when it is already retained and, crucially,
     * can only invoke the currently selected plugin. */
    if( panel_active >= 0 && panel_active == g_plugin_page )
    {
        (void)PluginHost_PanelEnsureBuilt(app->plugins, panel_generation);
        panel_active = PluginHost_PanelActive(app->plugins);
        panel_generation = PluginHost_PanelSelectionGeneration(app->plugins);
    }
    panel_model_rev = PluginHost_PanelModelRevision(app->plugins);
    rev = 0;
    if( app->plugin_panel_built_for == count && app->plugin_panel_built_rev == rev &&
        app->plugin_panel_built_generation == panel_generation &&
        (g_plugin_page >= 0 ||
         app->plugin_panel_built_registry_rev ==
             PluginHost_PanelRegistryRevision(app->plugins)) &&
        g_plugin_page_built == g_plugin_page &&
        g_plugin_page_view_built == g_plugin_page_view &&
        g_plugin_fullscreen_built == g_plugin_fullscreen )
    {
        if( app->plugin_panel_built_model_rev == panel_model_rev )
            return;
        if( app_plugin_panel_patch_semantic(app, panel_active, panel_generation) )
        {
            app->plugin_panel_built_model_rev = panel_model_rev;
            return;
        }
    }

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
        ToriRSChrome_PanelSetFixedHeight(
            &app->plugin_ui, app->plugin_panel, 260 * scale);
        ToriRSChrome_PanelSetScrollable(&app->plugin_ui, app->plugin_panel, 1);
        /* The interfaces' own nine-slice border rather than the minimenu's
         * rails. This is the one panel a PLAYER sees, and it has to look like
         * the game's own furniture whichever executor is bound to it. */
        ToriRSChrome_PanelSetFramed(&app->plugin_ui, app->plugin_panel, 1);
        ToriRSChrome_PanelSetVisible(
            &app->plugin_ui, app->plugin_panel, app->plugin_panel_visible);
    }

    /* Geometry, re-applied on every build rather than only on the first.
     * @see app_plugin_panel_place, which is also how a scale or canvas change
     * re-places the box between builds. */
    app_plugin_panel_place(app);

    /* A page that named a plugin the host no longer has is a page with nothing
     * to build; the roster is the honest answer to that. */
    if( g_plugin_page >= count )
    {
        app_plugin_page_select(app, -1, APP_PLUGIN_VIEW_SETTINGS);
        panel_active = PluginHost_PanelActive(app->plugins);
        panel_generation = PluginHost_PanelSelectionGeneration(app->plugins);
        panel_model_rev = PluginHost_PanelModelRevision(app->plugins);
    }

    /* The title says where you are, the way a page's own header would -- there
     * is no tab strip to say it any more. */
    ToriRSChrome_PanelSetTitle(
        &app->plugin_ui, app->plugin_panel,
        g_plugin_page < 0
            ? "Plugins"
            : panel_active == g_plugin_page
                  ? PluginHost_PanelTitle(app->plugins, g_plugin_page)
                  : PluginHost_Title(app->plugins, g_plugin_page));

    /* A structural redeclaration can keep the same selection generation
     * (`panel.clear()` followed by EnsureBuilt). Retained custom runs are keyed
     * by widget serial, so retire the old serials here rather than waiting for
     * the generation-only reset in the draw pass. */
    app_plugin_panel_overlay_reset(
        app, panel_active >= 0 ? panel_generation : 0);
    /*
     * The READER'S PLACE, carried across the rebuild.
     *
     * ClearWidgets takes the scroll to the top with the widget list, because
     * for a page that CHANGED that is right. But the one legitimate rebuild
     * the row model still has -- a detail block opened, a flag list grew, a
     * heading arrived -- re-declares the SAME page, and sending it back to the
     * top under whoever was reading it is the last piece of the flash the
     * reconciler exists to remove. Restored only when the page is the same
     * page: a different plugin's page, or the roster, has no place to keep.
     */
    {
        int const keep_scroll =
            g_plugin_page == g_plugin_page_scrolled
                ? ToriRSChrome_PanelScroll(&app->plugin_ui, app->plugin_panel)
                : 0;
        ToriRSChrome_PanelClearWidgets(&app->plugin_ui, app->plugin_panel);
        /* Not clamped here: the content it would be clamped against is the
         * page just thrown away. The next Build clamps against the new one. */
        ToriRSChrome_PanelSetScroll(&app->plugin_ui, app->plugin_panel, keep_scroll);
        g_plugin_page_scrolled = g_plugin_page;
    }
    g_plugin_back_widget = -1;
    g_plugin_fullscreen_widget = -1;

    /*
     * The size toggle, first and on EVERY page.
     *
     * On every page because the window is a window whichever page is open, and
     * a control that only exists on the roster is one you cannot reach from
     * the page you are actually reading. First because it changes the shape of
     * everything under it, and a row that reflows the list it sits in the
     * middle of moves under the finger that is reaching for it.
     *
     * A row and not a title-bar box: the title bar's only control is Close,
     * every widget in this chrome is a row, and a second kind of hit target up
     * there would need its own sprite, hover state and hit region for one
     * button. It also has to be pressable with a finger, and a row already is.
     */
    if( app->plugin_exec_kind == TORIRS_CHROME_EXEC_BUFFER )
    {
        g_plugin_fullscreen_widget = ToriRSChrome_Button(
            &app->plugin_ui, app->plugin_panel,
            g_plugin_fullscreen ? "Exit fullscreen" : "Fullscreen");
        ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);
    }

    app->plugin_panel_row_count = 0;
    app->plugin_panel_custom_row_count = 0;
    app->plugin_panel_custom_pending_count = 0;
    memset(
        app->plugin_panel_model_rows,
        0,
        sizeof(app->plugin_panel_model_rows));
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
         * not only that table: the Lua runtime registers a further plugin per
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
                 * A RUNTIME HOST is machinery, and a working one has no row.
                 *
                 * The Lua runtime host is registered beside the scripts it runs -- that
                 * uniformity is the whole design -- so it also appeared in the
                 * roster, called "lua", sitting among them and looking like a peer
                 * with nothing a user does to it. Its scripts are the rows; they
                 * speak for it.
                 *
                 * It comes back the moment it has something to say, and the two
                 * conditions below are the two states you cannot get out of
                 * otherwise: a fault has to be visible somewhere or a broken Lua
                 * layer is a client with no plugins and no explanation, and a
                 * switched-off runtime host has to have a switch or it can never come
                 * back on.
                 */
                if( PluginHost_IsRuntimeHost(app->plugins, p) && !err &&
                    PluginHost_IsEnabled(app->plugins, p) )
                    continue;

                /*
                 * A HIDDEN builtin has no row at all, faulting or not.
                 *
                 * Unlike a runtime host, there is nothing here for the user to do
                 * about it: it is a feature of the client whose switch is in the
                 * cache's own All Settings panel, and a row here would be a second
                 * switch over the same thing.
                 */
                if( PluginHost_IsHidden(app->plugins, p) )
                    continue;

                /* The TITLE, not the name: the name is the ini key. A semantic
                 * entry also carries its live badge/attention in this portable
                 * roster until each platform's narrow rail can expose those
                 * atoms itself. Nothing keys off the spelling; the row carries
                 * the plugin index. */
                if( PluginHost_PanelHasPage(app->plugins, p) )
                    /*
                     * The plugin's NAME, and nothing appended to it.
                     *
                     * A badge and an attention mark used to be spliced in here
                     * -- "XP Tracker [99.999M]" -- which made a roster row read
                     * as a readout and let a page write text into a strip that
                     * is a column of icons. What a row is for is finding the
                     * plugin and switching it; what it says is what the plugin
                     * is called.
                     */
                    snprintf(
                        label, sizeof(label), "%s",
                        PluginHost_PanelTitle(app->plugins, p));
                else
                    snprintf(
                        label, sizeof(label), "%s", PluginHost_Title(app->plugins, p));
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
                    ToriRSChrome_Error(
                        &app->plugin_ui,
                        app->plugin_panel,
                        PluginHost_Title(app->plugins, p),
                        err);
                }
            }
        }
    }
    /* ---- one plugin's page ------------------------------------------------ */
    else
    {
        int const p = g_plugin_page;
        /*
         * ONE face at a time, chosen by the entry point. @see enum
         * AppPluginPageView -- the rail opens the plugin's page, the roster
         * opens its settings, and a plugin with no page has only the settings
         * whichever way it was reached.
         */
        int const on_page =
            g_plugin_page_view == APP_PLUGIN_VIEW_PAGE && panel_active == p &&
            PluginHost_PanelView(app->plugins) == TORIRS_PANEL_VIEW_PAGE;
        int const cfg_count = on_page ? 0 : PluginHost_ConfigCount(app->plugins, p);
        /* The declared model belongs to whichever face is mounted: the page's
         * own readouts, or the extra controls a plugin put on its settings. */
        int const panel_count =
            panel_active == p
                ? PluginHost_PanelWidgetCount(app->plugins, panel_generation)
                : 0;
        /* Whether the OTHER face exists, so the page can offer a way to it. */
        int const has_settings_face = PluginHost_ConfigCount(app->plugins, p) > 0;
        int has_settings = 0;

        if( getenv("TORIRS_CHROME_DEBUG") )
            fprintf(
                stderr, "chrome: page build p=%d on_page=%d g_view=%d active=%d host_view=%d panel_count=%d cfg=%d\n",
                p, on_page, g_plugin_page_view, panel_active,
                PluginHost_PanelView(app->plugins), panel_count, cfg_count);

        /* Back first, so the way out is the first thing on the page and in the
         * same place on every page. Its handle is remembered rather than
         * tracked as a row: it belongs to no plugin.
         *
         * On every executor, not only the in-canvas one: a plugin's generated
         * settings page is a sub-page of the roster, and the browser rail's
         * only other way back is knowing that the pressed Manage stone reopens
         * the roster. A registered semantic page is its own destination and
         * does not get one -- its stone is the way in and out. */
        if( !on_page )
        {
            g_plugin_back_widget =
                ToriRSChrome_Button(&app->plugin_ui, app->plugin_panel, "< Plugins");
            ToriRSChrome_Separator(&app->plugin_ui, app->plugin_panel);
        }

        /* Only PanelActive is read. Registered-but-inactive plugins have rail
         * metadata and nothing else materialized, so merely opening Manage
         * Plugins cannot execute or allocate every custom page. */
        for( int i = 0; i < panel_count; i++ )
        {
            struct ToriRS_PanelWidget const* model =
                PluginHost_PanelWidgetAt(app->plugins, panel_generation, i);
            if( !model )
                break; /* selection changed while the model was being read */
            app_plugin_panel_add_semantic(app, p, i, model);
        }

        /*
         * NO settings door on the page.
         *
         * A plugin's page is what it has to say; its settings are a
         * destination of their own, reached from the roster where every
         * plugin's settings are. A button here made the page carry a control
         * that is not about the page, and it read as part of the plugin's own
         * chrome when it belongs to neither.
         * @see enum AppPluginPageView.
         */
        (void)has_settings_face;

        /* On the SETTINGS face, whatever the plugin declared sits above the
         * generated form under a rule, so the two groups read as two: its own
         * controls, then the schema's staged rows and their Save. */
        if( !on_page && panel_count > 0 && cfg_count > 0 )
        {
            ToriRSChrome_Heading(
                &app->plugin_ui, app->plugin_panel, "Plugin settings");
        }

        for( int c = 0; c < cfg_count; c++ )
        {
            struct ToriRS_ConfigItem const* item =
                PluginHost_ConfigItem(app->plugins, p, c);
            int widget = -1;

            /* A key with no label is state the plugin persists for itself --
             * the highlighter's tag list -- not something to hand-edit. */
            if( !item || !item->label )
                continue;
            has_settings = 1;

            if( item->type == TORIRS_CONFIG_BOOL )
            {
                widget = ToriRSChrome_Checkbox(
                    &app->plugin_ui,
                    app->plugin_panel,
                    item->label,
                    atoi(app_plugin_panel_value(app, p, item->key)) != 0);
            }
            else if( item->type == TORIRS_CONFIG_ENUM && item->choices )
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
            else if( item->type == TORIRS_CONFIG_COLOR )
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
            else if( item->type == TORIRS_CONFIG_TEXT )
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
    /* Callbacks above may have changed either compatibility controls or the
     * active model. Stamp what was actually consumed, not the pre-build
     * revision, so an unchanged next frame stays an O(1) sync. */
    app->plugin_panel_built_rev = 0;
    app->plugin_panel_built_model_rev = PluginHost_PanelModelRevision(app->plugins);
    app->plugin_panel_built_generation =
        PluginHost_PanelSelectionGeneration(app->plugins);
    app->plugin_panel_built_registry_rev =
        PluginHost_PanelRegistryRevision(app->plugins);
    PluginHost_PanelChangesAcknowledge(
        app->plugins, app->plugin_panel_built_generation);
    g_plugin_page_built = g_plugin_page;
    g_plugin_page_view_built = g_plugin_page_view;
    g_plugin_fullscreen_built = g_plugin_fullscreen;
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
        struct ToriRS_ConfigItem const* item;

        if( row->plugin != plugin || row->kind != APP_PLUGIN_ROW_CONFIG )
            continue;
        item = PluginHost_ConfigItem(app->plugins, plugin, row->cfg_index);
        if( !item )
            continue;

        if( item->type == TORIRS_CONFIG_BOOL )
        {
            char buf[4];
            snprintf(
                buf, sizeof(buf), "%d",
                ToriRSChrome_Checked(&app->plugin_ui, row->widget) ? 1 : 0);
            PluginHost_ConfigSet(app->plugins, plugin, item->key, buf);
        }
        else if( item->type == TORIRS_CONFIG_ENUM )
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
            if( item->type == TORIRS_CONFIG_INT && item->max > item->min )
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

/** The active host record named by one presented semantic row, or NULL. */
static struct ToriRS_PanelWidget const*
app_plugin_panel_model_for_row(
    struct App* app,
    struct AppPluginPanelRow const* row)
{
    uint32_t const generation = app->plugin_panel_built_generation;
    int const count = PluginHost_PanelWidgetCount(app->plugins, generation);

    if( row->kind != APP_PLUGIN_ROW_PANEL_WIDGET || row->widget_serial == 0 ||
        PluginHost_PanelActive(app->plugins) != row->plugin ||
        PluginHost_PanelSelectionGeneration(app->plugins) != generation )
        return NULL;
    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_PanelWidget const* model =
            PluginHost_PanelWidgetAt(app->plugins, generation, i);
        if( model && model->serial == row->widget_serial &&
            strcmp(model->id, row->widget_id) == 0 )
            return model;
    }
    return NULL;
}

/** Dispatch one copied semantic intent through all three identity fences. */
static int
app_plugin_panel_dispatch_row(
    struct App* app,
    struct AppPluginPanelRow const* row,
    int action,
    int value,
    char const* text,
    int x,
    int y)
{
    uint64_t sequence;

    if( !app || !app->plugins || !row ||
        app->plugin_panel_built_generation == 0 || row->widget_serial == 0 )
        return 0;
    sequence = ++app->plugin_panel_intent_sequence;
    if( sequence == 0 )
        sequence = ++app->plugin_panel_intent_sequence;
    return PluginHost_PanelDispatch(
        app->plugins,
        app->plugin_panel_built_generation,
        row->widget_serial,
        sequence,
        row->widget_id,
        action,
        value,
        text,
        x,
        y);
}

/**
 * Native editors/toggles deliver result state as TEXT/TOGGLE intents without
 * synthesizing an activation, and a canvas textarea has no Enter-to-commit
 * edge. Compare only semantic rows against their authoritative records after
 * input, dispatching any result that the activation path did not already
 * commit. Host-generated schema settings intentionally do not participate.
 */
static int
app_plugin_panel_reconcile_semantic(struct App* app)
{
    int dispatched = 0;

    assert(app);
    for( int i = 0; i < app->plugin_panel_row_count; i++ )
    {
        struct AppPluginPanelRow const* row = &app->plugin_panel_rows[i];
        struct ToriRS_PanelWidget const* model;
        int action = -1;
        int value = -1;
        char const* text = NULL;

        if( row->kind != APP_PLUGIN_ROW_PANEL_WIDGET )
            continue;
        model = app_plugin_panel_model_for_row(app, row);
        if( !model )
            break;

        switch( row->widget_kind )
        {
        case TORIRS_PANEL_WIDGET_CHECKBOX:
        case TORIRS_PANEL_WIDGET_TOGGLE:
        case TORIRS_PANEL_WIDGET_LIST_ROW:
            value = ToriRSChrome_Checked(&app->plugin_ui, row->widget) ? 1 : 0;
            if( value != (model->checked ? 1 : 0) )
                action = TORIRS_PANEL_ACTION_TOGGLE;
            break;

        case TORIRS_PANEL_WIDGET_INPUT:
        case TORIRS_PANEL_WIDGET_TEXTAREA:
            text = ToriRSChrome_Text(&app->plugin_ui, row->widget);
            if( strcmp(text ? text : "", model->text) != 0 )
                action = TORIRS_PANEL_ACTION_TEXT;
            break;

        case TORIRS_PANEL_WIDGET_DROPDOWN:
            if( row->widget >= 0 && row->widget < app->plugin_ui.widget_count &&
                app->plugin_ui.widgets[row->widget].kind == TORIRS_CHROME_W_DROPDOWN )
            {
                value = ToriRSChrome_DropdownSelected(&app->plugin_ui, row->widget);
                text = ToriRSChrome_DropdownSelectedValue(
                    &app->plugin_ui, row->widget);
                if( value != model->selected )
                    action = TORIRS_PANEL_ACTION_PICK;
            }
            else
            {
                /* Option-pool exhaustion degrades to a field, but still
                 * reports the plugin's semantic PICK with the copied text. */
                text = ToriRSChrome_Text(&app->plugin_ui, row->widget);
                value = -1;
                if( strcmp(text ? text : "", model->text) != 0 )
                    action = TORIRS_PANEL_ACTION_PICK;
            }
            break;

        default:
            break;
        }

        if( action >= 0 && app_plugin_panel_dispatch_row(
                              app, row, action, value, text ? text : "", 0, 0) )
            dispatched++;
    }
    return dispatched;
}

/* Apply one activated widget. */
static void
app_plugin_panel_apply(struct App* app, int widget)
{
    /* The page's own navigation first: Back belongs to no plugin, so it is not
     * in the row table the loop below walks. */
    if( widget >= 0 && widget == g_plugin_back_widget )
    {
        app_plugin_page_select(app, -1, APP_PLUGIN_VIEW_SETTINGS);
        return;
    }

    /* Nor does the size toggle. Flipping the flag is the whole of it: the
     * rebuild gate above notices the change and app_plugin_panel_sync re-applies
     * the geometry and re-labels this row on the next frame, so there is one
     * place that knows what each state looks like. */
    if( widget >= 0 && widget == g_plugin_fullscreen_widget )
    {
        g_plugin_fullscreen = !g_plugin_fullscreen;
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
                /* The ROSTER is a list of settings pages, so its row opens the
                 * settings -- not the plugin's own screen, which is what its
                 * rail stone is for. @see enum AppPluginPageView. */
                app_plugin_page_select(app, row->plugin, APP_PLUGIN_VIEW_SETTINGS);
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

        case APP_PLUGIN_ROW_PANEL_WIDGET:
        {
            int action = TORIRS_PANEL_ACTION_ACTIVATE;
            int value = -1;
            int local_x = 0;
            int local_y = 0;
            uint32_t custom_generation = 0;
            uint32_t custom_serial = 0;
            char const* text = NULL;

            /* The row was copied from one exact selection. Never retarget a
             * queued native click by looking a string id up in whichever page
             * happens to be active now; generation + never-reused serial + id
             * are all checked by the host. */
            if( app->plugin_panel_built_generation == 0 || row->widget_serial == 0 )
                return;

            switch( row->widget_kind )
            {
            case TORIRS_PANEL_WIDGET_CHECKBOX:
            case TORIRS_PANEL_WIDGET_TOGGLE:
                action = TORIRS_PANEL_ACTION_TOGGLE;
                value = ToriRSChrome_Checked(&app->plugin_ui, widget) ? 1 : 0;
                break;

            case TORIRS_PANEL_WIDGET_INPUT:
            case TORIRS_PANEL_WIDGET_TEXTAREA:
                action = TORIRS_PANEL_ACTION_TEXT;
                text = ToriRSChrome_Text(&app->plugin_ui, widget);
                break;

            case TORIRS_PANEL_WIDGET_DROPDOWN:
                action = TORIRS_PANEL_ACTION_PICK;
                value = ToriRSChrome_DropdownSelected(&app->plugin_ui, widget);
                text = ToriRSChrome_DropdownSelectedValue(
                    &app->plugin_ui, widget);
                if( !text )
                    text = "";
                break;

            case TORIRS_PANEL_WIDGET_LIST_ROW:
                if( !ToriRSChrome_ActivationWasAction(&app->plugin_ui) )
                {
                    action = TORIRS_PANEL_ACTION_TOGGLE;
                    value = ToriRSChrome_Checked(&app->plugin_ui, widget) ? 1 : 0;
                }
                break;

            case TORIRS_PANEL_WIDGET_ACTION_ROW:
                break;

            case TORIRS_PANEL_WIDGET_BUTTON:
                break;

            case TORIRS_PANEL_WIDGET_CUSTOM:
                if( !ToriRSChrome_ActivationWasCustom(
                        &app->plugin_ui,
                        &local_x,
                        &local_y,
                        &custom_generation,
                        &custom_serial) )
                    return;
                if( (custom_generation != 0 &&
                     custom_generation != app->plugin_panel_built_generation) ||
                    (custom_serial != 0 && custom_serial != row->widget_serial) )
                    return;
                /* Same event, same fences, same coordinates -- the BUTTON is
                 * the only difference, so it is the only thing decided here.
                 * A well is one control, and a plugin whose strip has per-band
                 * operations had no channel for them at all before this. */
                if( ToriRSChrome_ActivationWasMenu(&app->plugin_ui) )
                    action = TORIRS_PANEL_ACTION_MENU;
                break;

            /* Readouts have no interactive ToriRSChrome primitive. */
            case TORIRS_PANEL_WIDGET_LABEL:
            case TORIRS_PANEL_WIDGET_SEPARATOR:
            case TORIRS_PANEL_WIDGET_SECTION:
            case TORIRS_PANEL_WIDGET_PARAGRAPH:
            case TORIRS_PANEL_WIDGET_KEY_VALUE:
            case TORIRS_PANEL_WIDGET_IMAGE:
            case TORIRS_PANEL_WIDGET_PROGRESS:
            case TORIRS_PANEL_WIDGET_ERROR:
            default:
                return;
            }

            (void)app_plugin_panel_dispatch_row(
                app, row, action, value, text, local_x, local_y);
            return;
        }

        case APP_PLUGIN_ROW_CONFIG:
        default:
            /* Staged. Nothing happens until Save reads the widget back. */
            return;
        }
    }
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
int
app_plugin_io_down(struct App const* app)
{
    assert(app);
    if( !app->runner.px )
        return 0;
    return !Platform_IO_ServerReachable(app->runner.px);
}

static void
app_plugin_exec_bind(struct App* app);

void
app_plugin_window_set_open(struct App* app, int open)
{
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome: plugin window set_open(%d) tick=%d\n", open ? 1 : 0,
            g_plugin_panel_ticks);
    /* Collapse is a host transaction too: it invalidates the active
     * generation, sends the last visible layout, clears the sole mounted
     * model, and deliberately retains PanelLastSelected for the rail. */
    if( !open && app->plugins )
        (void)PluginHost_PanelClose(app->plugins);

    app->plugin_panel_visible = open ? 1 : 0;
    if( app->plugin_panel_visible )
    {
        /* Reopening a semantic detail page rebuilds only the remembered
         * plugin. Management and schema-only pages wake no panel plugin. */
        if( app->plugins && g_plugin_page >= 0 &&
            PluginHost_PanelHasPage(app->plugins, g_plugin_page) )
        {
            /* Back to the FACE it was closed on, not to the page: reopening a
             * settings form on the plugin's readout would be the window
             * changing the subject. @see enum ToriRS_PanelView. */
            int const want = g_plugin_page_view == APP_PLUGIN_VIEW_PAGE
                                 ? TORIRS_PANEL_VIEW_PAGE
                                 : TORIRS_PANEL_VIEW_SETTINGS;
            /* The VIEW is half the test, not an afterthought: the plugin can
             * already be the mounted one while the host holds its other face,
             * and a guard that compared only the plugin would leave the page
             * showing whichever face was up last. */
            if( PluginHost_PanelActive(app->plugins) != g_plugin_page ||
                PluginHost_PanelView(app->plugins) != want )
                (void)PluginHost_PanelSelectView(app->plugins, g_plugin_page, want);
        }
        ToriRSChromeShell_Select(
            &app->plugin_shell,
            g_plugin_page >= 0 && g_plugin_page_view == APP_PLUGIN_VIEW_PAGE
                ? g_plugin_page
                : TORIRS_CHROME_SHELL_PAGE_MANAGE);
    }
    else
        ToriRSChromeShell_Collapse(&app->plugin_shell);
    if( app->plugin_panel >= 0 )
        ToriRSChrome_PanelSetVisible(
            &app->plugin_ui, app->plugin_panel, app->plugin_panel_visible);

    /*
     * Lazy, and lazy in the way that matters: this runs on the frame the
     * window opens, so a session that never opens it never binds and never
     * opens a second OS window.
     */
    if( app->plugin_panel_visible )
    {
        /* The rail first, so the executor's begin() reads the width of the
         * page about to be shown. Bound first, it opened the pane at the
         * LAST selection's width and the tick's publish corrected it a frame
         * later: two window resizes per open, +360 then -40, which reads as
         * the window flinching. */
        app_plugin_rail_publish(app);
        app_plugin_exec_bind(app);
    }
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
         * So the page closes the way it opened: through the executor's own
         * end(). WEB/BROWSER close their application-owned page through the
         * common bridge, and the host needs to know none of it. The next open re-binds
         * from scratch, which is also what makes a window taken down by ITS
         * side (a title-bar X) openable again: `live` is the guard the bind
         * reads.
         */
        ToriRSChromeSync_Shutdown(&app->plugin_exec);
}

/** Copy every registered plugin destination into the platform-neutral rail. */
static void
app_plugin_rail_snapshot(
    struct App* app, struct ToriRSChromeRailSnapshot* snapshot)
{
    int count;

    assert(app);
    assert(snapshot);
    ToriRSChromeRailSnapshot_Init(snapshot);
    if( !app->plugins )
        return;

    snapshot->registry_revision =
        PluginHost_PanelRegistryRevision(app->plugins);
    snapshot->selection_generation = app->plugin_shell.selection_generation;
    snapshot->page_generation =
        PluginHost_PanelSelectionGeneration(app->plugins);
    snapshot->active_plugin = PluginHost_PanelActive(app->plugins);
    snapshot->last_selected_plugin =
        PluginHost_PanelLastSelected(app->plugins);
    snapshot->selected_entry = app->plugin_shell.active_plugin;
    /* A plugin's generated settings page has no stone of its own: it is
     * reached from the roster, so the rail keeps Manage Plugins pressed while
     * it is up, and pressing Manage again is the way back to the roster. Check
     * the host's mounted face as well as our desired face: if a rejected or
     * interrupted transition left those two disagreeing, the rail must report
     * what is actually on screen and the next plugin click must repair it. */
    if( snapshot->selected_entry >= 0 )
    {
        int const selected = snapshot->selected_entry;
        int const has_page = PluginHost_PanelHasPage(app->plugins, selected);
        int const managed_only =
            g_plugin_page_view != APP_PLUGIN_VIEW_PAGE ||
            PluginHost_PanelActive(app->plugins) != selected ||
            PluginHost_PanelView(app->plugins) != TORIRS_PANEL_VIEW_PAGE ||
            (has_page && PluginHost_IsEssential(app->plugins, selected));
        if( !ToriRSChromeRailSnapshot_IncludesPlugin(
                has_page, managed_only) )
            snapshot->selected_entry = TORIRS_CHROME_SHELL_PAGE_MANAGE;
    }
    snapshot->expanded = app->plugin_shell.expanded ? 1 : 0;
    snapshot->rail_hidden = app->plugin_nav.rail_hidden ? 1 : 0;

    /* Permanent application destination, in the same retained rail and the
     * same shared page pane as plugins. Its icon is the baked wrench. */
    (void)ToriRSChromeRailSnapshot_AddManage(
        snapshot, TORIRS_CHROME_SHELL_PAGE_MANAGE, "Manage Plugins");

    count = PluginHost_Count(app->plugins);
    for( int plugin = 0; plugin < count; plugin++ )
    {
        /* Essential entries are client settings reached through the roster,
         * not independent plugin pages. Giving them a rail stone duplicated
         * Manage Plugins with the same baked wrench fallback. */
        if( !ToriRSChromeRailSnapshot_IncludesPlugin(
                PluginHost_PanelHasPage(app->plugins, plugin),
                PluginHost_IsEssential(app->plugins, plugin)) )
            continue;
        (void)ToriRSChromeRailSnapshot_Add(
            snapshot,
            plugin,
            PluginHost_PanelTitle(app->plugins, plugin),
            PluginHost_PanelIconAsset(app->plugins, plugin),
            PluginHost_PanelPreferredWidth(app->plugins, plugin),
            "",
            PluginHost_PanelWantsAttention(app->plugins, plugin));
    }
}

static void
app_plugin_rail_publish(struct App* app)
{
    struct ToriRSChromeRailSnapshot snapshot;

    app_plugin_rail_snapshot(app, &snapshot);
    (void)ToriRSChromeRailSync_Run(
        &app->plugin_rail, &app->plugin_exec_pending, &snapshot);
    if( !app->plugins )
        return;
    for( int i = 0; i < snapshot.entry_count; i++ )
    {
        struct ToriRSChromeRailEntry const* entry = &snapshot.entries[i];
        struct ToriRSChromeRailIcon icon;

        if( entry->kind != TORIRS_CHROME_RAIL_ENTRY_PLUGIN )
            continue;
        memset(&icon, 0, sizeof(icon));
        icon.plugin_index = entry->plugin_index;
        icon.revision = PluginHost_PanelIconRevision(
            app->plugins, entry->plugin_index);
        if( icon.revision == 0 )
            continue;
        (void)PluginHost_PanelIconPixels(
            app->plugins,
            entry->plugin_index,
            icon.argb,
            TORIRS_CHROME_RAIL_ICON_PIXELS_MAX,
            &icon.width,
            &icon.height);
        (void)ToriRSChromeRailSync_Icon(
            &app->plugin_rail, &app->plugin_exec_pending, &icon);
    }
}

/**
 * Drain persistent rail work without ever invoking a plugin from a presenter
 * thread. Multiple clicks against one displayed generation coalesce to the
 * newest one, so a rapid A -> B gesture cannot briefly build A then reject B
 * merely because A advanced the generation first.
 */
static void
app_plugin_rail_select(struct App* app, int destination)
{
    assert(app);
    assert(app->plugins);
    struct AppPluginRailRouteInput const input = {
        .destination = destination,
        .destination_has_page =
            destination >= 0 && PluginHost_PanelHasPage(app->plugins, destination),
        .panel_visible = app->plugin_panel_visible,
        .shell_selection = app->plugin_shell.active_plugin,
        .requested_view = g_plugin_page_view,
        .host_selection = PluginHost_PanelActive(app->plugins),
        .host_view = PluginHost_PanelView(app->plugins),
    };
    int const route = AppPluginRailRoute_Decide(&input);

    if( route == APP_PLUGIN_RAIL_ROUTE_CLOSE )
    {
        app_plugin_window_set_open(app, 0);
        return;
    }
    if( route == APP_PLUGIN_RAIL_ROUTE_MANAGE )
    {
        app_plugin_page_select(app, -1, APP_PLUGIN_VIEW_SETTINGS);
        if( !app->plugin_panel_visible )
            app_plugin_window_set_open(app, 1);
        return;
    }
    if( route == APP_PLUGIN_RAIL_ROUTE_PAGE )
    {
        app_plugin_page_select(app, destination, APP_PLUGIN_VIEW_PAGE);
        if( !app->plugin_panel_visible )
            app_plugin_window_set_open(app, 1);
    }
}

static void
app_plugin_rail_drain(struct App* app)
{
    struct AppPluginRailDrainResult drained;
    uint32_t const generation = app->plugin_shell.selection_generation;

    AppPluginRailRoute_Drain(&app->plugin_exec_pending, generation, &drained);
    if( drained.have_layout &&
        (!app->plugin_rail_has_layout ||
         drained.layout.sequence >= app->plugin_rail_layout.sequence) )
    {
        app->plugin_rail_layout = drained.layout;
        app->plugin_rail_has_layout = 1;
    }

    if( !drained.have_select || !app->plugins ||
        drained.select.selection_generation != app->plugin_shell.selection_generation )
        return;

    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome: rail select plugin=%d seq=%llu gen=%u tick=%d\n",
            drained.select.plugin_index, (unsigned long long)drained.select.sequence,
            (unsigned)drained.select.selection_generation, g_plugin_panel_ticks);
    app_plugin_rail_select(app, drained.select.plugin_index);
}

/*
 * Bind the window to a presentation, the first time it is opened.
 *
 * Lazily, not at boot: WEB/BROWSER must not mount their DOM page for a window
 * the user never asked to see, and most sessions never open this at all. An
 * explicit choice comes from TORIRS_CHROME_EXECUTOR beside the theme override.
 *
 * An executor that will not start is not an error. ToriRSChromeExec_ForKind
 * already answers "not compiled in on this platform" with the buffer executor,
 * and a refused begin() is answered here with the same thing -- so a blocked
 * page, a missing embedded engine, or a lane with no web executor all end at
 * internal in-canvas chrome rather than at no chrome.
 */
static void
app_plugin_exec_bind_inner(struct App* app)
{
    /* Nothing was handed over: use the internal in-canvas fallback. */
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

/** Copy one retained item with the row's current visible scroll clip. */
int
app_plugin_panel_overlay_visible(
    struct App const* app,
    int index,
    struct UITreeEntityOverlay* out)
{
    struct ToriRSChromeRect clip;
    int right;
    int bottom;

    assert(app);
    assert(out);
    if( index < 0 || index >= app->panel_overlay_count ||
        app->panel_overlay_generation == 0 )
        return 0;
    *out = app->panel_overlays[index];
    {
        int const row_index = app->panel_overlay_row[index];
        struct AppPluginPanelRow const* row;

        if( row_index < 0 || row_index >= app->plugin_panel_row_count )
            return 0;
        row = &app->plugin_panel_rows[row_index];
        if( row->widget_serial != app->panel_overlay_owner[index] ||
            row->widget_kind != TORIRS_PANEL_WIDGET_CUSTOM ||
            !row->custom_layout_valid )
            return 0;
        clip = row->custom_clip;
    }
    right = clip.x + clip.w;
    bottom = clip.y + clip.h;
    if( out->clip_x > clip.x )
        clip.x = out->clip_x;
    if( out->clip_y > clip.y )
        clip.y = out->clip_y;
    if( out->clip_x + out->clip_w < right )
        right = out->clip_x + out->clip_w;
    if( out->clip_y + out->clip_h < bottom )
        bottom = out->clip_y + out->clip_h;
    clip.w = right - clip.x;
    clip.h = bottom - clip.y;
    if( clip.w <= 0 || clip.h <= 0 )
        return 0;
    out->clip_x = clip.x;
    out->clip_y = clip.y;
    out->clip_w = clip.w;
    out->clip_h = clip.h;
    return 1;
}

static void
app_plugin_panel_overlay_bump(struct App* app)
{
    app->panel_overlay_revision++;
    if( app->panel_overlay_revision == 0 )
        app->panel_overlay_revision++;
}

/** Re-place one retained custom run when only its buffer-space origin moved. */
static int
app_plugin_panel_overlay_move(
    struct App* app, uint32_t serial, int dx, int dy)
{
    int moved = 0;

    assert(app);
    if( serial == 0 || (dx == 0 && dy == 0) )
        return 0;
    for( int i = 0; i < app->panel_overlay_count; i++ )
    {
        struct UITreeEntityOverlay* item;

        if( app->panel_overlay_owner[i] != serial )
            continue;
        item = &app->panel_overlays[i];
        item->x += dx;
        item->y += dy;
        item->clip_x += dx;
        item->clip_y += dy;
        moved = 1;
    }
    if( moved )
        app_plugin_panel_overlay_bump(app);
    return moved;
}

static void
app_plugin_panel_custom_pending_set(
    struct App* app,
    struct AppPluginPanelRow* row,
    int pending)
{
    assert(app);
    assert(row);
    pending = pending ? 1 : 0;
    if( row->custom_present_pending == pending )
        return;
    row->custom_present_pending = pending;
    app->plugin_panel_custom_pending_count += pending ? 1 : -1;
    assert(app->plugin_panel_custom_pending_count >= 0);
    assert(app->plugin_panel_custom_pending_count <=
           app->plugin_panel_custom_row_count);
}

/** Drop every retained custom primitive when the selected page changes. */
static void
app_plugin_panel_overlay_reset(struct App* app, uint32_t generation)
{
    assert(app);
    if( app->panel_overlay_count > 0 || app->panel_overlay_generation != generation )
        app_plugin_panel_overlay_bump(app);
    app->panel_overlay_count = 0;
    app->panel_overlay_stage_count = 0;
    app->panel_overlay_stage_active = 0;
    app->panel_overlay_stage_overflow = 0;
    app->panel_overlay_generation = generation;
    app->panel_custom_last_draw_cycle = 0;
    app->panel_custom_has_draw_cycle = 0;
    for( int i = 0; i < app->plugin_panel_custom_row_count; i++ )
    {
        int const row = app->plugin_panel_custom_rows[i];
        app->plugin_panel_rows[row].custom_present_pending = 0;
    }
    app->plugin_panel_custom_pending_count = 0;
}

/**
 * Atomically replace one custom region's retained primitive run.
 *
 * A pass that staged NOTHING is a decline, not an erasure. A plugin whose art
 * has not landed yet -- the atlases and sprites cross the IO queue, and the
 * obj icons come out of an evicting cache -- returns from on_ui_draw
 * without drawing, and the host cannot tell that apart from a deliberate
 * clear. Committing it would swap the last complete picture for an empty run
 * and blank the well until the plugin's next tick invalidates it again,
 * which is precisely the flicker a list shows while its icons churn. Keeping
 * the retained run costs a stale frame at worst; committing costs a blank one.
 * A well with genuinely nothing to show has a zero height, not an empty run.
 */
static int
app_plugin_panel_overlay_commit(
    struct App* app,
    uint32_t generation,
    uint32_t serial,
    int row_index)
{
    int other = 0;
    int out = 0;

    assert(app);
    if( generation == 0 || serial == 0 || row_index < 0 ||
        row_index >= app->plugin_panel_row_count ||
        app->plugin_panel_rows[row_index].widget_serial != serial ||
        generation != app->panel_overlay_generation ||
        app->panel_overlay_stage_overflow || app->panel_overlay_stage_count == 0 )
        return 0;

    for( int i = 0; i < app->panel_overlay_count; i++ )
        if( app->panel_overlay_owner[i] != serial )
            other++;
    if( other + app->panel_overlay_stage_count > APP_PLUGIN_PANEL_OVERLAYS_MAX )
        return 0;

    /* Compact the other regions only after capacity and identity passed, so a
     * failed redraw leaves the last complete frame intact. */
    for( int i = 0; i < app->panel_overlay_count; i++ )
    {
        if( app->panel_overlay_owner[i] == serial )
            continue;
        if( out != i )
        {
            app->panel_overlays[out] = app->panel_overlays[i];
            app->panel_overlay_owner[out] = app->panel_overlay_owner[i];
            app->panel_overlay_row[out] = app->panel_overlay_row[i];
        }
        out++;
    }
    for( int i = 0; i < app->panel_overlay_stage_count; i++ )
    {
        app->panel_overlays[out] = app->panel_overlay_stage[i];
        app->panel_overlay_owner[out] = serial;
        app->panel_overlay_row[out] = row_index;
        out++;
    }
    app->panel_overlay_count = out;
    app_plugin_panel_overlay_bump(app);
    return 1;
}

/**
 * Draw every dirty custom well on the one active semantic page.
 *
 * Buffer geometry is read only after ToriRSChrome_Build; foreign presenters
 * supply their own content width. A size change invalidates that exact
 * semantic serial. Movement and clipping only reposition its retained run, so
 * an unchanged clean well does no plugin work while the page scrolls.
 */
static int
app_plugin_panel_draw_custom(struct App* app)
{
    uint32_t const generation = app->plugins
                                    ? PluginHost_PanelSelectionGeneration(app->plugins)
                                    : 0;
    int const active = app->plugins ? PluginHost_PanelActive(app->plugins) : -1;
    int changed = 0;
    int drew_this_pass = 0;

    assert(app);
    if( !app->plugins || !app->plugin_panel_visible || active < 0 ||
        active != g_plugin_page || generation == 0 ||
        generation != app->plugin_panel_built_generation )
    {
        app_plugin_panel_overlay_reset(app, 0);
        return 0;
    }
    if( app->panel_overlay_generation != generation )
        app_plugin_panel_overlay_reset(app, generation);

    for( int custom = 0; custom < app->plugin_panel_custom_row_count; custom++ )
    {
        int const row_index = app->plugin_panel_custom_rows[custom];
        struct AppPluginPanelRow* row = &app->plugin_panel_rows[row_index];
        struct ToriRSChromeRect region;
        struct ToriRSChromeRect clip;
        int size_changed;
        int position_changed;
        int clip_changed;
        int draw_geometry_changed;
        int external;
        int retained_moved = 0;
        unsigned layout_changes;
        int scale;
        int logical_w;
        int logical_h;

        assert(row->kind == APP_PLUGIN_ROW_PANEL_WIDGET);
        assert(row->widget_kind == TORIRS_PANEL_WIDGET_CUSTOM);
        assert(row->widget_serial != 0);

        external = app->plugin_exec_kind != TORIRS_CHROME_EXEC_BUFFER;
        if( external )
        {
            struct ToriRS_PanelWidget const* model =
                PluginHost_PanelWidgetAt(
                    app->plugins, generation, row->model_index);

            if( !app->plugin_rail_has_layout || !app->plugin_rail_layout.visible ||
                app->plugin_rail_layout.selection_generation !=
                    app->plugin_shell.selection_generation ||
                app->plugin_rail_layout.page_generation != generation || !model ||
                model->kind != TORIRS_PANEL_WIDGET_CUSTOM ||
                model->serial != row->widget_serial )
                continue;
            logical_w = app->plugin_rail_layout.custom_width;
            logical_h = model->preferred_height;
            if( logical_w <= 0 )
            {
                struct ToriRSChromeRect fallback_region;
                int const fallback_scale =
                    ToriRSChrome_Scale(&app->plugin_ui) > 0
                        ? ToriRSChrome_Scale(&app->plugin_ui)
                        : 1;

                /* Additive wire compatibility: an older cached page does not
                 * send customWidth. Prefer the hidden model's already-resolved
                 * content width when it has one; otherwise use the bounded
                 * pane allocation rather than leave the custom page blank.
                 * A later measured width changes the region and redraws it. */
                if( ToriRSChrome_CustomRegion(
                        &app->plugin_ui, row->widget, &fallback_region, NULL) )
                    logical_w =
                        (fallback_region.w + fallback_scale - 1) / fallback_scale;
                else
                    logical_w = app->plugin_rail_layout.width > TORIRS_PANEL_WIDTH_MAX
                                    ? TORIRS_PANEL_WIDTH_MAX
                                    : app->plugin_rail_layout.width;
            }
            if( logical_w <= 0 || logical_h <= 0 )
                continue;
            region = (struct ToriRSChromeRect){ 0, 0, logical_w, logical_h };
            clip = region;
            /* Browser/native presenters scale this logical 1x bitmap at their
             * own (possibly fractional) DPR. Keeping the raster logical avoids
             * coupling it to the hidden in-canvas model's integer scale. */
            scale = 1;
        }
        else
        {
            if( !ToriRSChrome_CustomRegion(
                    &app->plugin_ui, row->widget, &region, &clip) )
            {
                struct ToriRSChromeRect const none = { 0, 0, 0, 0 };

                /* A scrolled-out buffer well still owns a complete retained
                 * run. Hide it through the live clip, but keep the pixels so
                 * scrolling it back does not invoke the plugin again. */
                layout_changes = ToriRSChromePanelDraw_Changes(
                    row->custom_layout_valid,
                    row->custom_region,
                    row->custom_clip,
                    0,
                    none,
                    none);
                if( layout_changes & TORIRS_CHROME_PANEL_DRAW_CLIP )
                {
                    row->custom_clip = none;
                    app_plugin_panel_overlay_bump(app);
                    changed++;
                }
                continue;
            }
            scale = ToriRSChrome_Scale(&app->plugin_ui);
            if( scale <= 0 )
                scale = 1;
            /* The exact physical region remains the clip. Rounding the logical
             * callback box up lets IF3 cover an odd-sized final column/row;
             * the region clip trims the at-most-(scale-1) excess pixels. */
            logical_w = (region.w + scale - 1) / scale;
            logical_h = (region.h + scale - 1) / scale;
        }

        layout_changes = ToriRSChromePanelDraw_Changes(
            row->custom_layout_valid,
            row->custom_region,
            row->custom_clip,
            1,
            region,
            clip);
        size_changed = (layout_changes & TORIRS_CHROME_PANEL_DRAW_SIZE) != 0;
        position_changed = (layout_changes & TORIRS_CHROME_PANEL_DRAW_ORIGIN) != 0;
        clip_changed = (layout_changes & TORIRS_CHROME_PANEL_DRAW_CLIP) != 0;
        if( position_changed && !size_changed )
            retained_moved = app_plugin_panel_overlay_move(
                app,
                row->widget_serial,
                region.x - row->custom_region.x,
                region.y - row->custom_region.y);
        changed += retained_moved;
        /* Visible clipping is applied while retained runs are replayed. It is
         * paint damage, not new plugin content. */
        if( clip_changed && !size_changed && !retained_moved )
        {
            app_plugin_panel_overlay_bump(app);
            changed++;
        }
        row->custom_region = region;
        row->custom_clip = clip;
        row->custom_layout_valid = 1;
        draw_geometry_changed = size_changed;
        if( draw_geometry_changed )
            (void)PluginHost_PanelInvalidate(
                app->plugins, generation, row->widget_serial);
        if( !PluginHost_PanelNeedsDraw(
                app->plugins, generation, row->widget_serial) )
            continue;
        /* A draw callback may re-invalidate itself for animation. Cap that
         * loop to every other 20ms game cycle (25 Hz); geometry and the first
         * frame bypass the cap so resize/open never waits. */
        if( !draw_geometry_changed && app->panel_custom_has_draw_cycle &&
            app->logic_cycle < app->panel_custom_last_draw_cycle + 2 )
            continue;
        if( logical_w <= 0 || logical_h <= 0 )
            continue;

        app->panel_overlay_stage_count = 0;
        app->panel_overlay_stage_overflow = 0;
        app->panel_overlay_stage_active = 1;
        app->panel_overlay_origin_x = region.x;
        app->panel_overlay_origin_y = region.y;
        app->panel_overlay_scale = scale;
        /* Retain the complete region. Surface/buffer presentation intersects
         * this with the current scrolling clip; native custom Views receive
         * the full bitmap and let their own ScrollView clip it. */
        app->panel_overlay_clip = region;

        if( PluginHost_PanelDraw(
                app->plugins,
                generation,
                row->widget_serial,
                &app->panel_overlay_stage,
                0,
                0,
                logical_w,
                logical_h) &&
            PluginHost_PanelActive(app->plugins) == active &&
            PluginHost_PanelSelectionGeneration(app->plugins) == generation &&
            app_plugin_panel_overlay_commit(
                app, generation, row->widget_serial, row_index) )
        {
            ToriRSChrome_WidgetInvalidate(&app->plugin_ui, row->widget);
            app_plugin_panel_custom_pending_set(app, row, 1);
            changed++;
            drew_this_pass = 1;
        }
        app->panel_overlay_stage_active = 0;
        app->panel_overlay_stage_count = 0;
        app->panel_overlay_stage_overflow = 0;

        /* A draw callback may disable or replace itself. No later row from
         * the abandoned generation is eligible in this pass. */
        if( PluginHost_PanelActive(app->plugins) != active ||
            PluginHost_PanelSelectionGeneration(app->plugins) != generation )
        {
            app_plugin_panel_overlay_reset(app, 0);
            break;
        }
    }
    if( drew_this_pass )
    {
        app->panel_custom_last_draw_cycle = app->logic_cycle;
        app->panel_custom_has_draw_cycle = 1;
    }
    return changed;
}

/** Raster one retained serial into a region-local transparent bitmap. */
static int
app_plugin_panel_raster_custom(
    struct App* app,
    struct AppPluginPanelRow const* row,
    struct ToriRSChromeCustomFrame* out)
{
    struct UITreeEmitDesc desc;
    struct ToriRS_Frame frame;
    size_t pixels;
    int count = 0;

    assert(app);
    assert(row);
    assert(out);
    if( !app->scene || !app->soft_chrome || row->custom_region.w <= 0 ||
        row->custom_region.h <= 0 || row->custom_region.w > 4096 ||
        row->custom_region.h > 4096 )
        return 0;
    if( (size_t)row->custom_region.w > SIZE_MAX / (size_t)row->custom_region.h )
        return 0;
    pixels = (size_t)row->custom_region.w * (size_t)row->custom_region.h;
    if( pixels > SIZE_MAX / sizeof(*app->panel_custom_pixels) )
        return 0;
    if( pixels > app->panel_custom_pixel_capacity )
    {
        uint32_t* grown = realloc(
            app->panel_custom_pixels,
            pixels * sizeof(*app->panel_custom_pixels));
        if( !grown )
            return 0;
        app->panel_custom_pixels = grown;
        app->panel_custom_pixel_capacity = pixels;
    }
    memset(app->panel_custom_pixels, 0, pixels * sizeof(*app->panel_custom_pixels));

    /* Reuse the draw stage as a temporary local-coordinate list. No plugin
     * callback is open now, and the native sink copies before this function's
     * caller advances to the next region. */
    for( int i = 0; i < app->panel_overlay_count; i++ )
    {
        struct UITreeEntityOverlay* item;
        if( app->panel_overlay_owner[i] != row->widget_serial ||
            count >= APP_PLUGIN_PANEL_OVERLAYS_MAX )
            continue;
        item = &app->panel_overlay_stage[count++];
        *item = app->panel_overlays[i];
        item->x -= row->custom_region.x;
        item->y -= row->custom_region.y;
        item->clip_x -= row->custom_region.x;
        item->clip_y -= row->custom_region.y;
    }

    if( count > 0 )
    {
        memset(&desc, 0, sizeof(desc));
        desc.kind = UITREE_EMIT_ENTITY_OVERLAY;
        desc.entity_overlays = app->panel_overlay_stage;
        desc.entity_overlay_count = count;
        desc.clip.x = 0;
        desc.clip.y = 0;
        desc.clip.w = row->custom_region.w;
        desc.clip.h = row->custom_region.h;

        ToriRS_FrameInit(&frame);
        ToriRS_FrameSetScene(&frame, app->scene);
        ToriRS_FrameSetCanvas(
            &frame, row->custom_region.w, row->custom_region.h);
        ToriRS_FrameSetEmit(&frame, &desc, 1);
        ToriRS_Soft3D_Init(
            app->soft_chrome,
            app->scene,
            (int*)app->panel_custom_pixels,
            row->custom_region.w,
            row->custom_region.h);
        /*
         * Drained by hand, never ToriRS_Soft3D_RenderFrame: that call clears
         * the canvas to TORIRS_SOFT3D_BG (an OPAQUE 0xFF202428) first, which
         * overwrote the transparent memset above. Every gap a plugin leaves
         * clear on purpose -- the loot tracker's spacing between its bands --
         * then reached the page as a cold slate fill, and no stylesheet under
         * the well could show through an opaque pixel.
         */
        {
            struct ToriRS_RenderCommand cmd;
            ToriRS_FrameBegin(&frame);
            while( ToriRS_FrameNextCommand(&frame, &cmd) )
                ToriRS_Soft3D_Execute(app->soft_chrome, &cmd);
            ToriRS_FrameEnd(&frame);
        }
    }

    memset(out, 0, sizeof(*out));
    out->panel = app->plugin_panel;
    out->widget = row->widget;
    out->selection_generation = app->plugin_panel_built_generation;
    out->widget_serial = row->widget_serial;
    /* Only WEB/BROWSER consumes this frame. It is authored in logical pixels;
     * the presenter maps those CSS units through its own fractional DPR. */
    out->scale_milli = 1000;
    out->width = row->custom_region.w;
    out->height = row->custom_region.h;
    out->stride = row->custom_region.w;
    out->argb = app->panel_custom_pixels;
    return 1;
}

/** Publish dirty custom frames after their WIDGET_ADD transaction committed. */
static void
app_plugin_panel_present_custom(struct App* app)
{
    uint32_t generation;
    int active;

    assert(app);
    if( app->plugin_panel_custom_pending_count == 0 )
        return;
    if( !app->plugin_exec.exec.custom_present )
    {
        for( int i = 0; i < app->plugin_panel_custom_row_count; i++ )
        {
            int const row = app->plugin_panel_custom_rows[i];
            app->plugin_panel_rows[row].custom_present_pending = 0;
        }
        app->plugin_panel_custom_pending_count = 0;
        return;
    }
    generation = PluginHost_PanelSelectionGeneration(app->plugins);
    active = PluginHost_PanelActive(app->plugins);
    if( generation == 0 || generation != app->plugin_panel_built_generation ||
        active < 0 || active != g_plugin_page )
        return;

    for( int custom = 0; custom < app->plugin_panel_custom_row_count; custom++ )
    {
        int const row_index = app->plugin_panel_custom_rows[custom];
        struct AppPluginPanelRow* row = &app->plugin_panel_rows[row_index];
        struct ToriRSChromeCustomFrame frame;

        if( !row->custom_present_pending )
            continue;
        if( !row->custom_layout_valid ||
            row->widget_kind != TORIRS_PANEL_WIDGET_CUSTOM )
        {
            app_plugin_panel_custom_pending_set(app, row, 0);
            continue;
        }
        if( !app_plugin_panel_raster_custom(app, row, &frame) )
            continue;
        if( app->plugin_exec.exec.custom_present(
                app->plugin_exec.exec.user, &frame) )
            app_plugin_panel_custom_pending_set(app, row, 0);
    }
}

/** Publish the selected semantic page's allocation before any input uses it. */
static int
app_plugin_panel_publish_layout(struct App* app)
{
    struct ToriRSChromeRect rect;
    uint32_t generation;
    int active;
    int scale;
    int width;
    int height;
    int size_class;
    int game_visible;

    assert(app);
    if( !app->plugins || app->plugin_panel < 0 || !app->plugin_panel_visible ||
        !app->plugin_ui.panels[app->plugin_panel].visible )
        return 0;
    active = PluginHost_PanelActive(app->plugins);
    generation = PluginHost_PanelSelectionGeneration(app->plugins);
    if( active < 0 || active != g_plugin_page || generation == 0 )
        return 0;

    /* Attached Android/DOM presenters own their allocation. In compact mode
     * they replace the game rather than merely drawing a floating model panel,
     * so their neutral report wins over the in-canvas geometry below. */
    if( app->plugin_exec_kind != TORIRS_CHROME_EXEC_BUFFER )
    {
        struct ToriRSChromeRailIntent const* layout = &app->plugin_rail_layout;
        if( !app->plugin_rail_has_layout ||
            layout->selection_generation != app->plugin_shell.selection_generation ||
            layout->page_generation != generation || !layout->visible ||
            layout->width <= 0 || layout->height <= 0 )
            return 0;
        return PluginHost_PanelLayout(
            app->plugins,
            generation,
            layout->width,
            layout->height,
            layout->scale_milli,
            layout->size_class,
            true,
            layout->game_visible != 0);
    }

    rect = ToriRSChrome_PanelRect(&app->plugin_ui, app->plugin_panel);
    scale = ToriRSChrome_Scale(&app->plugin_ui);
    if( scale <= 0 )
        scale = 1;
    /* PanelRect is executor/canvas pixels; the plugin contract is logical
     * chrome units plus an explicit scale, so a Retina lane does not look
     * twice as wide to the plugin. */
    width = rect.w / scale;
    height = rect.h / scale;
    if( width <= 0 || height <= 0 )
        return 0;

    if( width < TORIRS_PANEL_WIDTH_DEFAULT )
        size_class = TORIRS_PANEL_SIZE_COMPACT;
    else if( width >= TORIRS_PANEL_WIDTH_MAX )
        size_class = TORIRS_PANEL_SIZE_EXPANDED;
    else
        size_class = TORIRS_PANEL_SIZE_MEDIUM;

    /* The ordinary floating/attached panel leaves the game visible. The
     * phone/fullscreen presentation replaces the canvas and tells animated
     * plugins to stand down from duplicate game work. */
    game_visible = !(g_plugin_fullscreen &&
                     app->plugin_exec_kind == TORIRS_CHROME_EXEC_BUFFER);
    /*
     * Where the page is scrolled to, and any request to move it.
     *
     * Here rather than in the sync, because this is the function that already
     * knows the selection generation both halves are fenced on -- and the
     * publish happens BEFORE the take so a plugin reading `scroll` inside the
     * callback its own scroll_to came from sees the place it was, not a
     * half-applied one.
     */
    {
        int wanted = 0;
        PluginHost_PanelSetScroll(
            app->plugins, generation,
            ToriRSChrome_PanelScroll(&app->plugin_ui, app->plugin_panel) / scale);
        if( PluginHost_PanelTakeScrollRequest(app->plugins, generation, &wanted) )
            ToriRSChrome_PanelSetScroll(
                &app->plugin_ui, app->plugin_panel, wanted * scale);
    }
    return PluginHost_PanelLayout(
        app->plugins,
        generation,
        width,
        height,
        scale * 1000,
        size_class,
        true,
        game_visible != 0);
}

/*
 * Per-frame: the toggle, the lazy build, input, and one activation.
 *
 * Runs beside the other chrome ticks at the top of App_RunOnce, before the
 * BOOTING early-out, so the window is usable while a cache is still loading --
 * which is exactly when someone wants to switch a misbehaving plugin off.
 */
void
app_plugin_panel_tick(struct App* app, struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    g_plugin_panel_ticks++;
#if defined(TORIRS_PLATFORM_WEB)
    /* Like Android's Activity rail, the DOM rail deliberately survives
     * executor shutdown. Its exported request latch is therefore drained by
     * the always-running shell tick rather than by the executor that is absent
     * precisely while the pane is collapsed. */
    {
        int expanded = 0;
        if( ToriRSChromeExecWeb_TakeOpenRequest(&expanded) )
            app_plugin_window_set_open(app, expanded != 0);
    }
#endif
    if( !app->plugins )
        return;

    /* Navigation is application chrome, not page chrome: publish and drain it
     * even while plugin_exec is shut down in the collapsed state. The pop-out
     * column goes first, because whether it carries the destinations is what
     * the rail snapshot's `rail_hidden` reports. */
    app_plugin_popout_nav_tick(app);
    app_plugin_rail_publish(app);
    app_plugin_rail_drain(app);
    app_plugin_rail_publish(app);

    /*
     * Headless drive: open the window on a named plugin's named FACE.
     *
     * `TORIRS_SIM_PLUGIN_PANEL="<tick>,<plugin-name>,<page|settings>;..."`.
     *
     * This exists because the bug it was written for -- a page boundary
     * leaving a blank pane -- is invisible to every unit test in the tree and
     * was only ever reported from a running client. Switching pages is a
     * GESTURE, and the state it moves through spans the plugin host, the
     * retained chrome, the sync shadow and an executor's own page; nothing
     * below the whole application exercises that path. The hotkey and the rail
     * both need input this harness cannot deliver, so the drive goes in beside
     * them and calls exactly what they call.
     */
    {
        static int sim_panel_init = 0;
        static char const* sim_panel_cursor = NULL;
        static long sim_panel_tick = -1;
        static char sim_panel_name[TORIRS_PLUGIN_NAME_MAX];
        static int sim_panel_view = APP_PLUGIN_VIEW_PAGE;

        if( !sim_panel_init )
        {
            sim_panel_init = 1;
            sim_panel_cursor = getenv("TORIRS_SIM_PLUGIN_PANEL");
        }
        if( sim_panel_tick < 0 && sim_panel_cursor && *sim_panel_cursor )
        {
            char* end = NULL;
            long const at = strtol(sim_panel_cursor, &end, 0);
            if( end && *end == ',' )
            {
                char const* start = end + 1;
                size_t len = 0;
                while( start[len] && start[len] != ',' && start[len] != ';' )
                    len++;
                if( len >= sizeof(sim_panel_name) )
                    len = sizeof(sim_panel_name) - 1;
                memcpy(sim_panel_name, start, len);
                sim_panel_name[len] = '\0';
                start += len;
                sim_panel_view = APP_PLUGIN_VIEW_PAGE;
                if( *start == ',' )
                {
                    start++;
                    if( strncmp(start, "settings", 8) == 0 )
                        sim_panel_view = APP_PLUGIN_VIEW_SETTINGS;
                    while( *start && *start != ';' )
                        start++;
                }
                sim_panel_cursor = *start == ';' ? start + 1 : NULL;
                sim_panel_tick = at;
            }
            else
                sim_panel_cursor = NULL;
        }
        if( sim_panel_tick >= 0 && g_plugin_panel_ticks >= sim_panel_tick )
        {
            int const index = PluginHost_IndexOf(app->plugins, sim_panel_name);
            sim_panel_tick = -1;
            if( index >= 0 )
            {
                fprintf(
                    stderr, "chrome: sim panel '%s' -> %s (tick %d)\n", sim_panel_name,
                    sim_panel_view == APP_PLUGIN_VIEW_PAGE ? "page" : "settings",
                    g_plugin_panel_ticks);
                if( sim_panel_view == APP_PLUGIN_VIEW_PAGE )
                    /* Exercise the exact application mutation used after a
                     * queued rail.select, including PAGE-vs-SETTINGS repair. */
                    app_plugin_rail_select(app, index);
                else
                {
                    app_plugin_page_select(app, index, sim_panel_view);
                    if( !app->plugin_panel_visible )
                        app_plugin_window_set_open(app, 1);
                }
            }
            else
                fprintf(stderr, "chrome: sim panel: no plugin '%s'\n", sim_panel_name);
        }
    }

    /*
     * Headless drive, second half: pick one value in one plugin page control.
     *
     * `TORIRS_SIM_PANEL_PICK="<tick>,<plugin-name>,<widget-id>,<value>;..."`.
     *
     * Ends in app_plugin_panel_dispatch_row exactly as a dropdown pick from
     * any executor does, so the plugin's on_ui_action, its native write and
     * the page republish are the real ones; only the executor's own dropdown
     * hit-test is skipped, and that is measured separately by a real click.
     */
    {
        static int sim_pick_init = 0;
        static char const* sim_pick_cursor = NULL;
        static long sim_pick_tick = -1;
        static char sim_pick_plugin[TORIRS_PLUGIN_NAME_MAX];
        static char sim_pick_widget[TORIRS_PLUGIN_WIDGET_ID_MAX];
        static char sim_pick_value[TORIRS_PLUGIN_SELECT_VALUE_MAX];

        if( !sim_pick_init )
        {
            sim_pick_init = 1;
            sim_pick_cursor = getenv("TORIRS_SIM_PANEL_PICK");
        }
        if( sim_pick_tick < 0 && sim_pick_cursor && *sim_pick_cursor )
        {
            char* end = NULL;
            long const at = strtol(sim_pick_cursor, &end, 0);
            char const* fields[3] = { sim_pick_plugin, sim_pick_widget, sim_pick_value };
            size_t sizes[3] = { sizeof(sim_pick_plugin), sizeof(sim_pick_widget), sizeof(sim_pick_value) };
            int ok = end && *end == ',';
            for( int f = 0; ok && f < 3; f++ )
            {
                char const* start = end + 1;
                char const* stop = strpbrk(start, f == 2 ? ";" : ",");
                size_t len = stop ? (size_t)(stop - start) : strlen(start);
                if( len >= sizes[f] || (f < 2 && !stop) )
                    ok = 0;
                else
                {
                    memcpy((char*)fields[f], start, len);
                    ((char*)fields[f])[len] = '\0';
                    end = (char*)(stop ? stop : start + len);
                }
            }
            if( ok )
            {
                sim_pick_tick = at;
                sim_pick_cursor = *end == ';' ? end + 1 : NULL;
            }
            else
            {
                fprintf(stderr, "chrome: sim pick: malformed TORIRS_SIM_PANEL_PICK\n");
                sim_pick_cursor = NULL;
            }
        }
        if( sim_pick_tick >= 0 && g_plugin_panel_ticks >= sim_pick_tick )
        {
            int dispatched = 0;
            int found = 0;
            sim_pick_tick = -1;
            for( int i = 0; i < app->plugin_panel_row_count && !found; i++ )
            {
                struct AppPluginPanelRow const* row = &app->plugin_panel_rows[i];
                struct ToriRS_PanelWidget const* model;
                int value = -1;
                if( row->kind != APP_PLUGIN_ROW_PANEL_WIDGET || row->plugin < 0 ||
                    strcmp(PluginHost_Name(app->plugins, row->plugin), sim_pick_plugin) != 0 ||
                    strcmp(row->widget_id, sim_pick_widget) != 0 )
                    continue;
                model = app_plugin_panel_model_for_row(app, row);
                if( !model )
                    continue;
                found = 1;
                for( int o = 0; model->structured_select && o < model->select_option_count; o++ )
                    if( strcmp(model->select_options[o].value, sim_pick_value) == 0 )
                        value = o;
                /* 0 here is the host's own fence refusing the intent (a value
                 * the row does not offer, a stale serial); the plugin never
                 * saw it. Said as such, not as a missing row. The literal
                 * value "!activate" presses a button or action row instead of
                 * picking, through the same dispatch. */
                dispatched = strcmp(sim_pick_value, "!activate") == 0
                    ? app_plugin_panel_dispatch_row(
                          app, row, TORIRS_PANEL_ACTION_ACTIVATE, 0, "", 0, 0)
                    : app_plugin_panel_dispatch_row(
                          app, row, TORIRS_PANEL_ACTION_PICK, value, sim_pick_value, 0, 0);
                fprintf(stderr, "chrome: sim pick '%s' %s = '%s' (option %d) -> %s (tick %d)\n",
                    sim_pick_plugin, sim_pick_widget, sim_pick_value, value,
                    dispatched ? "dispatched" : "refused by the host fence", g_plugin_panel_ticks);
            }
            if( !found )
                fprintf(stderr, "chrome: sim pick: no row '%s' in '%s'\n", sim_pick_widget, sim_pick_plugin);
        }
    }

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
            if( getenv("TORIRS_CHROME_DEBUG") )
                fprintf(stderr, "chrome: hotkey toggle tick=%d\n", g_plugin_panel_ticks);
            app_plugin_window_set_open(app, !app->plugin_panel_visible);
        }
        toggle_was_down = down;
    }

    if( !app->plugin_panel_visible )
    {
        app_plugin_panel_overlay_reset(app, 0);
        return;
    }

    app_plugin_exec_bind(app);

    /* Scripts register asynchronously, so the list can still be growing the
     * first few frames the window is open. */
    app_plugin_panel_sync(app);

    /*
     * Lay the window out and produce its display list.
     *
     * The IN-CANVAS executor is the one that needs this, and it is the reason
     * ToriRSChromeExec_Buffer's apply() is deliberately empty: "the model draws
     * itself through ToriRSChrome_Build/Prims". Nothing was calling Build on
     * plugin_ui, so the model never drew itself -- Prims handed back a count of
     * zero, app_chrome_merged_prims took its `win_count == 0` early-out, and
     * the window opened, bound its executor and rendered nothing at all.
     *
     * BUFFER remains the internal fallback on a lane without WEB/BROWSER, so
     * this retained display list must stay valid even though normal desktop
     * plugin chrome is projected through a web executor.
     *
     * Unconditional rather than gated on the bound executor: Build returns 0
     * and does no work on a frame where nothing moved, and a display list that
     * is always current is what lets the merge stay a pointer copy.
    */
    ToriRSChrome_Build(&app->plugin_ui);

    /* Layout is a lifecycle input to the selected plugin, so publish it before
     * either pointer path can dispatch a control. A layout callback may update
     * retained values (or request a rebuild); fold that revision back into the
     * same frame before exposing controls to input. */
    (void)app_plugin_panel_publish_layout(app);
    if( app->plugin_panel_built_model_rev !=
            PluginHost_PanelModelRevision(app->plugins) ||
        app->plugin_panel_built_generation !=
            PluginHost_PanelSelectionGeneration(app->plugins) )
    {
        app_plugin_panel_sync(app);
        ToriRSChrome_Build(&app->plugin_ui);
        (void)app_plugin_panel_publish_layout(app);
    }

    if( ToriRSChrome_HasVisiblePanel(&app->plugin_ui) )
    {
        /* Its own instance, so its own routing -- and no ownership dance with
         * the developer chrome's activation latch, because it no longer shares
         * one. That shared latch is what made the old panel peek before taking,
         * and two panels could still eat each other's clicks.
         *
         * A web executor gets the KEYBOARD only: its clicks arrive as semantic
         * DOM intents, and routing
         * the mouse here as well would hand them to the in-canvas window's
         * ghost -- laid out and hit-testable at its floating position even
         * though nothing draws it. The keyboard must still flow, because the
         * text fields being typed into are the model's. */
        if( app->plugin_exec_kind == TORIRS_CHROME_EXEC_BUFFER )
            app_chrome_route_input(app, &app->plugin_ui, input);
        else
            app_chrome_route_keys(app, &app->plugin_ui, input);
    }

    /* Intents from a web executor land on the model the same way a
     * click would, so the drain below sees both without knowing which. */
    ToriRSChromeSync_Pump(&app->plugin_exec, &app->plugin_ui);

    {
        int const activated = ToriRSChrome_TakeActivated(&app->plugin_ui);
        if( activated >= 0 )
        {
            app_plugin_panel_apply(app, activated);
            /* A semantic action can synchronously change siblings, clear and
             * rebuild its page, or unregister itself. Present that
             * authoritative result in this commit rather than one frame later. */
            if( app->plugin_panel_visible )
                app_plugin_panel_sync(app);
        }
    }
    if( app->plugin_panel_visible && app_plugin_panel_reconcile_semantic(app) > 0 )
        app_plugin_panel_sync(app);

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
    {
        int built = ToriRSChrome_Build(&app->plugin_ui);

        /* Build resolves each custom well's full box and visible clip. Draw
         * only after those exist, then rebuild once if new retained pixels
         * damaged a well. A clean steady page does neither operation. */
        if( app_plugin_panel_draw_custom(app) )
            built |= ToriRSChrome_Build(&app->plugin_ui);
        if( built )
        {
            app->need_redraw = 1;
            ToriRSChrome_DamageClear(&app->plugin_ui);
        }
    }

    /*
     * Drain the retained change queue into the bound web executor. BUFFER has
     * an empty sink because its authoritative model is drawn in-canvas.
     *
     * A quiet frame is one count test: no transaction and no model/shadow scan.
     */
    ToriRSChromeSync_Run(&app->plugin_exec, &app->plugin_ui);
    if( app->plugin_exec.last_run_restate )
        for( int i = 0; i < app->plugin_panel_custom_row_count; i++ )
        {
            struct AppPluginPanelRow* row =
                &app->plugin_panel_rows[app->plugin_panel_custom_rows[i]];
            if( row->custom_layout_valid )
                app_plugin_panel_custom_pending_set(app, row, 1);
        }

    /* Web custom canvases are created by the sync above. Publish the
     * pixels afterwards so an asynchronous UI queue cannot receive a frame
     * before the WIDGET_ADD that gives it an identity and destination. */
    app_plugin_panel_present_custom(app);
}
