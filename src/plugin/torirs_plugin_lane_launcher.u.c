/*
 * The plugin launcher on a lane's own stone column.
 *
 * Included by app/app_plugin_panel.c after torirs_plugin_popout_nav.u.c, whose
 * rail select this reuses: pressing this button is pressing the rail's Manage
 * Plugins stone, and the route decides open/close exactly as it does there.
 *
 * WHEN. The profile names an anchor (`[role:plugin_launcher_anchor]`) and the
 * column that anchor lives in is on screen. Only the OSRS239 MOBILE toplevel
 * names one, because it is the only root where nothing else offers the plugin
 * window -- @see ui/torirs_chrome_lane_launcher.h for the other three
 * launchers and why each of them stands down there.
 *
 * HOW, WITHOUT BREAKING THE CACHE'S BUTTONS. The button is an owned child
 * (component id -1, an engine owner token), so it has no (parent, sub-id)
 * identity and a click on it can never be sent to the server as an IF_BUTTON1
 * on whatever it was hung under. A remount frees it with its parent; the
 * reference then resolves to nothing, the next tick creates it again, and
 * every property is re-stated because the incarnation changed.
 *
 * WHERE IT HANGS. NOT under the anchor's own parent, which is the thing that
 * would read best: an interface layer clips its children to its own box
 * (UITree_LayerChildClip), and the stone column is exactly as tall as its
 * stones -- a button one pitch below the last of them is a button one pitch
 * outside the column, and the cache scripts resize that column on every chat
 * toggle. So the mount is the nearest ancestor the button actually fits
 * inside, and `UITree_WidgetMoveAfter` puts it straight after the column in
 * that ancestor's child order, so it draws where the column draws and a
 * fullscreen bank still covers it.
 */

#include "ui/torirs_chrome_lane_launcher.h"
#include "ui/torirs_chrome_skin.h"

/** The profile role naming the stone the launcher hangs below. */
#define APP_PLUGIN_LAUNCHER_ROLE "plugin_launcher_anchor"

/** How deep the mount search climbs before giving up. A toplevel is nothing
 *  like this deep; it is a bound, not a policy. */
#define APP_PLUGIN_LAUNCHER_MOUNT_MAX_DEPTH 32

/**
 * The column's geometry for this tick, all parent-local.
 *
 * Gathered in one pass because every part of it is read off the same sibling
 * list, and because a box assembled from two walks of a tree that moved in
 * between is a box that belongs to neither.
 */
struct AppPluginLaneLauncherPlace
{
    /** The anchor's parent -- the stone column, a tree index. */
    int32_t column;
    /** Where the button goes and how big it is, parent-local. */
    int x;
    int y;
    int w;
    int h;
    /** The anchor's backing picture, and the box its glyph occupies inside the
     *  anchor. The launcher wears the first and fills the second. */
    int backing_scene_id;
    int backing_frame;
    int icon_x;
    int icon_y;
    int icon_w;
    int icon_h;
};

/**
 * The anchor's node when the engine may use it, or -1.
 *
 * The anchor itself is NOT required to be visible -- the mobile chat toggle
 * hides the "Start chatting" stone -- but the column it sits in is, and so is
 * a displayable root above it. @see app_plugin_popout_nav_column, which gates
 * its own column the same way and for the same reason: a CS2 script that reads
 * a property of an interface before it is placed mounts it as an orphan root,
 * and a button hung in that is invisible and unclickable.
 *
 * And the PLUGIN layer's hides on top of the native ones, which is the one
 * thing this launcher needs that the pop-out column's gate does not. A frame
 * provider that has replaced the lane's chrome puts that column away with
 * `frame_hidden`, not with the cache's `hide` -- the Stone Drawer does exactly
 * that on this root -- and a provider that has taken the frame over carries
 * its own way into the plugin window, as the Stone Drawer's PLUGINS switch
 * does. Reading only the native hide would hang a second wrench in the air
 * over somebody else's gameframe.
 */
static int32_t
app_plugin_lane_launcher_anchor(struct App* app)
{
    int32_t anchor;
    int32_t column;
    int32_t root;

    assert(app);
    if( !app->tree || app->tree->component_count == 0 )
        return -1;
    anchor = UITree_RoleNodeByName(app->tree, &app->ui_roles, APP_PLUGIN_LAUNCHER_ROLE);
    if( anchor < 0 )
        return -1;
    column = app->tree->components[anchor].parent;
    if( column < 0 )
        return -1;
    for( root = column; app->tree->components[root].parent >= 0;
         root = app->tree->components[root].parent )
        ;
    if( !UITree_RootIsDisplayable(app->tree, root) )
        return -1;
    if( !UITree_NodeNativeVisible(app->tree, &app->ui_host, column, -1) )
        return -1;
    if( UITree_NodeOrAncestorDisplayHiddenEx(app->tree, column, 0) )
        return -1;
    return anchor;
}

/** Is this child one of the column's own shown stones? */
static int
app_plugin_lane_launcher_is_stone(struct App* app, int32_t child)
{
    struct UITreeComponent const* c = &app->tree->components[child];

    if( c->plugin_owner )
        return 0;
    if( c->position.abs_w <= 0 || c->position.abs_h <= 0 )
        return 0;
    return UITree_NodeNativeVisible(app->tree, &app->ui_host, child, -1) ? 1 : 0;
}

/**
 * The anchor's own art: the backing it draws and the box its glyph fills.
 *
 * By AREA and not by sub-id: the cache builds these with `cc_create(..., 0)`
 * and `cc_create(..., 1)`, but that ordering is the script's business, and the
 * only thing that is true of a stone button anywhere is that the plate is the
 * big picture and the glyph is a smaller one on top of it.
 *
 * @return 1 when at least a backing was found. A button with no glyph child
 * still gets a launcher -- the wrench is then centred at its own size.
 */
static int
app_plugin_lane_launcher_art(
    struct App* app,
    int32_t anchor,
    struct AppPluginLaneLauncherPlace* place)
{
    struct UITreeComponent const* anchor_c = &app->tree->components[anchor];
    int32_t backing = -1;
    int32_t glyph = -1;
    long backing_area = 0;
    long glyph_area = 0;

    for( int32_t child = anchor_c->first_child; child >= 0;
         child = app->tree->components[child].next_sibling )
    {
        struct UITreeComponent const* c = &app->tree->components[child];
        long area;

        if( c->type != UIELEM_RS_GRAPHIC || c->u.rs_graphic.scene_id <= 0 )
            continue;
        if( c->position.abs_w <= 0 || c->position.abs_h <= 0 )
            continue;
        area = (long)c->position.abs_w * (long)c->position.abs_h;
        if( area > backing_area )
        {
            glyph = backing;
            glyph_area = backing_area;
            backing = child;
            backing_area = area;
        }
        else if( area > glyph_area )
        {
            glyph = child;
            glyph_area = area;
        }
    }
    if( backing < 0 )
        return 0;

    place->backing_scene_id = app->tree->components[backing].u.rs_graphic.scene_id;
    place->backing_frame = app->tree->components[backing].u.rs_graphic.atlas_index;
    if( glyph >= 0 )
    {
        struct UITreeComponent const* c = &app->tree->components[glyph];
        place->icon_x = c->position.abs_x - anchor_c->position.abs_x;
        place->icon_y = c->position.abs_y - anchor_c->position.abs_y;
        place->icon_w = c->position.abs_w;
        place->icon_h = c->position.abs_h;
    }
    else
    {
        struct ToriRSChromeSkin_Sprite const* wrench =
            ToriRSChromeSkin_Get(ToriRSChromeSkin_SLOT_PluginIcon);
        assert(wrench);
        place->icon_w = wrench->w < place->w ? wrench->w : place->w;
        place->icon_h = wrench->h < place->h ? wrench->h : place->h;
        place->icon_x = (place->w - place->icon_w) / 2;
        place->icon_y = (place->h - place->icon_h) / 2;
    }
    return 1;
}

/**
 * Where the launcher goes this tick, or 0 when it cannot be placed.
 *
 * The y follows the column's last SHOWN stone rather than the anchor's own
 * laid-out box: hiding a node does not take it out of the layout, so an anchor
 * the chat toggle put away still has a box where it used to be, and pinning to
 * it would leave the launcher floating in the gap.
 * @see ToriRSLaneLauncher_Y.
 */
static int
app_plugin_lane_launcher_place(
    struct App* app,
    int32_t anchor,
    struct AppPluginLaneLauncherPlace* place)
{
    struct UITreeComponent const* anchor_c = &app->tree->components[anchor];
    struct UITreeComponent const* column_c;
    int const anchor_y = anchor_c->position.abs_y;
    int bottom = -1;
    int previous_y = -1;
    int pitch;

    memset(place, 0, sizeof(*place));
    place->column = anchor_c->parent;
    column_c = &app->tree->components[place->column];

    place->w = anchor_c->position.abs_w;
    place->h = anchor_c->position.abs_h;
    if( place->w <= 0 || place->h <= 0 )
        return 0;
    /* A lane whose stone is bigger than the composition buffers gets no
     * launcher. @see TORIRS_LANE_LAUNCHER_MAX_W. */
    if( place->w > TORIRS_LANE_LAUNCHER_MAX_W || place->h > TORIRS_LANE_LAUNCHER_MAX_H )
        return 0;

    for( int32_t child = column_c->first_child; child >= 0;
         child = app->tree->components[child].next_sibling )
    {
        struct UITreeComponent const* c = &app->tree->components[child];
        int edge;

        if( !app_plugin_lane_launcher_is_stone(app, child) )
            continue;
        edge = c->position.abs_y + c->position.abs_h;
        if( edge > bottom )
            bottom = edge;
        if( c->position.abs_y < anchor_y && c->position.abs_y > previous_y )
            previous_y = c->position.abs_y;
    }
    /* A column showing nothing at all is a column mid-rebuild, not a column
     * with room underneath it. */
    if( bottom < 0 )
        return 0;

    pitch = ToriRSLaneLauncher_Pitch(anchor_y, previous_y, place->h);
    place->x = anchor_c->position.abs_x - column_c->position.abs_x;
    place->y = ToriRSLaneLauncher_Y(bottom, place->h, pitch) - column_c->position.abs_y;
    return app_plugin_lane_launcher_art(app, anchor, place);
}

/**
 * The node the button hangs under: the nearest ancestor of the column whose
 * box holds the whole button, the column itself included.
 *
 * Climbing rather than taking the root outright, because every layer between
 * the two is one more thing that can hide, move or scroll the button along
 * with the chrome it belongs to. The root is the answer only when nothing
 * closer will hold it.
 *
 * Writes the ancestor's own child that the column descends from, which is the
 * sibling the button is ordered after.
 */
static int32_t
app_plugin_lane_launcher_mount(
    struct App* app,
    struct AppPluginLaneLauncherPlace const* place,
    int32_t* out_sibling)
{
    struct UITreeComponent const* column_c = &app->tree->components[place->column];
    int const left = column_c->position.abs_x + place->x;
    int const top = column_c->position.abs_y + place->y;
    int32_t below = place->column;
    int32_t mount = place->column;

    assert(out_sibling);
    for( int depth = 0; depth < APP_PLUGIN_LAUNCHER_MOUNT_MAX_DEPTH; depth++ )
    {
        struct UITreeComponent const* c = &app->tree->components[mount];

        if( left >= c->position.abs_x && top >= c->position.abs_y &&
            left + place->w <= c->position.abs_x + c->position.abs_w &&
            top + place->h <= c->position.abs_y + c->position.abs_h )
            break;
        if( c->parent < 0 )
            break;
        below = mount;
        mount = c->parent;
    }
    *out_sibling = below == mount ? -1 : below;
    return mount;
}

static void
app_plugin_lane_launcher_clear(struct App* app)
{
    struct AppPluginLaneLauncher* launcher = &app->plugin_launcher;

    if( app->tree && UITree_ResolveRef(app->tree, launcher->ref) >= 0 )
        (void)UITree_WidgetRemove(app->tree, launcher->ref, APP_PLUGIN_LAUNCHER_OWNER);
    launcher->ref.index = -1;
    launcher->placed = 0;
    launcher->graphic_set = 0;
    launcher->label[0] = '\0';
    /* The published picture is NOT forgotten. It describes a scene entry that
     * survives this, and the column coming back with the same stone must not
     * recompose it -- a root remounts on every window-mode change. */
}

/**
 * Compose and publish the button's picture, if the stone or the box it is
 * fitted to changed since the last one.
 *
 * @return 1 when a new picture was published, 0 when the one already
 * published is still right, and -1 when there is no picture to be had -- the
 * stone's own sprite has not reached the scene yet. The caller places no
 * button on -1: a node created for a picture that is not there yet would be
 * created and removed on every tick until it arrived, and a tree generation
 * that never rests re-declares every plugin frame at frame rate.
 */
static int
app_plugin_lane_launcher_picture(
    struct App* app, struct AppPluginLaneLauncherPlace const* place)
{
    struct AppPluginLaneLauncher* launcher = &app->plugin_launcher;
    static uint32_t backing[TORIRS_LANE_LAUNCHER_PIXELS_MAX];
    static uint32_t picture[TORIRS_LANE_LAUNCHER_PIXELS_MAX];
    struct ToriRSChromeSkin_Sprite const* wrench;
    int backing_w = 0;
    int backing_h = 0;

    if( launcher->backing_scene_id == place->backing_scene_id &&
        launcher->backing_frame == place->backing_frame &&
        launcher->composed_w == place->w && launcher->composed_h == place->h &&
        launcher->icon_x == place->icon_x && launcher->icon_y == place->icon_y &&
        launcher->icon_w == place->icon_w && launcher->icon_h == place->icon_h )
        return 0;

    if( !UITreeSceneBridge_ReadSpriteFrame(
            &app->bridge, place->backing_scene_id, place->backing_frame, backing,
            TORIRS_LANE_LAUNCHER_PIXELS_MAX, &backing_w, &backing_h) )
        return -1;

    /* The wrench the client's launcher wears everywhere else, baked rather
     * than resolved out of the cache: sprite 785 is the OSRS wrench and
     * something else entirely on every other cache, and a launcher wearing the
     * wrong picture is worse than one wearing none. */
    wrench = ToriRSChromeSkin_Get(ToriRSChromeSkin_SLOT_PluginIcon);
    assert(wrench);
    assert(wrench->argb);

    ToriRSLaneLauncher_Compose(
        backing, backing_w, backing_h, wrench->argb, wrench->w, wrench->h, place->w, place->h,
        place->icon_x, place->icon_y, place->icon_w, place->icon_h, picture);
    (void)UITreeSceneBridge_PublishLauncherButton(&app->bridge, place->w, place->h, picture);
    launcher->backing_scene_id = place->backing_scene_id;
    launcher->backing_frame = place->backing_frame;
    launcher->composed_w = place->w;
    launcher->composed_h = place->h;
    launcher->icon_x = place->icon_x;
    launcher->icon_y = place->icon_y;
    launcher->icon_w = place->icon_w;
    launcher->icon_h = place->icon_h;
    return 1;
}

static void
app_plugin_lane_launcher_trace(struct App* app, int32_t mount)
{
    struct AppPluginLaneLauncher* launcher = &app->plugin_launcher;

    if( launcher->reported_placed == launcher->placed && launcher->reported_x == launcher->x &&
        launcher->reported_y == launcher->y )
        return;
    launcher->reported_placed = launcher->placed;
    launcher->reported_x = launcher->x;
    launcher->reported_y = launcher->y;
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome: lane launcher placed=%d mount=%d at=%d,%d %dx%d\n", launcher->placed,
            (int)mount, launcher->x, launcher->y, launcher->w, launcher->h);
}

/**
 * Reconcile the launcher with the lane's column. Runs once per panel tick,
 * beside the pop-out column's own reconcile.
 */
static void
app_plugin_lane_launcher_tick(struct App* app)
{
    struct AppPluginLaneLauncher* launcher = &app->plugin_launcher;
    struct AppPluginLaneLauncherPlace place;
    int32_t const anchor = app_plugin_lane_launcher_anchor(app);
    int32_t sibling = -1;
    int32_t mount;
    int32_t node;
    char label[UITREE_MENU_OPTION_LEN];
    int published;
    int mount_x;
    int mount_y;

    assert(app);
    assert(app->plugins);
    if( launcher->op_serial == 0 )
        launcher->op_serial = 1;

    if( anchor < 0 || !app_plugin_lane_launcher_place(app, anchor, &place) )
    {
        app_plugin_lane_launcher_clear(app);
        app_plugin_lane_launcher_trace(app, -1);
        return;
    }

    /* The picture BEFORE the node: a button with nothing to wear is not placed
     * at all. @see app_plugin_lane_launcher_picture. */
    published = app_plugin_lane_launcher_picture(app, &place);
    if( published < 0 )
    {
        app_plugin_lane_launcher_clear(app);
        app_plugin_lane_launcher_trace(app, -1);
        return;
    }

    mount = app_plugin_lane_launcher_mount(app, &place, &sibling);
    node = UITree_WidgetCreateGraphic(
        app->tree, UITree_RefAt(app->tree, mount), APP_PLUGIN_LAUNCHER_OWNER, "torirs-launcher");
    /* One owned node under a parent that resolved this tick: neither the owner
     * cap nor the parent can refuse it. */
    assert(node >= 0);
    if( UITree_ResolveRef(app->tree, launcher->ref) != node )
    {
        launcher->ref = UITree_RefAt(app->tree, node);
        launcher->graphic_set = 0;
        launcher->x = -1;
        launcher->y = -1;
        launcher->w = -1;
        launcher->h = -1;
        launcher->label[0] = '\0';
    }

    if( published || !launcher->graphic_set || launcher->w != place.w ||
        launcher->h != place.h )
    {
        (void)UITree_WidgetSetGraphic(
            app->tree, launcher->ref, APP_PLUGIN_LAUNCHER_OWNER, UITREE_SCENE_PLUGIN_LAUNCHER_ID,
            place.w, place.h);
        launcher->graphic_set = 1;
        launcher->w = place.w;
        launcher->h = place.h;
        app->need_redraw = 1;
    }

    /* The place is column-local; the button hangs under the mount. */
    mount_x = app->tree->components[place.column].position.abs_x + place.x -
              app->tree->components[mount].position.abs_x;
    mount_y = app->tree->components[place.column].position.abs_y + place.y -
              app->tree->components[mount].position.abs_y;
    if( launcher->x != mount_x || launcher->y != mount_y )
    {
        (void)UITree_WidgetSetPosition(
            app->tree, launcher->ref, APP_PLUGIN_LAUNCHER_OWNER, mount_x, mount_y);
        launcher->x = mount_x;
        launcher->y = mount_y;
        app->need_redraw = 1;
    }

    /* Straight after the column in the mount's child order, so the launcher
     * draws with the chrome it belongs to rather than over everything the
     * mount holds. Restated every tick: a restatement that is already true
     * costs one comparison and changes nothing, and the cache rebuilds these
     * child lists. */
    if( sibling >= 0 )
        (void)UITree_WidgetMoveAfter(
            app->tree, launcher->ref, APP_PLUGIN_LAUNCHER_OWNER,
            UITree_RefAt(app->tree, sibling));

    /* The lane's own stones carry plain labels -- "Logout", "Toggle Chatbox",
     * "Start chatting" -- and this one is read the same way, by a long press. */
    snprintf(
        label, sizeof(label), "%s", app->plugin_panel_visible ? "Close Plugins" : "Plugins");
    if( strcmp(label, launcher->label) != 0 )
    {
        (void)UITree_WidgetSetOperation(
            app->tree, launcher->ref, APP_PLUGIN_LAUNCHER_OWNER, launcher->op_serial, 1, label);
        memcpy(launcher->label, label, sizeof(launcher->label));
    }

    launcher->placed = 1;
    app_plugin_lane_launcher_trace(app, mount);
}

int
app_plugin_lane_launcher_click(struct App* app, int32_t node)
{
    assert(app);
    assert(app->tree);
    if( !app->plugins )
        return 0;
    if( !app->plugin_launcher.placed )
        return 0;
    if( UITree_ResolveRef(app->tree, app->plugin_launcher.ref) != node )
        return 0;
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(stderr, "chrome: lane launcher click tick=%d\n", g_plugin_panel_ticks);
    /* The rail's Manage Plugins destination, routed by the rail's own rules:
     * the settings page when the window is shut or showing something else,
     * closed when it is already the page in front. */
    app_plugin_rail_select(app, TORIRS_CHROME_SHELL_PAGE_MANAGE);
    return 1;
}
