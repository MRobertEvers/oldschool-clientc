/*
 * One view of the edited world: per-view state, the camera's publish and
 * follow, the thin toolbar, and the boot pose. The glue that puts it in a
 * viewer process is the App layer's (src/app/app_editor_view.c), so nothing
 * here names the App. editor_view.h has the contract.
 */

#include "editor/editor_view.h"

#include "render/torirs_world_projection.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/torirs_chrome_skin.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- the pose ---------------------------------------------------------- */

void
EditorView_CameraToValues(
    const struct EditorViewCamera* camera,
    int32_t* out_values)
{
    assert(camera);
    assert(out_values);

    out_values[TORIRSMAPED_CAMERA_X] = camera->x;
    out_values[TORIRSMAPED_CAMERA_Z] = camera->z;
    out_values[TORIRSMAPED_CAMERA_LEVEL] = camera->level;
    out_values[TORIRSMAPED_CAMERA_YAW] = camera->yaw;
    out_values[TORIRSMAPED_CAMERA_PITCH] = camera->pitch;
    out_values[TORIRSMAPED_CAMERA_ZOOM] = camera->zoom;
}

void
EditorView_CameraFromValues(
    const int32_t* values,
    struct EditorViewCamera* out_camera)
{
    assert(values);
    assert(out_camera);

    out_camera->x = values[TORIRSMAPED_CAMERA_X];
    out_camera->z = values[TORIRSMAPED_CAMERA_Z];
    out_camera->level = values[TORIRSMAPED_CAMERA_LEVEL];
    out_camera->yaw = values[TORIRSMAPED_CAMERA_YAW];
    out_camera->pitch = values[TORIRSMAPED_CAMERA_PITCH];
    out_camera->zoom = values[TORIRSMAPED_CAMERA_ZOOM];
}

static int
camera_equal(
    const struct EditorViewCamera* a,
    const struct EditorViewCamera* b)
{
    return a->x == b->x && a->z == b->z && a->level == b->level && a->yaw == b->yaw
           && a->pitch == b->pitch && a->zoom == b->zoom;
}

/** Adopt a pose from inside the view: the host applies it next frame. */
static void
view_adopt(
    struct EditorView* view,
    const struct EditorViewCamera* camera)
{
    view->camera = *camera;
    view->camera_valid = 1;
    view->camera_dirty = 1;
}

/* ---- the core ---------------------------------------------------------- */

/** STATE_SELECTION's kind 0: nothing selected (editor_panel.h's
 *  EDITOR_SELECTION_NONE; the wire value, so the view needs no panel). */
enum
{
    VIEW_SELECTION_NONE = 0,
};

void
EditorView_Init(
    struct EditorView* view,
    struct Editor* editor)
{
    assert(view);
    assert(editor);

    memset(view, 0, sizeof(*view));
    view->editor = editor;
    view->self_index = -1;
    view->level_cap = -1;
    view->layers = EDITOR_VIEW_LAYER_DEFAULT;
    view->follow_index = -1;
    view->toolbar.panel = -1;
}

int
EditorView_AskSelf(struct EditorView* view)
{
    assert(view);
    return Editor_StateSet(view->editor, TORIRSMAPED_STATE_VIEW_SELF, NULL, 0);
}

/** Publish the camera key now: the pose, or nothing (count 0) to say this
 *  view stopped broadcasting. */
static int
view_publish(
    struct EditorView* view,
    int with_pose)
{
    int32_t values[TORIRSMAPED_CAMERA_VALUES];
    uint32_t key;

    if( view->self_index < 0 )
        return 0;
    key = ToriRSMapEd_CameraKey(view->self_index);
    if( !with_pose )
    {
        view->published_valid = 0;
        return Editor_StateSet(view->editor, key, NULL, 0);
    }
    if( !view->camera_valid )
        return 0;
    EditorView_CameraToValues(&view->camera, values);
    view->published = view->camera;
    view->published_valid = 1;
    return Editor_StateSet(view->editor, key, values, TORIRSMAPED_CAMERA_VALUES);
}

int
EditorView_OnState(
    struct EditorView* view,
    uint32_t key,
    const int32_t* values,
    int count)
{
    int index;

    assert(view);
    assert(values || count == 0);

    if( key == TORIRSMAPED_STATE_VIEW_SELF )
    {
        if( count < TORIRSMAPED_VIEW_SELF_VALUES )
            return 1;
        view->self_index = values[TORIRSMAPED_VIEW_SELF_INDEX];
        view->group = (uint32_t)values[TORIRSMAPED_VIEW_SELF_GROUP];
        /* A view's own index is never a peer; one it was following under
         * that number belonged to a connection that has gone. */
        if( view->follow_index == view->self_index )
            view->follow_index = -1;
        if( view->self_index >= 0 && view->self_index < EDITOR_VIEW_PEER_MAX )
            view->peers[view->self_index].live = 0;
        /* "Broadcast" asked before the index was known goes out now. */
        if( view->broadcast )
            view_publish(view, 1);
        return 1;
    }

    index = ToriRSMapEd_CameraKeyIndex(key);
    if( index >= 0 )
    {
        if( index == view->self_index )
            return 1; /* the echo of our own publish */
        if( count < TORIRSMAPED_CAMERA_VALUES )
        {
            view->peers[index].live = 0;
            /* The followed view stopped or left: let go, so the slot's next
             * owner is never followed by accident. */
            if( view->follow_index == index )
                view->follow_index = -1;
            return 1;
        }
        view->peers[index].live = 1;
        EditorView_CameraFromValues(values, &view->peers[index].camera);
        if( view->follow_index == index )
            view_adopt(view, &view->peers[index].camera);
        return 1;
    }

    /* Follow selection: frame the tile the Client selected (absolute world
     * tiles, editor_panel.c's layout: kind, x, z, level, ...). Not consumed:
     * the panels want the selection too. */
    if( key == TORIRSMAPED_STATE_SELECTION && view->follow_selection && count >= 4
        && values[0] != VIEW_SELECTION_NONE )
    {
        struct EditorViewCamera framed = view->camera;
        if( !view->camera_valid )
        {
            framed.yaw = 0;
            framed.pitch = 300;
            framed.zoom = 1200;
        }
        framed.x = values[1] * 128 + 64;
        framed.z = values[2] * 128 + 64;
        framed.level = values[3] >= 0 && values[3] <= 3 ? values[3] : 0;
        view_adopt(view, &framed);
    }
    return 0;
}

void
EditorView_SetCamera(
    struct EditorView* view,
    const struct EditorViewCamera* camera)
{
    assert(view);
    assert(camera);

    view->camera = *camera;
    view->camera_valid = 1;
}

int
EditorView_TakeCamera(
    struct EditorView* view,
    struct EditorViewCamera* out)
{
    assert(view);
    assert(out);

    if( !view->camera_dirty )
        return 0;
    view->camera_dirty = 0;
    *out = view->camera;
    return 1;
}

int
EditorView_SetBroadcast(
    struct EditorView* view,
    int broadcast)
{
    assert(view);

    broadcast = broadcast ? 1 : 0;
    if( view->broadcast == broadcast )
        return view->self_index >= 0;
    view->broadcast = broadcast;
    if( view->self_index < 0 )
        return 0;
    view_publish(view, broadcast);
    return 1;
}

int
EditorView_Follow(
    struct EditorView* view,
    int index)
{
    assert(view);
    assert(index >= -1);
    assert(index < EDITOR_VIEW_PEER_MAX);

    if( index >= 0 && index == view->self_index )
        return 0;
    view->follow_index = index;
    if( index >= 0 && view->peers[index].live )
        view_adopt(view, &view->peers[index].camera);
    return 1;
}

void
EditorView_ApplyPreset(
    struct EditorView* view,
    enum EditorViewPreset preset)
{
    struct EditorViewCamera pose;

    assert(view);
    assert(preset >= 0 && preset < EDITOR_VIEW_PRESET_COUNT);

    pose = view->camera;
    if( !view->camera_valid )
    {
        pose.x = 0;
        pose.z = 0;
        pose.level = 0;
        pose.yaw = 0;
        pose.pitch = 300;
        pose.zoom = 1200;
    }
    switch( preset )
    {
    case EDITOR_VIEW_PRESET_TOP:
        /* The host clamps to its own steepest pitch. */
        pose.pitch = 512;
        pose.zoom = 2600;
        break;
    case EDITOR_VIEW_PRESET_ISO:
        pose.yaw = 256;
        pose.pitch = 256;
        pose.zoom = 1800;
        break;
    case EDITOR_VIEW_PRESET_NORTH:
        pose.yaw = 0;
        break;
    case EDITOR_VIEW_PRESET_GROUND:
        pose.pitch = 160;
        pose.zoom = 700;
        break;
    case EDITOR_VIEW_PRESET_COUNT:
        break;
    }
    /* A preset is the user taking the camera back from whatever it followed. */
    view->follow_index = -1;
    view_adopt(view, &pose);
}

int
EditorView_Tick(
    struct EditorView* view,
    int64_t now_ms)
{
    assert(view);

    if( !view->broadcast || view->self_index < 0 || !view->camera_valid )
        return 0;
    if( view->published_valid && camera_equal(&view->published, &view->camera) )
        return 0;
    if( view->published_valid && now_ms - view->published_ms < EDITOR_VIEW_PUBLISH_MS )
        return 0;
    view->published_ms = now_ms;
    return view_publish(view, 1);
}

uint8_t
EditorView_LevelMask(const struct EditorView* view)
{
    assert(view);

    if( view->level_cap < 0 )
        return 0;
    if( view->level_solo )
        return (uint8_t)(1u << view->level_cap);
    return (uint8_t)((1u << (view->level_cap + 1)) - 1);
}

void
EditorView_SetLevelCap(
    struct EditorView* view,
    int level_cap,
    int solo)
{
    assert(view);
    assert(level_cap >= -1 && level_cap <= 3);

    view->level_cap = level_cap;
    view->level_solo = level_cap >= 0 && solo ? 1 : 0;
}

int
EditorView_LayerShown(
    const struct EditorView* view,
    enum EditorViewLayer layer)
{
    assert(view);
    return (view->layers & (uint32_t)layer) != 0;
}

void
EditorView_SetLayer(
    struct EditorView* view,
    enum EditorViewLayer layer,
    int shown)
{
    assert(view);

    if( shown )
        view->layers |= (uint32_t)layer;
    else
        view->layers &= ~(uint32_t)layer;
}

/* ---- pose <-> eye --------------------------------------------------------- */

/** The orbit offset (eye minus focus) for a pose, at distance `distance`. */
static struct ToriDraw_Position
view_orbit_offset(
    int pitch,
    int yaw,
    int distance)
{
    struct ToriDraw_Position offset;
    memset(&offset, 0, sizeof(offset));
    ToriRS_OrbitCameraEye(0, 0, 0, pitch, yaw, distance, &offset);
    return offset;
}

void
EditorView_PoseEye(
    const struct EditorViewCamera* pose,
    int base_x,
    int base_z,
    int ground_y,
    struct ToriDraw_Position* out_eye)
{
    assert(pose);
    assert(out_eye);

    memset(out_eye, 0, sizeof(*out_eye));
    ToriRS_OrbitCameraEye(
        pose->x - base_x,
        ground_y,
        pose->z - base_z,
        pose->pitch,
        pose->yaw & 0x7ff,
        pose->zoom,
        out_eye);
}

void
EditorView_PoseFromEye(
    const struct ToriDraw_Position* eye,
    int yaw,
    int pitch,
    int ground_y,
    int base_x,
    int base_z,
    int level,
    int fallback_zoom,
    struct EditorViewCamera* out)
{
    struct ToriDraw_Position unit;
    int distance = fallback_zoom > 0 ? fallback_zoom : 1200;

    assert(eye);
    assert(out);

    yaw &= 0x7ff;
    /* y grows DOWN: an eye above the ground has the smaller y. The view ray
     * falls -unit.y per 4096 of distance. */
    unit = view_orbit_offset(pitch, yaw, 4096);
    if( unit.y < -64 && ground_y > eye->y )
    {
        long const d = ((long)(ground_y - eye->y) * 4096 + (-unit.y) / 2) / -unit.y;
        if( d >= 64 && d <= 65535 )
            distance = (int)d;
    }
    unit = view_orbit_offset(pitch, yaw, distance);

    out->x = eye->x - unit.x + base_x;
    out->z = eye->z - unit.z + base_z;
    out->level = level;
    out->yaw = yaw;
    out->pitch = pitch;
    out->zoom = distance;
    if( out->x < 0 )
        out->x = 0;
    if( out->z < 0 )
        out->z = 0;
}

/* ---- the thin toolbar ---------------------------------------------------- */

static const char* const k_level_labels[5] = { "All levels", "Level 0", "Level 1", "Level 2",
                                               "Level 3" };
static const char* const k_layer_labels[EDITOR_VIEW_LAYER_COUNT] = {
    "Terrain", "Locs", "Npcs", "Objs", "Collision", "Tile grid", "Zone grid",
};
static const char* const k_preset_labels[EDITOR_VIEW_PRESET_COUNT] = {
    "Top down", "Three-quarter", "North up", "Ground level",
};

enum
{
    /** RADIO group the level menu's rows share. */
    VIEW_RADIO_LEVEL = 0x5649,
};

static void
toolbar_menu_toggle(
    struct ToriRSChrome* ui,
    struct EditorViewToolbar* bar,
    int menu,
    int button)
{
    int const menus[3] = { bar->menu_level, bar->menu_layers, bar->menu_camera };
    int open = 0;

    for( int i = 0; i < 3; i++ )
    {
        if( menus[i] == menu )
            open = !ui->panels[menu].visible;
        else
            ToriRSChrome_PanelSetVisible(ui, menus[i], 0);
    }
    if( open )
    {
        /* Under the button that opened it. */
        struct ToriRSChromeRect const strip = ToriRSChrome_PanelRect(ui, bar->panel);
        int x = strip.x;
        int const index = button == bar->bt_level ? 0 : button == bar->bt_layers ? 1 : 2;
        x += index * 40;
        ToriRSChrome_PanelMove(ui, menu, x, strip.y + (strip.h > 0 ? strip.h : 36));
    }
    ToriRSChrome_PanelSetVisible(ui, menu, open);
}

void
EditorView_ToolbarBuild(
    struct EditorView* view,
    struct ToriRSChrome* ui)
{
    struct EditorViewToolbar* bar;

    assert(view);
    assert(ui);
    assert(!view->toolbar.built);

    bar = &view->toolbar;
    memset(bar, 0xff, sizeof(*bar)); /* every handle -1 */
    bar->built = 1;

    /* No fixed width: one row across the top, the viewer window's strip. */
    bar->panel = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_TOOLBAR, 8, 8, 0, "View");
    assert(bar->panel >= 0);
    bar->bt_level = ToriRSChrome_IconButton(ui, bar->panel, TORIRS_CHROME_SKIN_TOOL_RAISE, "Levels");
    bar->bt_layers = ToriRSChrome_IconButton(ui, bar->panel, TORIRS_CHROME_SKIN_TREE_OPEN, "Layers");
    bar->bt_camera = ToriRSChrome_IconButton(ui, bar->panel, TORIRS_CHROME_SKIN_SEARCH, "Camera");
    bar->bt_follow_selection =
        ToriRSChrome_IconButton(ui, bar->panel, TORIRS_CHROME_SKIN_TOOL_SELECT, "Follow selection");
    bar->bt_open_panels =
        ToriRSChrome_IconButton(ui, bar->panel, TORIRS_CHROME_SKIN_DOCK, "Open panels");
    ToriRSChrome_SetTooltip(ui, bar->bt_level, "Level cap: which planes this view draws");
    ToriRSChrome_SetTooltip(ui, bar->bt_layers, "What this view draws");
    ToriRSChrome_SetTooltip(ui, bar->bt_camera, "Camera presets, broadcast, follow another view");
    ToriRSChrome_SetTooltip(
        ui, bar->bt_follow_selection, "Frame the session's selection whenever it changes");
    ToriRSChrome_SetTooltip(ui, bar->bt_open_panels, "Show the editor's panels in this window too");

    bar->menu_level = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_MENU, 8, 48, 0, "Levels");
    for( int i = 0; i < 5; i++ )
        bar->level_items[i] =
            ToriRSChrome_Radio(ui, bar->menu_level, k_level_labels[i], VIEW_RADIO_LEVEL, i == 0);
    ToriRSChrome_Separator(ui, bar->menu_level);
    bar->item_solo = ToriRSChrome_Checkbox(ui, bar->menu_level, "Only that level", 0);

    bar->menu_layers = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_MENU, 48, 48, 0, "Layers");
    for( int i = 0; i < EDITOR_VIEW_LAYER_COUNT; i++ )
        bar->layer_items[i] = ToriRSChrome_Checkbox(
            ui, bar->menu_layers, k_layer_labels[i], (view->layers >> i) & 1u);

    bar->menu_camera = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_MENU, 88, 48, 0, "Camera");
    for( int i = 0; i < EDITOR_VIEW_PRESET_COUNT; i++ )
        bar->preset_items[i] = ToriRSChrome_MenuItem(ui, bar->menu_camera, k_preset_labels[i]);
    ToriRSChrome_Separator(ui, bar->menu_camera);
    bar->item_broadcast =
        ToriRSChrome_Checkbox(ui, bar->menu_camera, "Broadcast my camera", view->broadcast);
    bar->item_unfollow = ToriRSChrome_MenuItem(ui, bar->menu_camera, "Stop following");
    for( int i = 0; i < EDITOR_VIEW_PEER_MAX; i++ )
    {
        char label[TORIRS_CHROME_LABEL_MAX];
        snprintf(label, sizeof(label), "Follow view %d", i);
        bar->follow_items[i] = ToriRSChrome_Checkbox(ui, bar->menu_camera, label, 0);
        ToriRSChrome_SetHidden(ui, bar->follow_items[i], 1);
    }

    ToriRSChrome_PanelSetVisible(ui, bar->menu_level, 0);
    ToriRSChrome_PanelSetVisible(ui, bar->menu_layers, 0);
    ToriRSChrome_PanelSetVisible(ui, bar->menu_camera, 0);
    ToriRSChrome_PanelSetVisible(ui, bar->panel, 1);
    EditorView_ToolbarRefresh(view, ui);
}

void
EditorView_ToolbarRefresh(
    struct EditorView* view,
    struct ToriRSChrome* ui)
{
    struct EditorViewToolbar* bar;

    assert(view);
    assert(ui);

    bar = &view->toolbar;
    if( !bar->built )
        return;

    {
        int const row = view->level_cap < 0 ? 0 : view->level_cap + 1;
        if( !ToriRSChrome_Checked(ui, bar->level_items[row]) )
            ToriRSChrome_RadioSelect(ui, bar->level_items[row]);
        ToriRSChrome_SetChecked(ui, bar->item_solo, view->level_solo);
    }
    for( int i = 0; i < EDITOR_VIEW_LAYER_COUNT; i++ )
        ToriRSChrome_SetChecked(ui, bar->layer_items[i], (view->layers >> i) & 1u);
    ToriRSChrome_SetChecked(ui, bar->item_broadcast, view->broadcast);
    /* Only connections that are broadcasting can be followed. */
    for( int i = 0; i < EDITOR_VIEW_PEER_MAX; i++ )
    {
        int const offered = view->peers[i].live && i != view->self_index;
        ToriRSChrome_SetHidden(ui, bar->follow_items[i], !offered && view->follow_index != i);
        ToriRSChrome_SetChecked(ui, bar->follow_items[i], view->follow_index == i);
    }
    ToriRSChrome_SetHidden(ui, bar->item_unfollow, view->follow_index < 0);
    ToriRSChrome_SetLabel(
        ui,
        bar->bt_follow_selection,
        view->follow_selection ? "Following selection" : "Follow selection");
}

enum EditorViewAction
EditorView_ToolbarActivate(
    struct EditorView* view,
    struct ToriRSChrome* ui,
    int widget)
{
    struct EditorViewToolbar* bar;

    assert(view);
    assert(ui);

    bar = &view->toolbar;
    if( !bar->built || widget < 0 )
        return EDITOR_VIEW_ACTION_NONE;

    if( widget == bar->bt_level )
    {
        toolbar_menu_toggle(ui, bar, bar->menu_level, widget);
        return EDITOR_VIEW_ACTION_HANDLED;
    }
    if( widget == bar->bt_layers )
    {
        toolbar_menu_toggle(ui, bar, bar->menu_layers, widget);
        return EDITOR_VIEW_ACTION_HANDLED;
    }
    if( widget == bar->bt_camera )
    {
        toolbar_menu_toggle(ui, bar, bar->menu_camera, widget);
        return EDITOR_VIEW_ACTION_HANDLED;
    }
    if( widget == bar->bt_follow_selection )
    {
        view->follow_selection = !view->follow_selection;
        EditorView_ToolbarRefresh(view, ui);
        return EDITOR_VIEW_ACTION_HANDLED;
    }
    if( widget == bar->bt_open_panels )
        return EDITOR_VIEW_ACTION_OPEN_PANELS;

    for( int i = 0; i < 5; i++ )
    {
        if( widget == bar->level_items[i] )
        {
            EditorView_SetLevelCap(view, i - 1, ToriRSChrome_Checked(ui, bar->item_solo));
            EditorView_ToolbarRefresh(view, ui);
            return EDITOR_VIEW_ACTION_HANDLED;
        }
    }
    if( widget == bar->item_solo )
    {
        EditorView_SetLevelCap(view, view->level_cap, ToriRSChrome_Checked(ui, bar->item_solo));
        EditorView_ToolbarRefresh(view, ui);
        return EDITOR_VIEW_ACTION_HANDLED;
    }
    for( int i = 0; i < EDITOR_VIEW_LAYER_COUNT; i++ )
    {
        if( widget == bar->layer_items[i] )
        {
            EditorView_SetLayer(
                view, (enum EditorViewLayer)(1u << i), ToriRSChrome_Checked(ui, widget));
            return EDITOR_VIEW_ACTION_HANDLED;
        }
    }
    for( int i = 0; i < EDITOR_VIEW_PRESET_COUNT; i++ )
    {
        if( widget == bar->preset_items[i] )
        {
            EditorView_ApplyPreset(view, (enum EditorViewPreset)i);
            ToriRSChrome_PanelSetVisible(ui, bar->menu_camera, 0);
            EditorView_ToolbarRefresh(view, ui);
            return EDITOR_VIEW_ACTION_HANDLED;
        }
    }
    if( widget == bar->item_broadcast )
    {
        EditorView_SetBroadcast(view, ToriRSChrome_Checked(ui, widget));
        return EDITOR_VIEW_ACTION_HANDLED;
    }
    if( widget == bar->item_unfollow )
    {
        EditorView_Follow(view, -1);
        EditorView_ToolbarRefresh(view, ui);
        return EDITOR_VIEW_ACTION_HANDLED;
    }
    for( int i = 0; i < EDITOR_VIEW_PEER_MAX; i++ )
    {
        if( widget == bar->follow_items[i] )
        {
            EditorView_Follow(view, view->follow_index == i ? -1 : i);
            EditorView_ToolbarRefresh(view, ui);
            return EDITOR_VIEW_ACTION_HANDLED;
        }
    }
    return EDITOR_VIEW_ACTION_NONE;
}

int
EditorView_SpawnBody(
    const char* maped_url,
    uint32_t group,
    const struct EditorViewCamera* camera,
    char* out,
    int out_size)
{
    char pose[96];
    int32_t values[TORIRSMAPED_CAMERA_VALUES];
    int length;

    assert(maped_url);
    assert(camera);
    assert(out);
    assert(out_size > 0);

    EditorView_CameraToValues(camera, values);
    ToriRSMapEd_CameraFormat(values, pose, sizeof(pose));
    length = snprintf(
        out, (size_t)out_size, "maped=%s\ngroup=%u\ncamera=%s\n", maped_url, (unsigned)group, pose);
    return length < out_size ? length : -1;
}

/* ---- the boot pose ------------------------------------------------------- */

/* The pose main.c read from `--camera` / `camera=`, for the viewer boot
 * (src/app/app_editor_view.c) to start at. One per process. */
static struct EditorViewCamera g_boot_camera;
static int g_boot_camera_set;

void
EditorView_SetBootCamera(const struct EditorViewCamera* camera)
{
    if( !camera )
    {
        g_boot_camera_set = 0;
        return;
    }
    g_boot_camera = *camera;
    g_boot_camera_set = 1;
}

int
EditorView_BootCamera(struct EditorViewCamera* out)
{
    assert(out);
    if( !g_boot_camera_set )
        return 0;
    *out = g_boot_camera;
    return 1;
}
