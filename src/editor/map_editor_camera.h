#ifndef SRC_EDITOR_MAP_EDITOR_CAMERA_H
#define SRC_EDITOR_MAP_EDITOR_CAMERA_H

/*
 * The map editor camera's latches (app/app_mapeditor_camera.c): the
 * Unity-style scheme's drags and the grab pan. Part of the map editor's
 * record (editor/map_editor_app.h `camera`); zeroed with it.
 *
 * docs/MAPED_HOTKEYS.md "Camera". Plain data: nothing here reads input or
 * moves a camera.
 */

/** The Unity-style scheme's drags: a look (right), a pan (middle) or an
 *  orbit (Alt+left), latched by app_camera_unity_gate. */
struct MapEditorCameraUnity
{
    int look_active;
    int pan_active;
    int orbit_active;
    /** The pointer the last delta was measured from. */
    int last_x;
    int last_y;
    /** How far the pointer moved since the press (a right CLICK under a
     *  few px keeps the minimenu; past it the menu is the drag's casualty). */
    int travel;
    /** The orbit's pivot: scene fine units and its ground, the eye's
     *  distance from it, and the pose's angles (the camera's, kept here so
     *  the pitch clamp is applied to a running value). */
    int pivot_x;
    int pivot_y;
    int pivot_z;
    int pivot_dist;
    int yaw;
    int pitch;
};

/* The grab pan (docs/MAPED_HOTKEYS.md "Pan (Space)"): Space held over the
 * World, then a left-drag, under either scheme -- or Alt+Ctrl+left-drag
 * (Option+Cmd) under the Unity scheme. One latch for both: the press
 * starts it over the world, the drag then keeps the pointer until the left
 * button comes up, whether or not the key is still down. */
struct MapEditorCameraGrab
{
    int active;
    int last_x;
    int last_y;
    /** Space has been held since an earlier frame, and whether a grab
     *  started while it was: a tap with no drag under the Place tool says
     *  where Place here went (Space was its chord until 2026-11-07). */
    int space_held;
    int space_dragged;
    /** Under the Unity scheme Alt+Ctrl is held over the world this frame
     *  (app_camera_unity_gate): the cursor shows the pan before a press. */
    int unity_chord_over_world;
};

struct MapEditorCamera
{
    struct MapEditorCameraUnity unity;
    struct MapEditorCameraGrab grab;
};

#endif
