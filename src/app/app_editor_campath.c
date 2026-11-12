/*
 * The Camera path panel's App glue (editor/panels/edpanel_campath.c): the
 * World camera read and put EXACTLY -- the eye and its angles, not an orbit
 * pose, so a keyframe is what the renderer shows and a level or climbing
 * view survives the trip -- the ground under a point as the world draws it,
 * and the selected path drawn over the World.
 *
 * One translation unit of the App layer, as app_editor_view.c is, so the
 * panel names no App. Declared in app/app_editor_campath.h.
 */

#include "app/app_editor_campath.h"

#include "app/app_internal.h"
#include "editor/editor_campath.h"
#include "editor/panels/edpanels.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The path's colours: the eye's track amber as a patrol route is, a look
 * line cyan, the selected keyframe the selection's green. */
#define APP_CAMPATH_TRACK_COLOR 0xFFFFB020u
#define APP_CAMPATH_LOOK_COLOR 0xFF40D8E8u
#define APP_CAMPATH_SELECTED_COLOR APP_OUTLINE_COLOR_EDITOR_SELECT
#define APP_CAMPATH_DROP_COLOR 0xFF806030u
/** Pieces a line is drawn in, so the part in front of the camera shows when
 *  an end of it is behind. */
#define APP_CAMPATH_PIECES 8
/** A keyframe marker's half-size, fine units. */
#define APP_CAMPATH_MARK_HALF 24

int
EditorCamPath_AppEye(
    void* user_data,
    struct Editor_CamEye* out)
{
    struct App* app = user_data;
    const struct App_WorldView* view;

    assert(app);
    assert(out);
    if( !app->world || !app->frame_view )
        return 0;
    view = app->frame_view;
    out->x = view->world_camera_pos.x + app->world->_base_tile_x * 128;
    out->y = view->world_camera_pos.y;
    out->z = view->world_camera_pos.z + app->world->_base_tile_z * 128;
    out->yaw = view->world_camera.yaw & 0x7ff;
    out->pitch = view->world_camera.pitch & 0x7ff;
    return 1;
}

int
EditorCamPath_AppEyeSet(
    void* user_data,
    const struct Editor_CamEye* eye)
{
    struct App* app = user_data;
    struct App_WorldView* view;
    int scene_x;
    int scene_z;
    int extent;

    assert(app);
    assert(eye);
    if( !app->world || !app->frame_view )
        return 0;
    view = app->frame_view;
    scene_x = eye->x - app->world->_base_tile_x * 128;
    scene_z = eye->z - app->world->_base_tile_z * 128;
    extent = app->world->_scene_size * 128;
    if( scene_x < 0 || scene_z < 0 || scene_x >= extent || scene_z >= extent )
        return 0;
    view->world_camera_pos.x = scene_x;
    view->world_camera_pos.y = eye->y;
    view->world_camera_pos.z = scene_z;
    view->world_camera.yaw = eye->yaw & 0x7ff;
    view->world_camera.pitch = eye->pitch & 0x7ff;
    /* The free camera: the editor's world has no player to follow. */
    view->camera_unlocked = 1;
    app->need_redraw = 1;
    return 1;
}

int
EditorCamPath_AppGround(
    void* user_data,
    int fine_x,
    int fine_z,
    int level,
    int* out_y)
{
    struct App* app = user_data;
    int scene_x;
    int scene_z;
    int extent;

    assert(app);
    assert(out_y);
    assert(level >= 0);
    assert(level <= 3);
    if( !app->world )
        return 0;
    scene_x = fine_x - app->world->_base_tile_x * 128;
    scene_z = fine_z - app->world->_base_tile_z * 128;
    extent = app->world->_scene_size * 128;
    if( scene_x < 0 || scene_z < 0 || scene_x >= extent || scene_z >= extent )
        return 0;
    *out_y = app_world_height(app, scene_x, scene_z, level);
    return 1;
}

/** A line between two scene points, in pieces: each piece whose two ends
 *  project is drawn. */
static void
campath_line(
    struct App* app,
    int x0,
    int y0,
    int z0,
    int x1,
    int y1,
    int z1,
    uint32_t color,
    int width)
{
    int px = 0;
    int py = 0;
    int have = 0;

    for( int k = 0; k <= APP_CAMPATH_PIECES; k++ )
    {
        int const x = x0 + (x1 - x0) * k / APP_CAMPATH_PIECES;
        int const y = y0 + (y1 - y0) * k / APP_CAMPATH_PIECES;
        int const z = z0 + (z1 - z0) * k / APP_CAMPATH_PIECES;
        int sx;
        int sy;
        int const shown = app_world_project_at(app, x, z, y, &sx, &sy);

        if( shown && have )
            app_overlay_push_segment(app, px, py, sx, sy, color, width);
        px = sx;
        py = sy;
        have = shown;
    }
}

/** A keyframe's number beside its eye. */
static void
campath_number(
    struct App* app,
    int x,
    int y,
    int z,
    int number,
    uint32_t color)
{
    int const font_id = app_minimenu_font_scene_id(app);
    struct UITreeEntityOverlay text;
    int px;
    int py;

    if( font_id < 0 || !app_world_project_at(app, x, z, y, &px, &py) )
        return;
    memset(&text, 0, sizeof(text));
    text.kind = UITREE_ENTITY_OVERLAY_TEXT;
    text.font_id = font_id;
    text.x = px + 7;
    text.y = py - 5;
    text.color = 0xFF000000u;
    snprintf(text.text, sizeof(text.text), "%d", number);
    app_overlay_push(app, &text);
    text.x -= 1;
    text.y -= 1;
    text.color = color;
    app_overlay_push(app, &text);
}

void
app_overlay_build_editor_campath(struct App* app)
{
    static struct Editor_CamPathMark marks[EDITOR_CAMPATH_KEYS_MAX];
    struct Editor_Panel const* panel;
    int base_x;
    int base_z;
    int n;

    assert(app);
    panel = &app->editor_panel;
    if( !panel->ui || !app->world )
        return;
    n = Editor_CamPath_Marks(panel->ui, marks, EDITOR_CAMPATH_KEYS_MAX);
    base_x = app->world->_base_tile_x * 128;
    base_z = app->world->_base_tile_z * 128;
    for( int i = 0; i < n; i++ )
    {
        const struct Editor_CamPathMark* m = &marks[i];
        uint32_t const color = m->selected ? APP_CAMPATH_SELECTED_COLOR : APP_CAMPATH_TRACK_COLOR;
        int const ex = m->eye_x - base_x;
        int const ez = m->eye_z - base_z;
        int const h = APP_CAMPATH_MARK_HALF;

        /* The eye's track from the keyframe before: solid for a glide (the
         * eye travels it), thin for a cut (it does not). */
        if( i > 0 )
            campath_line(app, marks[i - 1].eye_x - base_x, marks[i - 1].eye_y, marks[i - 1].eye_z - base_z, ex, m->eye_y,
                ez, APP_CAMPATH_TRACK_COLOR, m->glide ? APP_OVERLAY_SEGMENT_WIDTH + 1 : 1);
        /* Where it looks, and how high it stands. */
        campath_line(app, ex, m->eye_y, ez, m->look_x - base_x, m->look_y, m->look_z - base_z,
            m->selected ? APP_CAMPATH_SELECTED_COLOR : APP_CAMPATH_LOOK_COLOR, m->selected ? APP_OVERLAY_SEGMENT_WIDTH : 1);
        campath_line(app, ex, m->eye_y, ez, ex, m->ground_y, ez, APP_CAMPATH_DROP_COLOR, 1);
        /* The marker: a diamond in the ground plane's directions and a
         * post, so it reads as a point in space from any side. */
        campath_line(app, ex - h, m->eye_y, ez, ex, m->eye_y, ez + h, color, APP_OVERLAY_SEGMENT_WIDTH);
        campath_line(app, ex, m->eye_y, ez + h, ex + h, m->eye_y, ez, color, APP_OVERLAY_SEGMENT_WIDTH);
        campath_line(app, ex + h, m->eye_y, ez, ex, m->eye_y, ez - h, color, APP_OVERLAY_SEGMENT_WIDTH);
        campath_line(app, ex, m->eye_y, ez - h, ex - h, m->eye_y, ez, color, APP_OVERLAY_SEGMENT_WIDTH);
        campath_line(app, ex, m->eye_y - h, ez, ex, m->eye_y + h, ez, color, APP_OVERLAY_SEGMENT_WIDTH);
        campath_number(app, ex, m->eye_y, ez, m->index + 1, color);
    }
}
