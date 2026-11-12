#ifndef SRC_APP_APP_EDITOR_CAMPATH_H
#define SRC_APP_APP_EDITOR_CAMPATH_H

/**
 * The Camera path panel's App glue (app_editor_campath.c): three
 * EditorUiAppServices members and the path's overlay. `user_data` is the
 * App, as for every service.
 */

struct App;
struct Editor_CamEye;

/** EditorUiAppServices::world_eye: the World panel's camera as it stands --
 *  the eye in ABSOLUTE fine units (y the scene's, down), its yaw and pitch.
 *  0 when the window has no world. */
int
EditorCamPath_AppEye(
    void* user_data,
    struct Editor_CamEye* out);

/** EditorUiAppServices::world_eye_set: the free camera put exactly there,
 *  angles and all. 0 when the eye is outside the loaded scene. */
int
EditorCamPath_AppEyeSet(
    void* user_data,
    const struct Editor_CamEye* eye);

/** EditorUiAppServices::world_ground (an Editor_CamGroundFn): the ground's y
 *  under ABSOLUTE fine (x, z) on `level` as the world draws it -- the height
 *  cam_moveto and cam_lookat measure from. 0 outside the loaded scene. */
int
EditorCamPath_AppGround(
    void* user_data,
    int fine_x,
    int fine_z,
    int level,
    int* out_y);

/** The shown path over the World (Editor_CamPath_Marks): the eye's track, a
 *  marker and a number per keyframe, a line from each eye to its look point
 *  and one down to the ground under it. Called with the editor's other
 *  overlays (app_overlay.c). */
void
app_overlay_build_editor_campath(struct App* app);

#endif
