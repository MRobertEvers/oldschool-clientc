/*
 * Plugin navigation inside the lane's own pop-out launcher column.
 *
 * Included by app/app_plugin_panel.c after torirs_plugin_panel.u.c, whose rail
 * snapshot and rail select this reuses: a button here is the same destination
 * a rail stone is, and pressing it runs the same route.
 *
 * WHEN. The profile names the column (`[role:plugin_nav_column]`), the column
 * is on screen, the mode is `auto`, and at least one button fits. Otherwise
 * every engine button is removed and the rail carries the destinations as it
 * always has -- which is the whole of the rs289 lane, whose profile names no
 * such column.
 *
 * HOW, WITHOUT BREAKING THE CACHE'S BUTTONS. Each button is an owned child of
 * the column (component id -1, an engine owner token). Script 5356 rebuilds
 * the lane's buttons with `cc_create` on every layout pass; that replaces only
 * dynamic children and its sub-id lookup skips owned ones, so neither side
 * can take the other's node. An owned node has no (parent, sub-id) identity,
 * so a click on one can never be sent to the server as `IF_BUTTON1` on
 * `popout:buttons`: it is answered here, before any server op is built.
 *
 * A remount of the pop-out interface frees the owned children with it. The
 * reference then resolves to nothing, the next tick creates the node again,
 * and every property is re-applied because the incarnation changed.
 *
 * WHAT IT LOOKS LIKE. The lane's own size and pitch, one pitch below the last
 * lane button, the lane's own hover dim, and the Tori face badge in the corner
 * of every one so no engine button can pass for a cache one.
 */

#include "ui/torirs_chrome_popout_nav.h"

/** The profile role naming the lane's launcher column. */
#define APP_PLUGIN_NAV_ROLE "plugin_nav_column"
/**
 * Panel ticks in-game with a declared column still unusable before the rail
 * comes back. Login mounts the gameframe first and the pop-out a few ticks
 * later; three seconds is that with room, and short enough that a column the
 * player switched off (or the mobile toplevel, which mounts it hidden) hands
 * the destinations back to the rail promptly.
 */
#define APP_PLUGIN_NAV_UNUSABLE_GRACE_TICKS 150
/** script5356's `cc_setonmouserepeat("cc_settrans(..., 128, -1)")`. */
#define APP_PLUGIN_NAV_HOVER_TRANSPARENCY 128

/**
 * The column's node when the engine may use it, or -1.
 *
 * On screen means natively visible all the way up AND under a displayable
 * root: a CS2 script that reads a property of 728 before it is placed mounts it
 * as an orphan root, and a button hung in that would be invisible and
 * unclickable while the rail had already stood down for it.
 */
static int32_t
app_plugin_popout_nav_column(struct App* app)
{
    int32_t column;
    int32_t root;

    assert(app);
    if( app->plugin_nav.mode != TORIRS_PLUGIN_NAV_AUTO )
        return -1;
    if( !app->tree || app->tree->component_count == 0 )
        return -1;
    column = UITree_RoleNodeByName(app->tree, &app->ui_roles, APP_PLUGIN_NAV_ROLE);
    if( column < 0 )
        return -1;
    for( root = column; app->tree->components[root].parent >= 0;
         root = app->tree->components[root].parent )
        ;
    if( !UITree_RootIsDisplayable(app->tree, root) )
        return -1;
    if( !UITree_NodeNativeVisible(app->tree, &app->ui_host, column, -1) )
        return -1;
    return column;
}

/** The bottom edge of the lowest shown lane button, column-local, or -1. */
static int
app_plugin_popout_nav_native_bottom(struct App* app, int32_t column)
{
    struct UITreeComponent const* parent = &app->tree->components[column];
    int bottom = -1;

    for( int32_t child = parent->first_child; child >= 0;
         child = app->tree->components[child].next_sibling )
    {
        struct UITreeComponent const* c = &app->tree->components[child];
        int edge;
        if( c->plugin_owner )
            continue;
        if( c->position.abs_w <= 0 || c->position.abs_h <= 0 )
            continue;
        if( !UITree_NodeNativeVisible(app->tree, &app->ui_host, child, -1) )
            continue;
        edge = c->position.abs_y - parent->position.abs_y + c->position.abs_h;
        if( edge > bottom )
            bottom = edge;
    }
    return bottom;
}

static void
app_plugin_popout_nav_remove(struct App* app, struct AppPluginPopoutNavButton* button)
{
    if( app->tree && UITree_ResolveRef(app->tree, button->ref) >= 0 )
        (void)UITree_WidgetRemove(app->tree, button->ref, APP_PLUGIN_NAV_OWNER);
}

static void
app_plugin_popout_nav_clear(struct App* app)
{
    struct AppPluginPopoutNav* nav = &app->plugin_nav;
    for( int i = 0; i < nav->button_count; i++ )
        app_plugin_popout_nav_remove(app, &nav->buttons[i]);
    nav->button_count = 0;
}

static struct AppPluginPopoutNavButton*
app_plugin_popout_nav_button_for(struct App* app, int destination)
{
    struct AppPluginPopoutNav* nav = &app->plugin_nav;
    struct AppPluginPopoutNavButton* button;
    int slot = 0;

    for( int i = 0; i < nav->button_count; i++ )
        if( nav->buttons[i].destination == destination )
            return &nav->buttons[i];

    /* The lowest scene slot no live button holds. */
    for( ;; slot++ )
    {
        int taken = 0;
        for( int i = 0; i < nav->button_count; i++ )
            if( nav->buttons[i].scene_slot == slot )
                taken = 1;
        if( !taken )
            break;
    }
    assert(nav->button_count < TORIRS_CHROME_RAIL_ENTRY_MAX);
    assert(slot < UITREE_SCENE_PLUGIN_NAV_BUTTON_SLOTS);
    button = &nav->buttons[nav->button_count++];
    memset(button, 0, sizeof(*button));
    button->destination = destination;
    button->ref.index = -1;
    button->scene_slot = slot;
    button->y = -1;
    button->transparency = -1;
    return button;
}

/** Compose and publish the button's picture when its icon changed. */
static int
app_plugin_popout_nav_picture(
    struct App* app, struct AppPluginPopoutNavButton* button)
{
    static uint32_t icon[TORIRS_CHROME_RAIL_ICON_PIXELS_MAX];
    uint32_t picture[TORIRS_POPOUT_NAV_BUTTON * TORIRS_POPOUT_NAV_BUTTON];
    uint32_t revision = 1;
    int width = 0;
    int height = 0;
    int have_icon = 0;

    if( button->destination >= 0 )
    {
        revision = PluginHost_PanelIconRevision(app->plugins, button->destination);
        if( revision != 0 )
            have_icon = PluginHost_PanelIconPixels(
                            app->plugins, button->destination, icon,
                            TORIRS_CHROME_RAIL_ICON_PIXELS_MAX, &width, &height) &&
                        width > 0 && height > 0;
    }
    if( button->composed && button->icon_revision == revision )
        return 0;

    if( have_icon )
        ToriRSPopoutNav_ComposeButton(icon, width, height, picture);
    else
    {
        /* The rail's own fallback for a destination without art, and the
         * Manage Plugins picture. */
        struct ToriRSChromeSkin_Sprite const* wrench =
            ToriRSChromeSkin_Get(ToriRSChromeSkin_SLOT_PluginIcon);
        assert(wrench);
        ToriRSPopoutNav_ComposeButton(wrench->argb, wrench->w, wrench->h, picture);
    }
    (void)UITreeSceneBridge_PublishNavButton(
        &app->bridge, button->scene_slot, TORIRS_POPOUT_NAV_BUTTON,
        TORIRS_POPOUT_NAV_BUTTON, picture);
    button->icon_revision = revision;
    button->composed = 1;
    return 1;
}

static void
app_plugin_popout_nav_sync_button(
    struct App* app,
    int32_t column,
    struct AppPluginPopoutNavButton* button,
    struct ToriRSChromeRailSnapshot const* snapshot,
    int y)
{
    struct AppPluginPopoutNav* nav = &app->plugin_nav;
    char key[32];
    char label[UITREE_MENU_OPTION_LEN];
    char const* title;
    int32_t node;
    int transparency;
    int const open =
        app->plugin_panel_visible && snapshot->selected_entry == button->destination;

    snprintf(key, sizeof(key), "torirs-nav:%d", button->destination);
    node = UITree_WidgetCreateGraphic(
        app->tree, UITree_RefAt(app->tree, column), APP_PLUGIN_NAV_OWNER, key);
    /* At most TORIRS_CHROME_RAIL_ENTRY_MAX owned nodes under a column that
     * resolved this tick: neither the owner cap nor the parent can refuse. */
    assert(node >= 0);
    if( UITree_ResolveRef(app->tree, button->ref) != node )
    {
        button->ref = UITree_RefAt(app->tree, node);
        button->graphic_set = 0;
        button->y = -1;
        button->transparency = -1;
        button->label[0] = '\0';
    }

    if( app_plugin_popout_nav_picture(app, button) || !button->graphic_set )
    {
        (void)UITree_WidgetSetGraphic(
            app->tree, button->ref, APP_PLUGIN_NAV_OWNER,
            UITREE_SCENE_PLUGIN_NAV_BUTTON_BASE + button->scene_slot,
            TORIRS_POPOUT_NAV_BUTTON, TORIRS_POPOUT_NAV_BUTTON);
        button->graphic_set = 1;
        app->need_redraw = 1;
    }

    if( button->y != y )
    {
        (void)UITree_WidgetSetPosition(app->tree, button->ref, APP_PLUGIN_NAV_OWNER, 0, y);
        button->y = y;
    }

    /* The lane's own row reads "Open <col=ff9040>XP Tracker</col>", and
     * "Close" on the panel that is up. */
    title = button->destination >= 0
                ? PluginHost_PanelTitle(app->plugins, button->destination)
                : "Manage Plugins";
    snprintf(
        label, sizeof(label), "%s <col=ff9040>%.32s</col>", open ? "Close" : "Open",
        title ? title : "");
    if( strcmp(label, button->label) != 0 )
    {
        (void)UITree_WidgetSetOperation(
            app->tree, button->ref, APP_PLUGIN_NAV_OWNER, nav->op_serial, 1, label);
        memcpy(button->label, label, sizeof(button->label));
    }

    transparency =
        app->interact.hover_node_index == node ? APP_PLUGIN_NAV_HOVER_TRANSPARENCY : 0;
    if( button->transparency != transparency )
    {
        (void)UITree_WidgetSetTransparency(
            app->tree, button->ref, APP_PLUGIN_NAV_OWNER, transparency);
        button->transparency = transparency;
        app->need_redraw = 1;
    }
}

/** Whether the rail stands down this tick. @see AppPluginPopoutNav::rail_hidden */
static void
app_plugin_popout_nav_decide_rail(struct App* app)
{
    struct AppPluginPopoutNav* nav = &app->plugin_nav;
    int const declared =
        nav->mode == TORIRS_PLUGIN_NAV_AUTO &&
        UITree_RoleFind(&app->ui_roles, APP_PLUGIN_NAV_ROLE) != 0;

    if( nav->active || !declared )
    {
        nav->unusable_ticks = 0;
        nav->rail_hidden = nav->active;
        return;
    }
    if( app->screen != APP_SCREEN_GAME )
    {
        nav->unusable_ticks = 0;
        nav->rail_hidden = 1;
        return;
    }
    if( nav->unusable_ticks < APP_PLUGIN_NAV_UNUSABLE_GRACE_TICKS )
        nav->unusable_ticks++;
    nav->rail_hidden = nav->unusable_ticks < APP_PLUGIN_NAV_UNUSABLE_GRACE_TICKS;
}

static void
app_plugin_popout_nav_trace(
    struct App* app, int32_t column, int first_y, int capacity, int wanted)
{
    struct AppPluginPopoutNav* nav = &app->plugin_nav;
    if( nav->reported_active == nav->active && nav->reported_first_y == first_y &&
        nav->reported_capacity == capacity && nav->reported_count == nav->button_count &&
        nav->reported_rail_hidden == nav->rail_hidden )
        return;
    nav->reported_rail_hidden = nav->rail_hidden;
    nav->reported_active = nav->active;
    nav->reported_first_y = first_y;
    nav->reported_capacity = capacity;
    nav->reported_count = nav->button_count;
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr,
            "chrome: plugin nav mode=%s column=%d active=%d first_y=%d capacity=%d "
            "buttons=%d destinations=%d rail_hidden=%d\n",
            ToriRSPluginNav_ModeName(nav->mode), (int)column, nav->active, first_y,
            capacity, nav->button_count, wanted, nav->rail_hidden);
}

/**
 * Reconcile the column's engine buttons with the rail destinations. Runs once
 * per panel tick, before the rail is published, so the snapshot's
 * `rail_hidden` is this tick's answer.
 */
static void
app_plugin_popout_nav_tick(struct App* app)
{
    struct AppPluginPopoutNav* nav = &app->plugin_nav;
    struct ToriRSChromeRailSnapshot snapshot;
    int32_t const column = app_plugin_popout_nav_column(app);
    int first_y = -1;
    int capacity = 0;
    int shown;

    assert(app);
    assert(app->plugins);
    if( nav->op_serial == 0 )
        nav->op_serial = 1;

    if( column >= 0 )
    {
        first_y = ToriRSPopoutNav_FirstY(app_plugin_popout_nav_native_bottom(app, column));
        capacity = ToriRSPopoutNav_Capacity(
            first_y, app->tree->components[column].position.abs_h);
    }
    if( column < 0 || capacity <= 0 )
    {
        app_plugin_popout_nav_clear(app);
        nav->active = 0;
        app_plugin_popout_nav_decide_rail(app);
        app_plugin_popout_nav_trace(app, column, first_y, capacity, 0);
        return;
    }

    nav->active = 0;
    app_plugin_rail_snapshot(app, &snapshot);
    /* Manage Plugins is the snapshot's first entry, so a short column keeps
     * the way to every destination that did not fit. */
    shown = snapshot.entry_count < capacity ? snapshot.entry_count : capacity;

    for( int i = 0; i < nav->button_count; i++ )
        nav->buttons[i].seen = 0;
    for( int i = 0; i < shown; i++ )
    {
        struct AppPluginPopoutNavButton* button =
            app_plugin_popout_nav_button_for(app, snapshot.entries[i].plugin_index);
        button->seen = 1;
        app_plugin_popout_nav_sync_button(
            app, column, button, &snapshot, first_y + i * TORIRS_POPOUT_NAV_PITCH);
    }
    for( int i = 0; i < nav->button_count; )
    {
        if( nav->buttons[i].seen )
        {
            i++;
            continue;
        }
        app_plugin_popout_nav_remove(app, &nav->buttons[i]);
        nav->buttons[i] = nav->buttons[--nav->button_count];
    }
    nav->active = 1;
    app_plugin_popout_nav_decide_rail(app);
    app_plugin_popout_nav_trace(app, column, first_y, capacity, snapshot.entry_count);
}

int
app_plugin_popout_nav_click(struct App* app, int32_t node)
{
    struct AppPluginPopoutNav* nav;

    assert(app);
    assert(app->tree);
    nav = &app->plugin_nav;
    if( !app->plugins )
        return 0;
    for( int i = 0; i < nav->button_count; i++ )
    {
        if( UITree_ResolveRef(app->tree, nav->buttons[i].ref) != node )
            continue;
        if( getenv("TORIRS_CHROME_DEBUG") )
            fprintf(
                stderr, "chrome: plugin nav click destination=%d tick=%d\n",
                nav->buttons[i].destination, g_plugin_panel_ticks);
        app_plugin_rail_select(app, nav->buttons[i].destination);
        return 1;
    }
    return 0;
}
