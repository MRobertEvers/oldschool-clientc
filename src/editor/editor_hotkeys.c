/*
 * The hotkey table, its chords, its ini codec and the per-frame resolver.
 * editor_hotkeys.h has the contract; docs/MAPED_HOTKEYS.md is the table as the
 * user reads it (editor_hotkeys_test checks every action is there).
 */

#include "editor/editor_hotkeys.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G EDITOR_HK_CTX_G
#define W EDITOR_HK_CTX_W
#define L EDITOR_HK_CTX_L
#define R EDITOR_HK_CTX_R
#define S EDITOR_HK_CTX_S
#define N EDITOR_HK_CTX_N
#define M EDITOR_HK_CTX_M
#define P EDITOR_HK_CTX_P
#define K EDITOR_HK_CTX_K
#define Q EDITOR_HK_CTX_Q
#define D EDITOR_HK_CTX_D
#define U EDITOR_HK_CTX_U
#define EDGE EDITOR_HK_EDGE
#define REPEAT EDITOR_HK_REPEAT
#define HELD EDITOR_HK_HELD

/* id, group, name, doc row, contexts, kind, { defaults }, panel */
static const struct Editor_HotkeyActionInfo k_actions[EDITOR_HK_COUNT] = {
    [EDITOR_HK_FILE_NEW] = { "file.new", "File", "New square...", "New square...", G, EDGE, { "ctrl+n" }, NULL },
    [EDITOR_HK_FILE_SAVE_ALL] = { "file.save_all", "File", "Save all", "Save all", G, EDGE, { "ctrl+s" }, NULL },
    [EDITOR_HK_FILE_SAVE_AS] = { "file.save_as", "File", "Save square as new...", "Save square as new...", G, EDGE,
                                 { "ctrl+shift+s" }, NULL },
    [EDITOR_HK_FILE_BAKE] = { "file.bake", "File", "Bake cache (slow)", "Bake cache (slow)", G, EDGE, { NULL }, NULL },
    [EDITOR_HK_FILE_CLOSE] = { "file.close", "File", "Close editor", "Close editor", G, EDGE, { NULL }, NULL },
    [EDITOR_HK_FILE_CHECK_SCRIPTS] = { "file.check_scripts", "File", "Check scripts",
                                       "Check the scripts on disk (sscompile)", G, EDGE, { "ctrl+shift+b" },
                                       NULL },
    [EDITOR_HK_FILE_RUN] = { "file.run", "File", "Run (save, build, reload)", "Run: save all, build, reload live",
                             G, EDGE, { "ctrl+return" }, NULL },
    [EDITOR_HK_FILE_PUBLISH] = { "file.publish", "File", "Publish...", "Publish the lane's changes to a lane", G,
                                 EDGE, { "ctrl+shift+u" }, "publish" },
    [EDITOR_HK_FILE_SAVE_SCENE] = { "file.save_scene", "File", "Save scene",
                                    "Save the session's state as a scene", G, EDGE, { "ctrl+shift+k" }, NULL },
    [EDITOR_HK_FILE_SCRATCH_LANE] = { "file.scratch_lane", "File", "Scratch lane",
                                      "Make a scratch lane from main; it opens at the next launch", G, EDGE,
                                      { "ctrl+shift+n" }, NULL },
    [EDITOR_HK_FILE_OPEN_SQUARES] = { "file.open_squares", "File", "Open squares...",
                                      "Open up to 3x3 squares, swept on the Square map", G, EDGE, { "ctrl+o" },
                                      NULL },

    [EDITOR_HK_EDIT_UNDO] = { "edit.undo", "Edit", "Undo", "Undo", G, EDGE, { "ctrl+z" }, NULL },
    [EDITOR_HK_EDIT_REDO] = { "edit.redo", "Edit", "Redo", "Redo", G, EDGE, { "ctrl+shift+z", "ctrl+y" }, NULL },
    [EDITOR_HK_EDIT_DESELECT] = { "edit.deselect", "Edit", "Deselect / cancel", "Deselect / cancel", G, EDGE,
                                  { "escape" }, NULL },
    [EDITOR_HK_EDIT_DELETE] = { "edit.delete", "Edit", "Delete selection", "Delete selection", L | S, EDGE,
                                { "delete", "backspace" }, NULL },
    [EDITOR_HK_EDIT_DUPLICATE] = { "edit.duplicate", "Edit", "Duplicate", "Duplicate (locs", L | S, EDGE,
                                   { "ctrl+d" }, NULL },
    [EDITOR_HK_EDIT_ROTATE] = { "edit.rotate", "Edit", "Rotate", "Rotate (selected locs", G, EDGE, { "q" }, NULL },
    [EDITOR_HK_EDIT_RESHAPE] = { "edit.reshape", "Edit", "Reshape", "Reshape (selected loc", G, EDGE,
                                 { "shift+q" }, NULL },
    [EDITOR_HK_EDIT_APPLY] = { "edit.apply", "Edit", "Apply tool to selection",
                               "Apply the armed tool to the selection", L, EDGE, { "return" }, NULL },
    [EDITOR_HK_EDIT_SELECT_SQUARE] = { "edit.select_square", "Edit", "Select the square's locs",
                                       "Select every loc of the hovered tile's square", W, EDGE, { "ctrl+a" },
                                       NULL },
    [EDITOR_HK_EDIT_SELECT_SAME] = { "edit.select_same", "Edit", "Select same",
                                     "Select every loc of the hovered loc's id (select same", W | L, EDGE,
                                     { "alt+a" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_N] = { "edit.nudge_n", "Edit", "Nudge north", "Nudge selection N / S / W / E", M, REPEAT,
                                 { "ctrl+up" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_S] = { "edit.nudge_s", "Edit", "Nudge south", "Nudge selection N / S / W / E", M, REPEAT,
                                 { "ctrl+down" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_W] = { "edit.nudge_w", "Edit", "Nudge west", "Nudge selection N / S / W / E", M, REPEAT,
                                 { "ctrl+left" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_E] = { "edit.nudge_e", "Edit", "Nudge east", "Nudge selection N / S / W / E", M, REPEAT,
                                 { "ctrl+right" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_FAR_N] = { "edit.nudge_far_n", "Edit", "Nudge north 8", "Nudge selection 8 tiles N / S / W / E",
                                     M, REPEAT, { "ctrl+shift+up" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_FAR_S] = { "edit.nudge_far_s", "Edit", "Nudge south 8", "Nudge selection 8 tiles N / S / W / E",
                                     M, REPEAT, { "ctrl+shift+down" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_FAR_W] = { "edit.nudge_far_w", "Edit", "Nudge west 8", "Nudge selection 8 tiles N / S / W / E",
                                     M, REPEAT, { "ctrl+shift+left" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_FAR_E] = { "edit.nudge_far_e", "Edit", "Nudge east 8", "Nudge selection 8 tiles N / S / W / E",
                                     M, REPEAT, { "ctrl+shift+right" }, NULL },
    [EDITOR_HK_EDIT_LEVEL_UP] = { "edit.level_up", "Edit", "Selection up a level",
                                  "Move the selected locs a level up / down", L, EDGE, { "ctrl+pageup" }, NULL },
    [EDITOR_HK_EDIT_LEVEL_DOWN] = { "edit.level_down", "Edit", "Selection down a level",
                                    "Move the selected locs a level up / down", L, EDGE, { "ctrl+pagedown" }, NULL },
    [EDITOR_HK_EDIT_COPY] = { "edit.copy", "Edit", "Copy tiles", "Copy tiles", R, EDGE, { "ctrl+c" }, NULL },
    [EDITOR_HK_EDIT_PASTE] = { "edit.paste", "Edit", "Paste tiles", "Paste tiles at the hovered tile", W, EDGE,
                               { "ctrl+v" }, NULL },
    [EDITOR_HK_EDIT_FILL] = { "edit.fill", "Edit", "Fill tiles", "Fill tiles", R, EDGE, { "ctrl+shift+f" }, NULL },
    [EDITOR_HK_EDIT_CLEAR] = { "edit.clear", "Edit", "Clear tiles", "Clear tiles", R, EDGE, { "shift+delete" },
                               NULL },
    [EDITOR_HK_EDIT_MARK_AREA] = { "edit.mark_area", "Edit", "Mark area from rect", "Mark area from rect", R, EDGE,
                                   { "ctrl+shift+m" }, NULL },
    [EDITOR_HK_EDIT_SELECT_KIND] = { "edit.select_kind", "Edit", "Select all of the picked kind",
                                     "Select every loc of the catalog's picked loc in the open squares", G, EDGE,
                                     { "ctrl+shift+l" }, NULL },
    [EDITOR_HK_EDIT_INVERT] = { "edit.invert", "Edit", "Invert selection",
                                "Invert selection (over the open squares' locs on the selection's level)", G, EDGE,
                                { "ctrl+i" }, NULL },

    [EDITOR_HK_PLACE_HERE] = { "place.here", "Place", "Place at the hovered tile",
                               "Place the catalog's loc at the hovered tile (the ghost's pose)", P, EDGE,
                               { "return" }, NULL },
    [EDITOR_HK_PLACE_FIT] = { "place.fit", "Place", "Fit walls to neighbours (toggle)",
                              "Fit walls to neighbours on/off (Place tool)", G, EDGE, { "g" }, NULL },
    [EDITOR_HK_PLACE_SWAP] = { "place.swap", "Place", "Swap to catalog pick",
                               "Swap the selected locs to the catalog's pick", L, EDGE, { "e" }, NULL },
    [EDITOR_HK_PLACE_CONSTRAIN] = { "place.constrain", "Place", "Constrain a drag to one axis (hold)",
                                    "Keep a Place drag (a wall run) to one axis (hold)", W, HELD, { "shift" },
                                    NULL },
    [EDITOR_HK_TOOL_SELECT] = { "tool.select", "Tools", "Select", "| Select |", G, EDGE, { "1" }, NULL },
    [EDITOR_HK_TOOL_RECT] = { "tool.rect", "Tools", "Rectangle", "| Rectangle |", G, EDGE, { "2" }, NULL },
    [EDITOR_HK_TOOL_HEIGHT] = { "tool.height", "Tools", "Height", "| Height |", G, EDGE, { "3" }, NULL },
    [EDITOR_HK_TOOL_FLATTEN] = { "tool.flatten", "Tools", "Flatten", "| Flatten |", G, EDGE, { "5" }, NULL },
    [EDITOR_HK_TOOL_SMOOTH] = { "tool.smooth", "Tools", "Smooth", "| Smooth |", G, EDGE, { "6" }, NULL },
    [EDITOR_HK_TOOL_PAINT_UNDERLAY] = { "tool.paint_underlay", "Tools", "Paint underlay", "Paint underlay", G, EDGE,
                                        { "7" }, NULL },
    [EDITOR_HK_TOOL_PAINT_OVERLAY] = { "tool.paint_overlay", "Tools", "Paint overlay", "Paint overlay", G, EDGE,
                                       { "8" }, NULL },
    [EDITOR_HK_TOOL_PAINT_FLAGS] = { "tool.paint_flags", "Tools", "Paint flags", "Paint flags", G, EDGE, { "9" },
                                     NULL },
    [EDITOR_HK_TOOL_PLACE] = { "tool.place", "Tools", "Place loc / wall run", "Place loc", G, EDGE, { "0" }, NULL },
    [EDITOR_HK_TOOL_MOVE] = { "tool.move", "Tools", "Move loc", "Move loc", G, EDGE, { "m" }, NULL },
    [EDITOR_HK_TOOL_ROTATE] = { "tool.rotate", "Tools", "Rotate loc (tool)", "Rotate loc (tool)", G, EDGE, { "t" },
                                NULL },
    [EDITOR_HK_TOOL_DELETE] = { "tool.delete", "Tools", "Delete loc (tool)", "Delete loc (tool)", G, EDGE, { "x" },
                                NULL },
    [EDITOR_HK_TOOL_PLACE_PLAYER] = { "tool.place_player", "Tools", "Place player",
                                      "Place player (the lane's play account onto the clicked tile)", G, EDGE,
                                      { "p" }, NULL },
    [EDITOR_HK_TOOL_PAINT_PATH] = { "tool.paint_path", "Tools", "Paint path",
                                    "Paint path (the palette's overlay laid along a drag)", G, EDGE,
                                    { "h" }, NULL },
    [EDITOR_HK_TOOL_NOISE] = { "tool.noise", "Tools", "Roughen",
                               "Roughen (seeded height noise under the brush, the step its amplitude)", G, EDGE,
                               { "u" }, NULL },
    [EDITOR_HK_TOOL_SCATTER] = { "tool.scatter", "Tools", "Scatter locs",
                                 "Scatter locs (the selected locs' ids, else the catalog's pick, dropped on the brush's free tiles at a density; Alt-drag takes them off)",
                                 G, EDGE, { "b" }, NULL },
    [EDITOR_HK_TOOL_PLATEAU] = { "tool.plateau", "Tools", "Plateau",
                                 "Plateau (the brush flattened to the first tile's height, a smoothed skirt around it: a mound or a hole with a flat top)",
                                 G, EDGE, { "v" }, NULL },
    [EDITOR_HK_TOOL_REGION] = { "tool.region", "Tools", "Region brush",
                                "Region brush (the palette's overlay under a swept disc, its edge cut to the tile shapes; Alt-drag takes it off)",
                                G, EDGE, { "o" }, NULL },
    [EDITOR_HK_TOOL_LASSO] = { "tool.lasso", "Tools", "Lasso",
                               "Lasso (a drawn loop, or clicked corners, filled with the palette's overlay, its edge cut to the tile shapes)",
                               G, EDGE, { "j" }, NULL },
    [EDITOR_HK_TOOL_TIDY] = { "tool.tidy", "Tools", "Tidy",
                              "Tidy (click an overlay: its run's stepped edge smoothed and refitted to the tile shapes)",
                              G, EDGE, { "y" }, NULL },
    [EDITOR_HK_TOOL_PREVIOUS] = { "tool.previous", "Tools", "Previous tool", "Previous tool", G, EDGE, { "grave" },
                                  NULL },
    [EDITOR_HK_TOOL_PICK_PAINT] = { "tool.pick_paint", "Tools", "Pick the tile's paint",
                                    "Pick the hovered tile's paint into the palette", W, EDGE, { "i" }, NULL },
    [EDITOR_HK_TOOL_PICK_LOC] = { "tool.pick_loc", "Tools", "Pick the hovered loc",
                                  "Pick the hovered loc into the catalog", W, EDGE, { "shift+i" }, NULL },
    [EDITOR_HK_HOLD_PLACE] = { "hold.place", "Tools", "Click places a loc (hold)",
                               "Place the catalog's loc on click (hold)", W, HELD, { "l" }, NULL },
    [EDITOR_HK_HOLD_DELETE] = { "hold.delete", "Tools", "Click deletes a loc (hold)",
                                "Delete the loc under the click (hold)", W, HELD, { "k" }, NULL },
    [EDITOR_HK_HOLD_CLEAR] = { "hold.clear", "Tools", "Click clears the tile (hold)",
                               "Clear every loc on the clicked tile (hold)", W, HELD, { "c" }, NULL },

    [EDITOR_HK_BRUSH_RADIUS_DOWN] = { "brush.radius_down", "Brush", "Radius -", "Radius - / +", G, REPEAT,
                                      { "lbracket" }, NULL },
    [EDITOR_HK_BRUSH_RADIUS_UP] = { "brush.radius_up", "Brush", "Radius +", "Radius - / +", G, REPEAT,
                                    { "rbracket" }, NULL },
    [EDITOR_HK_BRUSH_STRENGTH_DOWN] = { "brush.strength_down", "Brush", "Strength -", "Strength - / +", G, REPEAT,
                                        { "shift+lbracket" }, NULL },
    [EDITOR_HK_BRUSH_STRENGTH_UP] = { "brush.strength_up", "Brush", "Strength +", "Strength - / +", G, REPEAT,
                                      { "shift+rbracket" }, NULL },
    [EDITOR_HK_BRUSH_STEP_DOWN] = { "brush.step_down", "Brush", "Step -", "Step - / +", G, REPEAT,
                                    { "ctrl+lbracket" }, NULL },
    [EDITOR_HK_BRUSH_STEP_UP] = { "brush.step_up", "Brush", "Step +", "Step - / +", G, REPEAT, { "ctrl+rbracket" },
                                  NULL },

    [EDITOR_HK_LEVEL_DOWN] = { "level.down", "Level", "Level down", "Level down / up", G, EDGE, { "pagedown" },
                               NULL },
    [EDITOR_HK_LEVEL_UP] = { "level.up", "Level", "Level up", "Level down / up", G, EDGE, { "pageup" }, NULL },
    /* Workbench 6.8: the pins moved to Alt+0..5 (Ctrl+1..4 are the
     * workspaces). Ctrl+0 and Ctrl+5, which nothing took, stay as the second
     * chord for one release (Editor_HotkeyRetiredNote). */
    [EDITOR_HK_LEVEL_PIN0] = { "level.pin0", "Level", "Pin level 0", "Pin level 0..3", G, EDGE,
                               { "alt+0", "ctrl+0" }, NULL },
    [EDITOR_HK_LEVEL_PIN1] = { "level.pin1", "Level", "Pin level 1", "Pin level 0..3", G, EDGE, { "alt+1" }, NULL },
    [EDITOR_HK_LEVEL_PIN2] = { "level.pin2", "Level", "Pin level 2", "Pin level 0..3", G, EDGE, { "alt+2" }, NULL },
    [EDITOR_HK_LEVEL_PIN3] = { "level.pin3", "Level", "Pin level 3", "Pin level 0..3", G, EDGE, { "alt+3" }, NULL },
    [EDITOR_HK_LEVEL_AUTO] = { "level.auto", "Level", "Level Auto", "Level Auto", G, EDGE, { "alt+4" }, NULL },
    [EDITOR_HK_LEVEL_ABOVE] = { "level.above", "Level", "Show above", "Show above on/off", G, EDGE,
                                { "alt+5", "ctrl+5" }, NULL },

    [EDITOR_HK_CAM_FORWARD] = { "camera.forward", "Camera", "Fly forward", "Fly forward / back / left / right", G,
                                HELD, { "w" }, NULL, "2 fingers" },
    [EDITOR_HK_CAM_BACK] = { "camera.back", "Camera", "Fly back", "Fly forward / back / left / right", G, HELD,
                             { "s" }, NULL, "2 fingers" },
    [EDITOR_HK_CAM_LEFT] = { "camera.left", "Camera", "Fly left", "Fly forward / back / left / right", G, HELD,
                             { "a" }, NULL, "2 fingers" },
    [EDITOR_HK_CAM_RIGHT] = { "camera.right", "Camera", "Fly right", "Fly forward / back / left / right", G, HELD,
                              { "d" }, NULL, "2 fingers" },
    [EDITOR_HK_CAM_DOWN] = { "camera.down", "Camera", "Fly down", "Fly down / up", D, HELD, { "f" }, NULL, "pinch" },
    [EDITOR_HK_CAM_UP] = { "camera.up", "Camera", "Fly up", "Fly down / up", G, HELD, { "r" }, NULL, "pinch" },
    [EDITOR_HK_CAM_FAST] = { "camera.fast", "Camera", "Fly faster", "Fly faster", G, HELD, { "shift" }, NULL },
    [EDITOR_HK_CAM_TURN_LEFT] = { "camera.turn_left", "Camera", "Turn left", "Turn left / right, tilt up / down", G,
                                  HELD, { "left" }, NULL, "twist" },
    [EDITOR_HK_CAM_TURN_RIGHT] = { "camera.turn_right", "Camera", "Turn right", "Turn left / right, tilt up / down",
                                   G, HELD, { "right" }, NULL, "twist" },
    [EDITOR_HK_CAM_TILT_UP] = { "camera.tilt_up", "Camera", "Tilt up", "Turn left / right, tilt up / down", G, HELD,
                                { "up" }, NULL },
    [EDITOR_HK_CAM_TILT_DOWN] = { "camera.tilt_down", "Camera", "Tilt down", "Turn left / right, tilt up / down", G,
                                  HELD, { "down" }, NULL },
    [EDITOR_HK_CAM_FRAME] = { "camera.frame", "Camera", "Frame the selection",
                              "Frame the selection, else the hovered square", G, EDGE, { "home" }, NULL },
    [EDITOR_HK_CAM_FRAME_SQUARE] = { "camera.frame_square", "Camera", "Frame the square's centre",
                                     "Frame the current square's centre", G, EDGE, { "ctrl+home" }, NULL },
    [EDITOR_HK_CAM_FRAME_KEY] = { "camera.frame_key", "Camera", "Frame the selection (Unity scheme)",
                                  "Frame the selection, else the hovered square (Unity scheme)", U, EDGE, { "f" },
                                  NULL },
    [EDITOR_HK_CAM_SCHEME] = { "camera.scheme", "Camera", "Camera scheme: default / Unity (toggle)",
                               "Camera scheme: default / Unity (toggle)", G, EDGE, { NULL }, NULL },
    /* Space is the pan key of every image and level editor (Photoshop,
     * Figma, Godot, Hammer); it was Place here's second chord until
     * 2026-11-07, and Return and a click still place. */
    [EDITOR_HK_CAM_PAN] = { "camera.pan", "Camera", "Pan (hold, then drag)",
                            "Pan: drag the ground with the left button (held)", W, HELD, { "space" }, NULL },

    [EDITOR_HK_PICKER_NORTH] = { "picker.north", "Picker", "Focus north", "Move the focused square N / S / W / E",
                                 K, REPEAT, { "up" }, NULL },
    [EDITOR_HK_PICKER_SOUTH] = { "picker.south", "Picker", "Focus south", "Move the focused square N / S / W / E",
                                 K, REPEAT, { "down" }, NULL },
    [EDITOR_HK_PICKER_WEST] = { "picker.west", "Picker", "Focus west", "Move the focused square N / S / W / E", K,
                                REPEAT, { "left" }, NULL },
    [EDITOR_HK_PICKER_EAST] = { "picker.east", "Picker", "Focus east", "Move the focused square N / S / W / E", K,
                                REPEAT, { "right" }, NULL },
    [EDITOR_HK_PICKER_OPEN] = { "picker.open", "Picker", "Open the focused square",
                                "Open the focused square (an empty one: New square)", K, EDGE, { "return" }, NULL },
    [EDITOR_HK_PICKER_ADD] = { "picker.add", "Picker", "Add the focused square",
                               "Add the focused square to the open set", K, EDGE, { "shift+return" }, NULL },
    [EDITOR_HK_PICKER_BACK] = { "picker.back", "Picker", "Previous open square",
                                "Go to the previous square of the open set", K, EDGE, { "backspace" }, NULL },
    [EDITOR_HK_PICKER_CLEAR_FIND] = { "picker.clear_find", "Picker", "Clear Find",
                                      "Clear Find (and its pin)", K, EDGE, { "escape" }, NULL },

    [EDITOR_HK_PALETTE_SLOT_1] = { "palette.slot_1", "Palette", "Palette slot 1", "Palette slot 1", G, EDGE,
                                   { "shift+1" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_2] = { "palette.slot_2", "Palette", "Palette slot 2", "Palette slot 2", G, EDGE,
                                   { "shift+2" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_3] = { "palette.slot_3", "Palette", "Palette slot 3", "Palette slot 3", G, EDGE,
                                   { "shift+3" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_4] = { "palette.slot_4", "Palette", "Palette slot 4", "Palette slot 4", G, EDGE,
                                   { "shift+4" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_5] = { "palette.slot_5", "Palette", "Palette slot 5", "Palette slot 5", G, EDGE,
                                   { "shift+5" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_6] = { "palette.slot_6", "Palette", "Palette slot 6", "Palette slot 6", G, EDGE,
                                   { "shift+6" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_7] = { "palette.slot_7", "Palette", "Palette slot 7", "Palette slot 7", G, EDGE,
                                   { "shift+7" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_8] = { "palette.slot_8", "Palette", "Palette slot 8", "Palette slot 8", G, EDGE,
                                   { "shift+8" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_9] = { "palette.slot_9", "Palette", "Palette slot 9", "Palette slot 9", G, EDGE,
                                   { "shift+9" }, NULL },
    [EDITOR_HK_PALETTE_SLOT_10] = { "palette.slot_10", "Palette", "Palette slot 10", "Palette slot 10", G, EDGE,
                                   { "shift+0" }, NULL },
    [EDITOR_HK_PALETTE_ADD_PICK] = { "palette.add_pick", "Palette", "Add the pick to the palette",
                                     "Add the pick to the palette", G, EDGE, { "ctrl+shift+a" }, NULL },
    [EDITOR_HK_PALETTE_NEXT] = { "palette.next", "Palette", "Next palette", "Next palette", G, EDGE,
                                 { "alt+rbracket" }, NULL },
    [EDITOR_HK_PALETTE_PREVIOUS] = { "palette.previous", "Palette", "Previous palette", "Previous palette", G, EDGE,
                                     { "alt+lbracket" }, NULL },
    [EDITOR_HK_VIEW_HOTKEYS] = { "view.hotkeys", "View", "Hotkeys panel", "Hotkeys panel", G, EDGE, { "f1" },
                                 "hotkeys" },
    [EDITOR_HK_VIEW_TERRAIN] = { "view.terrain", "View", "Terrain", "| Terrain |", G, EDGE, { "f2" }, "terrain" },
    [EDITOR_HK_VIEW_PALETTE] = { "view.palette", "View", "Palette", "| Palette |", G, EDGE, { "f3" }, "palette" },
    [EDITOR_HK_VIEW_SQUARES] = { "view.squares", "View", "Squares", "| Squares |", G, EDGE, { "f4" }, "squares" },
    [EDITOR_HK_VIEW_LOCS] = { "view.locs", "View", "Locs", "| Locs |", G, EDGE, { "f5" }, "locs" },
    [EDITOR_HK_VIEW_SPAWNS] = { "view.spawns", "View", "Spawns", "| Spawns |", G, EDGE, { "f6" }, "spawns" },
    [EDITOR_HK_VIEW_SPAWN_PREVIEW] = { "view.spawn_preview", "View", "Show spawns in the world (toggle)",
                                       "Show every spawn of the open squares in the world, on/off", G, EDGE,
                                       { "shift+f6" }, NULL },
    [EDITOR_HK_VIEW_INSPECTOR] = { "view.inspector", "View", "Inspector", "| Inspector |", G, EDGE, { "f7" },
                                   "inspector" },
    [EDITOR_HK_VIEW_CATALOG] = { "view.catalog", "View", "Catalog", "| Catalog |", G, EDGE, { "f8" }, "catalog" },
    [EDITOR_HK_VIEW_PREVIEW] = { "view.preview", "View", "Preview", "| Preview (the Catalog pick's model) |", G,
                                 EDGE, { "ctrl+shift+p" }, "preview" },
    [EDITOR_HK_VIEW_GALLERY] = { "view.gallery", "View", "Gallery", "| Gallery (the Catalog's search as pictures) |", G,
                                 EDGE, { "shift+f8" }, "gallery" },
    [EDITOR_HK_VIEW_CATALOG_PALETTE] = { "view.catalog_palette", "View", "Catalog palette",
                                         "| Catalog palette (slots of locs, npcs and objs) |", G, EDGE, { NULL },
                                         "catalog_palette" },
    [EDITOR_HK_VIEW_CONSOLE] = { "view.console", "View", "Console (Output)", "| Console |", G, EDGE, { "f9" },
                                 "console" },
    [EDITOR_HK_VIEW_HISTORY] = { "view.history", "View", "History", "| History |", G, EDGE, { "f10" }, "history" },
    [EDITOR_HK_VIEW_AREAS] = { "view.areas", "View", "Areas", "| Areas |", G, EDGE, { "f11" }, "areas" },
    [EDITOR_HK_VIEW_WORLD] = { "view.world", "View", "World", "| World |", G, EDGE, { "f12" }, "world" },
    [EDITOR_HK_VIEW_TOOLRAIL] = { "view.toolrail", "View", "Tools (rail)", "Every other panel the View menu lists",
                                  G, EDGE, { NULL }, "toolrail" },
    [EDITOR_HK_VIEW_RECT] = { "view.rect", "View", "Rectangle panel", "Every other panel the View menu lists", G,
                              EDGE, { NULL }, "rect" },
    [EDITOR_HK_VIEW_RECORD] = { "view.record", "View", "Record", "Every other panel the View menu lists", G, EDGE,
                                { NULL }, "record" },
    [EDITOR_HK_VIEW_LOCBUILDER] = { "view.locbuilder", "View", "Loc builder",
                                    "Every other panel the View menu lists", G, EDGE, { NULL }, "locbuilder" },
    [EDITOR_HK_VIEW_NPCBUILDER] = { "view.npcbuilder", "View", "NPC editor",
                                    "Every other panel the View menu lists", G, EDGE, { NULL }, "npcbuilder" },
    [EDITOR_HK_VIEW_SHELL] = { "view.shelleditor", "View", "Shell editor", "Every other panel the View menu lists",
                               G, EDGE, { NULL }, "shelleditor" },
    [EDITOR_HK_VIEW_IFACE] = { "view.iface", "View", "Interface editor", "Every other panel the View menu lists", G,
                               EDGE, { NULL }, "iface" },
    [EDITOR_HK_VIEW_CS2] = { "view.cs2", "View", "CS2", "Every other panel the View menu lists", G, EDGE, { NULL },
                             "cs2" },
    [EDITOR_HK_VIEW_LIVE] = { "view.live", "View", "Live", "Every other panel the View menu lists", G, EDGE,
                              { NULL }, "live" },
    [EDITOR_HK_VIEW_ACCOUNTS] = { "view.accounts", "View", "Accounts", "Every other panel the View menu lists", G,
                                  EDGE, { NULL }, "accounts" },
    [EDITOR_HK_VIEW_TRIGGERS] = { "view.triggers", "View", "Triggers", "| Triggers |", G, EDGE, { "ctrl+e" },
                                  "triggers" },
    [EDITOR_HK_VIEW_SCENES] = { "view.scenes", "View", "Scenes", "Every other panel the View menu lists", G, EDGE,
                                { NULL }, "scenes" },
    [EDITOR_HK_VIEW_ENCOUNTER] = { "view.encounter", "View", "Encounters", "Every other panel the View menu lists",
                                   G, EDGE, { NULL }, "encounter" },
    [EDITOR_HK_VIEW_SHOP] = { "view.shop", "View", "Shop builder", "Every other panel the View menu lists", G, EDGE,
                              { NULL }, "shop" },
    [EDITOR_HK_VIEW_QUEST] = { "view.quest", "View", "Quest board", "Every other panel the View menu lists", G, EDGE,
                               { NULL }, "quest" },
    [EDITOR_HK_VIEW_PROBLEMS] = { "view.problems", "View", "Problems", "Every other panel the View menu lists", G,
                                  EDGE, { NULL }, "problems" },
    [EDITOR_HK_VIEW_HIDE_PANELS] = { "view.hide_panels", "View", "Hide / show panels",
                                     "Hide / show every panel but World", G, EDGE, { "tab" }, NULL },
    [EDITOR_HK_VIEW_RESET_LAYOUT] = { "view.reset_layout", "View", "Reset layout", "Reset layout", G, EDGE,
                                      { "ctrl+shift+r" }, NULL },
    [EDITOR_HK_VIEW_SNAP] = { "view.snap", "View", "Snap to grid", "Snap to grid (toggle)", G, EDGE, { "ctrl+g" }, NULL },
    [EDITOR_HK_VIEW_COMPACT] = { "view.compact", "View", "Compact panels",
                                 "Compact panels: dock members collapse to their title bars when the column is short "
                                 "(toggle; automatic on small windows, Reset layout returns to automatic)",
                                 G, EDGE, { "ctrl+shift+c" }, NULL },
    [EDITOR_HK_VIEW_SNAP_HOLD] = { "view.snap_hold", "View", "Snap while dragging (hold)",
                                   "Snap while dragging a panel (hold; the toggle is untouched)", G, HELD,
                                   { "ctrl" }, NULL },
    [EDITOR_HK_VIEW_POPOUT] = { "view.popout", "View", "Pop out panel",
                                "Pop out the panel under the pointer into its own window (World when over none)", G,
                                EDGE, { "shift+f12" }, NULL },
    [EDITOR_HK_VIEW_CLOSE_PANEL] = { "view.close_panel", "View", "Close panel",
                                     "Hide the panel under the pointer (the View menu brings it back)", G, EDGE,
                                     { "ctrl+w" }, NULL },
    [EDITOR_HK_VIEW_SCALE_AUTO] = { "view.scale_auto", "View", "Chrome scale Auto",
                                    "Chrome scale: Auto (the display's device pixel ratio)", G, EDGE, { NULL }, NULL },
    [EDITOR_HK_VIEW_SCALE_100] = { "view.scale_100", "View", "Chrome scale 100 %", "Chrome scale: 100 %", G, EDGE,
                                   { NULL }, NULL },
    [EDITOR_HK_VIEW_SCALE_125] = { "view.scale_125", "View", "Chrome scale 125 %", "Chrome scale: 125 %", G, EDGE,
                                   { NULL }, NULL },
    [EDITOR_HK_VIEW_SCALE_150] = { "view.scale_150", "View", "Chrome scale 150 %", "Chrome scale: 150 %", G, EDGE,
                                   { NULL }, NULL },
    [EDITOR_HK_VIEW_SCALE_175] = { "view.scale_175", "View", "Chrome scale 175 %", "Chrome scale: 175 %", G, EDGE,
                                   { NULL }, NULL },
    [EDITOR_HK_VIEW_SCALE_200] = { "view.scale_200", "View", "Chrome scale 200 %", "Chrome scale: 200 %", G, EDGE,
                                   { NULL }, NULL },
    [EDITOR_HK_VIEW_SCALE_250] = { "view.scale_250", "View", "Chrome scale 250 %", "Chrome scale: 250 %", G, EDGE,
                                   { NULL }, NULL },
    [EDITOR_HK_VIEW_SCALE_300] = { "view.scale_300", "View", "Chrome scale 300 %", "Chrome scale: 300 %", G, EDGE,
                                   { NULL }, NULL },
    [EDITOR_HK_VIEW_TEXT_NORMAL] = { "view.text_normal", "View", "Text size Normal",
                                     "Text size: Normal (the Body face, 18 px rows)", G, EDGE, { NULL }, NULL },
    [EDITOR_HK_VIEW_TEXT_LARGE] = { "view.text_large", "View", "Text size Large",
                                    "Text size: Large (the Menu face, 22 px rows)", G, EDGE, { NULL }, NULL },
    [EDITOR_HK_VIEW_TEXT_BITMAP] = { "view.text_bitmap", "View", "Text rendering bitmap",
                                     "Text rendering: the pixel font at 100, 200 and 300 %, smooth between",
                                     G, EDGE, { NULL }, NULL },
    [EDITOR_HK_VIEW_TEXT_SMOOTH] = { "view.text_smooth", "View", "Text rendering smooth",
                                     "Text rendering: the smooth vector face at every scale", G, EDGE,
                                     { NULL }, NULL },
    [EDITOR_HK_VIEW_WORKSPACE_BUILD] = { "view.workspace_build", "View", "Workspace Build",
                                         "Workspace: Build (placing locs and spawns; the default)", G, EDGE,
                                         { "ctrl+1" }, NULL },
    [EDITOR_HK_VIEW_WORKSPACE_TERRAIN] = { "view.workspace_terrain", "View", "Workspace Terrain",
                                           "Workspace: Terrain (heights, colours, shapes, flags)", G, EDGE,
                                           { "ctrl+2" }, NULL },
    [EDITOR_HK_VIEW_WORKSPACE_SCRIPT] = { "view.workspace_script", "View", "Workspace Script",
                                          "Workspace: Script (records, triggers, builders)", G, EDGE,
                                          { "ctrl+3" }, NULL },
    [EDITOR_HK_VIEW_WORKSPACE_PLAYTEST] = { "view.workspace_playtest", "View", "Workspace Playtest",
                                            "Workspace: Playtest (RUN, live, accounts)", G, EDGE, { "ctrl+4" },
                                            NULL },
    [EDITOR_HK_VIEW_COMMAND_PALETTE] = { "view.command_palette", "View", "Command palette",
                                         "Command palette (actions, panels, squares, records, accounts)", G,
                                         EDGE, { "ctrl+k", "alt+space" }, "cmdpalette" },
    [EDITOR_HK_VIEW_GOTO] = { "view.goto_tile", "View", "Go to tile...",
                              "Go to tile... (the palette prefilled with coordinates)", G, EDGE, { "ctrl+shift+g" },
                              NULL },
    [EDITOR_HK_VIEW_LAYER_LOCS] = { "view.layer_locs", "View", "Layer Locs (reserved)",
                                    "World layer Locs on/off (reserved)", G, EDGE, { "alt+l" }, NULL },
    [EDITOR_HK_VIEW_LAYER_NPCS] = { "view.layer_npcs", "View", "Layer NPCs (reserved)",
                                    "World layer NPCs on/off (reserved)", G, EDGE, { "alt+n" }, NULL },
    [EDITOR_HK_VIEW_LAYER_OBJS] = { "view.layer_objs", "View", "Layer Objs (reserved)",
                                    "World layer Objs on/off (reserved)", G, EDGE, { "alt+o" }, NULL },
    [EDITOR_HK_VIEW_LAYER_COLLISION] = { "view.layer_collision", "View", "Layer Collision (reserved)",
                                         "World layer Collision on/off (reserved)", G, EDGE, { "alt+c" }, NULL },
    [EDITOR_HK_VIEW_LAYER_GRID] = { "view.layer_grid", "View", "Layer Grid (reserved)",
                                    "World layer Grid on/off (reserved)", G, EDGE, { "alt+g" }, NULL },
    [EDITOR_HK_GRAPH_AUTO_LAYOUT] = { "graph.auto_layout", "Graph", "Auto-layout the graph",
                                      "Lay the focused dialogue graph out", Q, EDGE, { "ctrl+l" }, NULL },
    [EDITOR_HK_GRAPH_NEW_NODE] = { "graph.new_node", "Graph", "New node",
                                   "New dialogue node after the selected one", Q, EDGE, { "n" }, NULL },
};

#undef G
#undef W
#undef L
#undef R
#undef S
#undef N
#undef M
#undef P
#undef K
#undef Q
#undef D
#undef U
#undef EDGE
#undef REPEAT
#undef HELD

/* ---- the table ------------------------------------------------------------- */

const struct Editor_HotkeyActionInfo*
Editor_HotkeyInfo(enum Editor_HotkeyAction action)
{
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    return &k_actions[action];
}

int
Editor_HotkeyFind(const char* id)
{
    assert(id);
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
        if( strcmp(k_actions[a].id, id) == 0 )
            return a;
    return -1;
}

int
Editor_HotkeyForPanel(const char* panel)
{
    assert(panel);
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
        if( k_actions[a].panel && strcmp(k_actions[a].panel, panel) == 0 )
            return a;
    return -1;
}

void
Editor_HotkeyContextText(
    unsigned contexts,
    char* out,
    int out_size)
{
    static const char* const k_names[EDITOR_HK_CTX_COUNT] = { "G", "W", "L", "R", "S", "no loc / not Select-Move",
        "M", "P", "K", "Q", "D", "U" };
    int used = 0;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( int i = 0; i < EDITOR_HK_CTX_COUNT; i++ )
    {
        if( !(contexts & (1u << i)) )
            continue;
        used += snprintf(out + used, (size_t)(out_size - used), "%s%s", used ? ", " : "", k_names[i]);
        if( used >= out_size )
            return;
    }
}

void
Editor_HotkeyContextLetters(
    unsigned contexts,
    char* out,
    int out_size)
{
    static const char k_letters[EDITOR_HK_CTX_COUNT + 1] = "GWLRSNMPKQDU";
    int used = 0;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( int i = 0; i < EDITOR_HK_CTX_COUNT; i++ )
    {
        if( !(contexts & (1u << i)) )
            continue;
        used += snprintf(out + used, (size_t)(out_size - used), "%s%c", used ? ", " : "", k_letters[i]);
        if( used >= out_size )
            return;
    }
}

/* L excludes S and P (no loc selected); M excludes N, S and P; K (the
 * picker holds the keyboard) excludes every other context, G too; D (the
 * default camera scheme) excludes U (the Unity scheme). */
static int
hk_ctx_exclusive_one(
    unsigned a,
    unsigned b)
{
    if( a == EDITOR_HK_CTX_K )
        return b != EDITOR_HK_CTX_K;
    if( a == EDITOR_HK_CTX_L )
        return b == EDITOR_HK_CTX_S || b == EDITOR_HK_CTX_P;
    if( a == EDITOR_HK_CTX_M )
        return b == EDITOR_HK_CTX_N || b == EDITOR_HK_CTX_S || b == EDITOR_HK_CTX_P;
    if( a == EDITOR_HK_CTX_D )
        return b == EDITOR_HK_CTX_U;
    return 0;
}

static int
hk_ctx_exclusive(
    unsigned a,
    unsigned b)
{
    return hk_ctx_exclusive_one(a, b) || hk_ctx_exclusive_one(b, a);
}

int
Editor_HotkeyContextsOverlap(
    unsigned a,
    unsigned b)
{
    for( int i = 0; i < EDITOR_HK_CTX_COUNT; i++ )
    {
        if( !(a & (1u << i)) )
            continue;
        for( int j = 0; j < EDITOR_HK_CTX_COUNT; j++ )
            if( (b & (1u << j)) && !hk_ctx_exclusive(1u << i, 1u << j) )
                return 1;
    }
    return 0;
}

static void
hk_set_defaults(struct Editor_Hotkeys* hk)
{
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        memset(&hk->bind[a], 0, sizeof(hk->bind[a]));
        for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
        {
            int ok;
            if( !k_actions[a].defaults[c] )
                continue;
            ok = Editor_ChordParse(k_actions[a].defaults[c], &hk->bind[a].chord[c]);
            /* A default that does not parse is a typo in the table above. */
            assert(ok);
            (void)ok;
        }
    }
}

void
Editor_HotkeysInit(struct Editor_Hotkeys* hk)
{
    assert(hk);
    memset(hk, 0, sizeof(*hk));
    hk_set_defaults(hk);
    hk->stamp_next = 1;
    hk->serial = 1;
}

void
Editor_HotkeysResetDefaults(struct Editor_Hotkeys* hk)
{
    assert(hk);
    hk_set_defaults(hk);
    memset(hk->repeat_held, 0, sizeof(hk->repeat_held));
    hk->serial++;
}

void
Editor_HotkeysSet(
    struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action,
    const struct Editor_Chord* chords,
    int count)
{
    assert(hk);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    assert(count >= 0);
    assert(count <= EDITOR_HK_CHORDS);
    assert(chords || count == 0);
    memset(&hk->bind[action].chord, 0, sizeof(hk->bind[action].chord));
    for( int c = 0; c < count; c++ )
        hk->bind[action].chord[c] = chords[c];
    hk->bind[action].stamp = hk->stamp_next++;
    hk->repeat_held[action] = 0;
    hk->serial++;
}

void
Editor_HotkeysSetSlot(
    struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action,
    int slot,
    struct Editor_Chord chord)
{
    struct Editor_HotkeyBinding* bind;
    int const other = 1 - slot;

    assert(hk);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    assert(slot >= 0);
    assert(slot < EDITOR_HK_CHORDS);
    bind = &hk->bind[action];
    if( chord.key == TORIRSK_UNKNOWN )
        chord.mods = 0;
    bind->chord[slot] = chord;
    /* One copy of a chord: the other slot holding the same is emptied. */
    if( chord.key != TORIRSK_UNKNOWN && bind->chord[other].key == chord.key && bind->chord[other].mods == chord.mods )
        bind->chord[other].key = bind->chord[other].mods = 0;
    /* Never a second chord without a first. */
    if( bind->chord[0].key == TORIRSK_UNKNOWN && bind->chord[1].key != TORIRSK_UNKNOWN )
    {
        bind->chord[0] = bind->chord[1];
        bind->chord[1].key = bind->chord[1].mods = 0;
    }
    bind->stamp = hk->stamp_next++;
    hk->repeat_held[action] = 0;
    hk->serial++;
}

int
Editor_HotkeysIsDefault(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action)
{
    assert(hk);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
    {
        struct Editor_Chord def = { 0, 0 };
        if( k_actions[action].defaults[c] )
            (void)Editor_ChordParse(k_actions[action].defaults[c], &def);
        if( def.key != hk->bind[action].chord[c].key || def.mods != hk->bind[action].chord[c].mods )
            return 0;
    }
    return 1;
}

int
Editor_HotkeyIsQualifier(enum Editor_HotkeyAction action)
{
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    return action == EDITOR_HK_CAM_FAST || action == EDITOR_HK_VIEW_SNAP_HOLD || action == EDITOR_HK_PLACE_CONSTRAIN;
}

/** Do actions a and b share a chord that makes them clash? Two qualifiers on
 *  the same bare modifier do not: each only changes its own gesture. */
static int
hk_share_chord(
    const struct Editor_Hotkeys* hk,
    int a,
    int b)
{
    int const qualifiers = Editor_HotkeyIsQualifier(a) && Editor_HotkeyIsQualifier(b);

    for( int i = 0; i < EDITOR_HK_CHORDS; i++ )
    {
        struct Editor_Chord const ca = hk->bind[a].chord[i];
        if( ca.key == TORIRSK_UNKNOWN )
            continue;
        for( int j = 0; j < EDITOR_HK_CHORDS; j++ )
        {
            struct Editor_Chord const cb = hk->bind[b].chord[j];
            if( cb.key != ca.key || cb.mods != ca.mods )
                continue;
            if( qualifiers && ca.mods == 0 && Editor_HotkeyKeyIsModifier(ca.key) )
                continue;
            return 1;
        }
    }
    return 0;
}

int
Editor_HotkeysClash(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action)
{
    assert(hk);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    for( int b = 0; b < EDITOR_HK_COUNT; b++ )
        if( b != (int)action && hk_share_chord(hk, action, b)
            && Editor_HotkeyContextsOverlap(k_actions[action].contexts, k_actions[b].contexts) )
            return b;
    return -1;
}

int
Editor_HotkeysMatch(
    const struct Editor_Hotkeys* hk,
    struct Editor_Chord chord,
    unsigned contexts)
{
    int best = -1;

    assert(hk);
    if( chord.key == TORIRSK_UNKNOWN )
        return -1;
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        if( !(k_actions[a].contexts & contexts) )
            continue;
        for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
        {
            struct Editor_Chord const bound = hk->bind[a].chord[c];
            if( bound.key != chord.key || bound.mods != chord.mods )
                continue;
            /* A clash goes to the later binding, as in the resolver. */
            if( best < 0 || hk->bind[a].stamp > hk->bind[best].stamp )
                best = a;
        }
    }
    return best;
}

int
Editor_HotkeysConflicts(
    const struct Editor_Hotkeys* hk,
    int* out_pairs,
    int max_pairs)
{
    int count = 0;

    assert(hk);
    assert(out_pairs || max_pairs == 0);
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
        for( int b = a + 1; b < EDITOR_HK_COUNT; b++ )
        {
            if( !hk_share_chord(hk, a, b)
                || !Editor_HotkeyContextsOverlap(k_actions[a].contexts, k_actions[b].contexts) )
                continue;
            if( count < max_pairs )
            {
                out_pairs[2 * count] = a;
                out_pairs[2 * count + 1] = b;
            }
            count++;
        }
    return count;
}

/* ---- chords ------------------------------------------------------------------ */

int
Editor_HotkeyKeyIsModifier(int key)
{
    return key == TORIRSK_CTRL || key == TORIRSK_SHIFT || key == TORIRSK_ALT;
}

static unsigned
hk_mod_of_key(int key)
{
    if( key == TORIRSK_CTRL )
        return EDITOR_HK_MOD_CTRL;
    if( key == TORIRSK_SHIFT )
        return EDITOR_HK_MOD_SHIFT;
    if( key == TORIRSK_ALT )
        return EDITOR_HK_MOD_ALT;
    return 0;
}

static int
hk_token_equals(
    const char* token,
    int len,
    const char* word)
{
    int i;
    for( i = 0; i < len && word[i]; i++ )
        if( tolower((unsigned char)token[i]) != word[i] )
            return 0;
    return i == len && word[i] == '\0';
}

int
Editor_ChordParse(
    const char* text,
    struct Editor_Chord* out)
{
    char token[32];
    const char* p;
    unsigned mods = 0;
    int key = TORIRSK_UNKNOWN;

    assert(text);
    assert(out);
    out->key = TORIRSK_UNKNOWN;
    out->mods = 0;
    while( *text == ' ' || *text == '\t' )
        text++;
    p = text;
    for( ;; )
    {
        const char* end = p;
        int len;
        while( *end && *end != '+' )
            end++;
        len = (int)(end - p);
        while( len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n') )
            len--;
        while( len > 0 && (*p == ' ' || *p == '\t') )
        {
            p++;
            len--;
        }
        if( len <= 0 || len >= (int)sizeof(token) )
            return 0;
        if( *end == '+' )
        {
            /* A modifier, then more. */
            if( hk_token_equals(p, len, "ctrl") || hk_token_equals(p, len, "cmd")
                || hk_token_equals(p, len, "control") )
                mods |= EDITOR_HK_MOD_CTRL;
            else if( hk_token_equals(p, len, "shift") )
                mods |= EDITOR_HK_MOD_SHIFT;
            else if( hk_token_equals(p, len, "alt") || hk_token_equals(p, len, "option") )
                mods |= EDITOR_HK_MOD_ALT;
            else
                return 0;
            p = end + 1;
            continue;
        }
        memcpy(token, p, (size_t)len);
        token[len] = '\0';
        if( hk_token_equals(token, len, "cmd") || hk_token_equals(token, len, "control") )
            key = TORIRSK_CTRL;
        else if( hk_token_equals(token, len, "option") )
            key = TORIRSK_ALT;
        else
            key = LibToriRS_KeyCodeFromName(token);
        break;
    }
    if( key == TORIRSK_UNKNOWN )
        return 0;
    /* "ctrl+ctrl" is ctrl. */
    mods &= ~hk_mod_of_key(key);
    out->key = (uint8_t)key;
    out->mods = (uint8_t)mods;
    return 1;
}

void
Editor_ChordFormat(
    struct Editor_Chord chord,
    char* out,
    int out_size)
{
    const char* name;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( chord.key == TORIRSK_UNKNOWN )
        return;
    name = LibToriRS_KeyCodeName((enum LibToriRS_KeyCode)chord.key);
    assert(name);
    snprintf(out, (size_t)out_size, "%s%s%s%s", (chord.mods & EDITOR_HK_MOD_CTRL) ? "ctrl+" : "",
        (chord.mods & EDITOR_HK_MOD_SHIFT) ? "shift+" : "", (chord.mods & EDITOR_HK_MOD_ALT) ? "alt+" : "", name);
}

static void
hk_key_display(
    int key,
    int glyphs,
    char* out,
    int out_size)
{
    static const struct
    {
        int key;
        const char* text;
    } k_names[] = {
        { TORIRSK_ESCAPE, "Esc" },     { TORIRSK_RETURN, "Enter" },   { TORIRSK_BACKSPACE, "Backspace" },
        { TORIRSK_INSERT, "Ins" },     { TORIRSK_DELETE, "Del" },     { TORIRSK_SHIFT, "Shift" },
        { TORIRSK_CTRL, "Ctrl" },      { TORIRSK_ALT, "Alt" },        { TORIRSK_TAB, "Tab" },
        { TORIRSK_SPACE, "Space" },    { TORIRSK_LEFT, "Left" },      { TORIRSK_RIGHT, "Right" },
        { TORIRSK_UP, "Up" },          { TORIRSK_DOWN, "Down" },      { TORIRSK_PAGE_UP, "PgUp" },
        { TORIRSK_PAGE_DOWN, "PgDn" }, { TORIRSK_COMMA, "," },        { TORIRSK_PERIOD, "." },
        { TORIRSK_MINUS, "-" },        { TORIRSK_EQUALS, "=" },       { TORIRSK_LBRACKET, "[" },
        { TORIRSK_RBRACKET, "]" },     { TORIRSK_SEMICOLON, ";" },    { TORIRSK_APOSTROPHE, "'" },
        { TORIRSK_GRAVE, "`" },        { TORIRSK_SLASH, "/" },        { TORIRSK_BACKSLASH, "\\" },
        { TORIRSK_HOME, "Home" },      { TORIRSK_END, "End" },
    };
    /* The keyboard's own symbols, where the keycap's face has them. */
    static const struct
    {
        int key;
        const char* text;
    } k_glyphs[] = {
        { TORIRSK_UP, "\xe2\x86\x91" },        { TORIRSK_DOWN, "\xe2\x86\x93" },
        { TORIRSK_LEFT, "\xe2\x86\x90" },      { TORIRSK_RIGHT, "\xe2\x86\x92" },
        { TORIRSK_RETURN, "\xe2\x86\xb5" },    { TORIRSK_DELETE, "\xe2\x8c\xa6" },
        { TORIRSK_BACKSPACE, "\xe2\x8c\xab" }, { TORIRSK_TAB, "\xe2\x87\xa5" },
    };
    const char* name;

    if( glyphs )
        for( size_t i = 0; i < sizeof(k_glyphs) / sizeof(k_glyphs[0]); i++ )
            if( k_glyphs[i].key == key )
            {
                snprintf(out, (size_t)out_size, "%s", k_glyphs[i].text);
                return;
            }
    for( size_t i = 0; i < sizeof(k_names) / sizeof(k_names[0]); i++ )
        if( k_names[i].key == key )
        {
            snprintf(out, (size_t)out_size, "%s", k_names[i].text);
            return;
        }
    name = LibToriRS_KeyCodeName((enum LibToriRS_KeyCode)key);
    assert(name);
    /* Letters and F-keys upper-cased; digits as they are. */
    snprintf(out, (size_t)out_size, "%s", name);
    for( char* c = out; *c; c++ )
        *c = (char)toupper((unsigned char)*c);
}

void
Editor_ChordSpell(
    struct Editor_Chord chord,
    int apple,
    int glyphs,
    char* out,
    int out_size)
{
    char key[16];

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( chord.key == TORIRSK_UNKNOWN )
        return;
    hk_key_display(chord.key, glyphs, key, (int)sizeof(key));
    /* A modifier alone (Fly faster on Shift) is its key's name. */
    if( apple && chord.key == TORIRSK_CTRL )
        snprintf(key, sizeof(key), "%s", glyphs ? "\xe2\x8c\x98" : "Cmd");
    else if( glyphs && apple && chord.key == TORIRSK_SHIFT )
        snprintf(key, sizeof(key), "%s", "\xe2\x87\xa7");
    else if( glyphs && apple && chord.key == TORIRSK_ALT )
        snprintf(key, sizeof(key), "%s", "\xe2\x8c\xa5");
    if( apple && glyphs )
    {
        /* The Mac's order, run together: Option, Shift, Command. */
        snprintf(out, (size_t)out_size, "%s%s%s%s", (chord.mods & EDITOR_HK_MOD_ALT) ? "\xe2\x8c\xa5" : "",
            (chord.mods & EDITOR_HK_MOD_SHIFT) ? "\xe2\x87\xa7" : "",
            (chord.mods & EDITOR_HK_MOD_CTRL) ? "\xe2\x8c\x98" : "", key);
        return;
    }
    snprintf(out, (size_t)out_size, "%s%s%s%s",
        (chord.mods & EDITOR_HK_MOD_CTRL) ? (apple ? "Cmd+" : "Ctrl+") : "",
        (chord.mods & EDITOR_HK_MOD_SHIFT) ? "Shift+" : "", (chord.mods & EDITOR_HK_MOD_ALT) ? "Alt+" : "", key);
}

void
Editor_ChordDisplay(
    struct Editor_Chord chord,
    char* out,
    int out_size)
{
    Editor_ChordSpell(chord, EDITOR_HK_APPLE, EDITOR_HK_SMALL_FACE_GLYPHS, out, out_size);
}

/* The chords the 6.8 moves retired, kept as the second default for one
 * release (the doc's Level table says so). */
static const struct
{
    enum Editor_HotkeyAction action;
    const char* chord;
} k_retired[] = {
    { EDITOR_HK_LEVEL_PIN0, "ctrl+0" },
    { EDITOR_HK_LEVEL_ABOVE, "ctrl+5" },
};

int
Editor_HotkeyRetiredNote(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action,
    struct Editor_Chord chord,
    char* out,
    int out_size)
{
    assert(hk);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( size_t i = 0; i < sizeof(k_retired) / sizeof(k_retired[0]); i++ )
    {
        struct Editor_Chord old;
        char old_text[32];
        char now_text[32];
        int const ok = Editor_ChordParse(k_retired[i].chord, &old);
        assert(ok);
        (void)ok;
        if( k_retired[i].action != action || old.key != chord.key || old.mods != chord.mods )
            continue;
        Editor_ChordDisplay(old, old_text, (int)sizeof(old_text));
        Editor_ChordDisplay(hk->bind[action].chord[0], now_text, (int)sizeof(now_text));
        /* Rebound onto the old chord itself: it is the user's now. */
        if( hk->bind[action].chord[0].key == old.key && hk->bind[action].chord[0].mods == old.mods )
            return 0;
        snprintf(out, (size_t)out_size, "editor: %s is the old chord of %s: %s from now on; the old one goes next release",
            old_text, k_actions[action].name, now_text[0] ? now_text : "unbound");
        return 1;
    }
    return 0;
}

void
Editor_HotkeyDisplay(
    const struct Editor_Hotkeys* hk,
    enum Editor_HotkeyAction action,
    char* out,
    int out_size)
{
    int used = 0;

    assert(hk);
    assert((int)action >= 0);
    assert(action < EDITOR_HK_COUNT);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
    {
        char one[32];
        if( hk->bind[action].chord[c].key == TORIRSK_UNKNOWN )
            continue;
        Editor_ChordDisplay(hk->bind[action].chord[c], one, (int)sizeof(one));
        used += snprintf(out + used, (size_t)(out_size - used), "%s%s", used ? ", " : "", one);
        if( used >= out_size )
            return;
    }
}

/* ---- the hint strip ------------------------------------------------------------ */

/** A tool by its arming action: enum Editor_Tool's value (editor_ui.c asserts
 *  the two orders agree). */
#define HK_TOOL(action) ((action) - EDITOR_HK_TOOL_SELECT)

/** One hint row: the mouse word, the actions whose chords follow it, the
 *  verb. A NULL verb ends a tool's list. */
struct hk_hint_row
{
    const char* mouse;
    int actions[EDITOR_HK_HINT_ACTIONS];
    int action_count;
    const char* verb;
};

#define HK_NUDGES                                                                                  \
    {                                                                                              \
        EDITOR_HK_EDIT_NUDGE_N, EDITOR_HK_EDIT_NUDGE_S, EDITOR_HK_EDIT_NUDGE_W, EDITOR_HK_EDIT_NUDGE_E \
    }

static const struct hk_hint_row k_hint_select[] = {
    { "click", { 0 }, 0, "tile" },
    { "Shift+click", { 0 }, 0, "add loc" },
    { "drag", { 0 }, 0, "marquee" },
    { NULL, { EDITOR_HK_EDIT_SELECT_SQUARE }, 1, "square" },
    { NULL, { EDITOR_HK_EDIT_SELECT_SAME }, 1, "same" },
    { NULL, HK_NUDGES, 4, "nudge" },
    { NULL, { EDITOR_HK_EDIT_ROTATE }, 1, "rotate" },
    { NULL, { EDITOR_HK_EDIT_RESHAPE }, 1, "reshape" },
    { NULL, { EDITOR_HK_PLACE_SWAP }, 1, "swap" },
    { NULL, { EDITOR_HK_EDIT_DELETE }, 1, "delete" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_place[] = {
    { "click", { EDITOR_HK_PLACE_HERE }, 1, "place" },
    { "drag", { 0 }, 0, "wall run" },
    { NULL, { EDITOR_HK_PLACE_CONSTRAIN }, 1, "one axis" },
    { NULL, { EDITOR_HK_EDIT_ROTATE }, 1, "turn" },
    { NULL, { EDITOR_HK_EDIT_RESHAPE }, 1, "reshape" },
    { NULL, { EDITOR_HK_PLACE_FIT }, 1, "fit" },
    { NULL, { EDITOR_HK_PLACE_SWAP }, 1, "swap" },
    { NULL, { EDITOR_HK_EDIT_DESELECT }, 1, "drop" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_brush[] = {
    { "drag", { 0 }, 0, "paint" },
    { NULL, { EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP }, 2, "radius" },
    { NULL, { EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP }, 2, "strength" },
    { NULL, { EDITOR_HK_BRUSH_STEP_DOWN, EDITOR_HK_BRUSH_STEP_UP }, 2, "step" },
    { NULL, { 0 }, 0, NULL },
};

/* The Height brush's wheel: Ctrl is Command on a Mac (platform_sdl2.c maps
 * both to TORIRSK_CTRL), and the strip says the key the hand presses. */
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#define HK_HEIGHT_WHEEL "cmd+wheel"
#define HK_HEIGHT_WHEEL_GRID "cmd+shift+wheel"
#define HK_HEIGHT_PICK "cmd+click"
#else
#define HK_HEIGHT_WHEEL "ctrl+wheel"
#define HK_HEIGHT_WHEEL_GRID "ctrl+shift+wheel"
#define HK_HEIGHT_PICK "ctrl+click"
#endif

static const struct hk_hint_row k_hint_height[] = {
    { "drag", { 0 }, 0, "pull" },
    { HK_HEIGHT_WHEEL, { 0 }, 0, "height" },
    { HK_HEIGHT_WHEEL_GRID, { 0 }, 0, "grid only" },
    { HK_HEIGHT_PICK, { 0 }, 0, "grid here" },
    { NULL, { EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP }, 2, "radius" },
    { NULL, { EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP }, 2, "strength" },
    { NULL, { EDITOR_HK_BRUSH_STEP_DOWN, EDITOR_HK_BRUSH_STEP_UP }, 2, "step" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_move[] = {
    { "drag", HK_NUDGES, 4, "move" },
    { NULL, { EDITOR_HK_EDIT_DUPLICATE }, 1, "dup" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_rotate[] = {
    { "click", { 0 }, 0, "rotate" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_delete[] = {
    { "click", { 0 }, 0, "delete" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_rect[] = {
    { "drag", { 0 }, 0, "rectangle" },
    { NULL, { EDITOR_HK_EDIT_COPY }, 1, "copy" },
    { NULL, { EDITOR_HK_EDIT_PASTE }, 1, "paste" },
    { NULL, { EDITOR_HK_EDIT_FILL }, 1, "fill" },
    { NULL, { EDITOR_HK_EDIT_CLEAR }, 1, "clear" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_player[] = {
    { "click", { 0 }, 0, "put player" },
    { NULL, { 0 }, 0, NULL },
};

static const struct hk_hint_row k_hint_path[] = {
    { "drag", { 0 }, 0, "lay path" },
    { HK_HEIGHT_PICK, { 0 }, 0, "set height" },
    { NULL, { EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP }, 2, "width" },
    { NULL, { 0 }, 0, NULL },
};
static const struct hk_hint_row k_hint_scatter[] = {
    { "drag", { 0 }, 0, "scatter" },
    { "alt+drag", { 0 }, 0, "erase" },
    { NULL, { EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP }, 2, "radius" },
    { NULL, { EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP }, 2, "density" },
    { NULL, { 0 }, 0, NULL },
};
static const struct hk_hint_row k_hint_plateau[] = {
    { "drag", { 0 }, 0, "flatten" },
    { NULL, { EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP }, 2, "radius" },
    { NULL, { EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP }, 2, "skirt" },
    { NULL, { 0 }, 0, NULL },
};
static const struct hk_hint_row k_hint_region[] = {
    { "drag", { 0 }, 0, "paint" },
    { "alt+drag", { 0 }, 0, "take off" },
    { NULL, { EDITOR_HK_BRUSH_RADIUS_DOWN, EDITOR_HK_BRUSH_RADIUS_UP }, 2, "radius" },
    { NULL, { EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP }, 2, "hardness" },
    { NULL, { 0 }, 0, NULL },
};
static const struct hk_hint_row k_hint_lasso[] = {
    { "drag", { 0 }, 0, "draw loop" },
    { "click", { 0 }, 0, "corner" },
    { "alt+drag", { 0 }, 0, "take off" },
    { NULL, { EDITOR_HK_EDIT_DESELECT }, 1, "drop" },
    { NULL, { 0 }, 0, NULL },
};
static const struct hk_hint_row k_hint_tidy[] = {
    { "click", { 0 }, 0, "tidy run" },
    { NULL, { EDITOR_HK_BRUSH_STRENGTH_DOWN, EDITOR_HK_BRUSH_STRENGTH_UP }, 2, "hardness" },
    { NULL, { 0 }, 0, NULL },
};

#undef HK_NUDGES

static const struct hk_hint_row* const k_hint_rows[EDITOR_HK_HINT_TOOLS] = {
    [HK_TOOL(EDITOR_HK_TOOL_SELECT)] = k_hint_select,
    [HK_TOOL(EDITOR_HK_TOOL_RECT)] = k_hint_rect,
    [HK_TOOL(EDITOR_HK_TOOL_HEIGHT)] = k_hint_height,
    [HK_TOOL(EDITOR_HK_TOOL_FLATTEN)] = k_hint_brush,
    [HK_TOOL(EDITOR_HK_TOOL_SMOOTH)] = k_hint_brush,
    [HK_TOOL(EDITOR_HK_TOOL_PAINT_UNDERLAY)] = k_hint_brush,
    [HK_TOOL(EDITOR_HK_TOOL_PAINT_OVERLAY)] = k_hint_brush,
    [HK_TOOL(EDITOR_HK_TOOL_PAINT_FLAGS)] = k_hint_brush,
    [HK_TOOL(EDITOR_HK_TOOL_PLACE)] = k_hint_place,
    [HK_TOOL(EDITOR_HK_TOOL_MOVE)] = k_hint_move,
    [HK_TOOL(EDITOR_HK_TOOL_ROTATE)] = k_hint_rotate,
    [HK_TOOL(EDITOR_HK_TOOL_DELETE)] = k_hint_delete,
    [HK_TOOL(EDITOR_HK_TOOL_PLACE_PLAYER)] = k_hint_player,
    [HK_TOOL(EDITOR_HK_TOOL_PAINT_PATH)] = k_hint_path,
    [HK_TOOL(EDITOR_HK_TOOL_NOISE)] = k_hint_brush,
    [HK_TOOL(EDITOR_HK_TOOL_SCATTER)] = k_hint_scatter,
    [HK_TOOL(EDITOR_HK_TOOL_PLATEAU)] = k_hint_plateau,
    [HK_TOOL(EDITOR_HK_TOOL_REGION)] = k_hint_region,
    [HK_TOOL(EDITOR_HK_TOOL_LASSO)] = k_hint_lasso,
    [HK_TOOL(EDITOR_HK_TOOL_TIDY)] = k_hint_tidy,
};

/**
 * The chords of a row of several actions, compacted: each action's first
 * chord, the modifier prefix they all share said once ("Shift+[ ]" for
 * Shift+[ and Shift+]) when they all hold the same modifiers (else each said
 * whole: "Ctrl+Shift+[ Ctrl+]"), and the four arrow keys as "arrows". "" when
 * none of them is bound.
 */
static void
hk_hint_join(
    const struct Editor_Hotkeys* hk,
    const int* actions,
    int count,
    char* out,
    int out_size)
{
    char shown[EDITOR_HK_HINT_ACTIONS][32];
    uint8_t mods[EDITOR_HK_HINT_ACTIONS];
    int bound = 0;
    int prefix = 0;
    int same_mods = 1;
    int used;

    assert(count <= EDITOR_HK_HINT_ACTIONS);
    out[0] = '\0';
    for( int i = 0; i < count; i++ )
    {
        if( hk->bind[actions[i]].chord[0].key == TORIRSK_UNKNOWN )
            continue;
        Editor_ChordDisplay(hk->bind[actions[i]].chord[0], shown[bound], (int)sizeof(shown[bound]));
        mods[bound] = hk->bind[actions[i]].chord[0].mods;
        bound++;
    }
    if( bound == 0 )
        return;
    /* Compact only chords that hold the same modifiers: with Ctrl+Shift+[
     * beside Ctrl+], a shared "Ctrl+" said once would read "Ctrl+Shift+[ ]",
     * which says Ctrl+Shift+] -- a chord nothing is bound to. Each is then
     * said whole. */
    for( int i = 1; i < bound; i++ )
        same_mods &= mods[i] == mods[0];
    /* The longest prefix every chord shares that ends just past a '+': the
     * modifiers, never part of a key. */
    for( int at = 0; same_mods && shown[0][at]; at++ )
    {
        int same = 1;
        for( int i = 1; i < bound && same; i++ )
            same = shown[i][at] == shown[0][at];
        if( !same )
            break;
        if( shown[0][at] == '+' && shown[0][at + 1] )
            prefix = at + 1;
    }
    used = snprintf(out, (size_t)out_size, "%.*s", prefix, shown[0]);
    if( bound == 4 && strcmp(shown[0] + prefix, "Up") == 0 && strcmp(shown[1] + prefix, "Down") == 0
        && strcmp(shown[2] + prefix, "Left") == 0 && strcmp(shown[3] + prefix, "Right") == 0 )
    {
        snprintf(out + used, (size_t)(out_size - used), "arrows");
        return;
    }
    for( int i = 0; i < bound && used < out_size; i++ )
        used += snprintf(out + used, (size_t)(out_size - used), "%s%s", i ? " " : "", shown[i] + prefix);
}

void
Editor_HotkeysJoin(
    const struct Editor_Hotkeys* hk,
    const int* actions,
    int count,
    char* out,
    int out_size)
{
    assert(hk);
    assert(actions);
    assert(count > 0);
    assert(count <= EDITOR_HK_HINT_ACTIONS);
    assert(out);
    assert(out_size > 0);
    for( int i = 0; i < count; i++ )
    {
        assert(actions[i] >= 0);
        assert(actions[i] < EDITOR_HK_COUNT);
    }
    hk_hint_join(hk, actions, count, out, out_size);
}

/** A row's mouse word as a finger says it ("click" is a tap; a drag is a
 *  drag), NULL when a finger has none (Shift+click). */
static const char*
hk_touch_word(const char* mouse)
{
    if( !mouse )
        return NULL;
    if( strcmp(mouse, "click") == 0 )
        return "tap";
    if( strcmp(mouse, "drag") == 0 )
        return "drag";
    return NULL;
}

/** The camera's rows on a touch strip, each read off its table action's
 *  gesture spelling, then the long press (the minimenu). */
static const struct
{
    int action;
    const char* verb;
} k_touch_camera[] = {
    { EDITOR_HK_CAM_FORWARD, "pan" },
    { EDITOR_HK_CAM_UP, "zoom" },
    { EDITOR_HK_CAM_TURN_LEFT, "turn" },
};

static int
hk_hint_touch(
    int tool,
    struct Editor_HotkeyHint* out_rows,
    int max)
{
    const struct hk_hint_row* row;
    int count = 0;

    for( row = k_hint_rows[tool]; row->verb && count < max; row++ )
    {
        const char* word = hk_touch_word(row->mouse);
        if( !word )
            continue;
        snprintf(out_rows[count].chord, sizeof(out_rows[count].chord), "%s", word);
        out_rows[count].verb = row->verb;
        out_rows[count].action = row->action_count > 0 ? row->actions[0] : -1;
        count++;
    }
    for( size_t i = 0; i < sizeof(k_touch_camera) / sizeof(k_touch_camera[0]) && count < max; i++ )
    {
        const char* gesture = k_actions[k_touch_camera[i].action].gesture;
        if( !gesture )
            continue;
        snprintf(out_rows[count].chord, sizeof(out_rows[count].chord), "%s", gesture);
        out_rows[count].verb = k_touch_camera[i].verb;
        out_rows[count].action = k_touch_camera[i].action;
        count++;
    }
    if( count < max )
    {
        /* The long press: the minimenu, as a right click (input/torirs_touch.h). */
        snprintf(out_rows[count].chord, sizeof(out_rows[count].chord), "%s", "hold");
        out_rows[count].verb = "menu";
        out_rows[count].action = -1;
        count++;
    }
    return count;
}

int
Editor_HotkeysHint(
    const struct Editor_Hotkeys* hk,
    unsigned contexts,
    int tool,
    int touch,
    struct Editor_HotkeyHint* out_rows,
    int max)
{
    const struct hk_hint_row* row;
    int count = 0;

    assert(hk);
    assert(tool >= 0);
    assert(tool < EDITOR_HK_HINT_TOOLS);
    assert(out_rows);
    assert(max > 0);
    if( touch )
        return hk_hint_touch(tool, out_rows, max);
    contexts |= EDITOR_HK_CTX_G;
    for( row = k_hint_rows[tool]; row->verb && count < max; row++ )
    {
        struct Editor_HotkeyHint* out = &out_rows[count];
        char keys[EDITOR_HK_HINT_CHORD_MAX];
        int live = 0;

        keys[0] = '\0';
        for( int i = 0; i < row->action_count; i++ )
            if( k_actions[row->actions[i]].contexts & contexts )
                live = 1;
        if( live && row->action_count == 1 )
            Editor_HotkeyDisplay(hk, (enum Editor_HotkeyAction)row->actions[0], keys, (int)sizeof(keys));
        else if( live )
            hk_hint_join(hk, row->actions, row->action_count, keys, (int)sizeof(keys));
        if( !row->mouse && !keys[0] )
            continue;
        snprintf(out->chord, sizeof(out->chord), "%s%s%s", row->mouse ? row->mouse : "",
            row->mouse && keys[0] ? "/" : "", keys);
        out->verb = row->verb;
        out->action = row->action_count > 0 ? row->actions[0] : -1;
        count++;
    }
    return count;
}

unsigned
Editor_HotkeyContextsFrom(const struct Editor_HotkeyContextFacts* facts)
{
    unsigned contexts = EDITOR_HK_CTX_G;

    assert(facts);
    assert(facts->tool >= 0);
    assert(facts->tool < EDITOR_HK_HINT_TOOLS);
    if( facts->pointer_in_world )
        contexts |= EDITOR_HK_CTX_W;
    if( facts->loc_count > 0 )
    {
        contexts |= EDITOR_HK_CTX_L;
        /* Only Select and Move nudge (Ctrl+arrows); the bare arrows are the
         * camera's under every tool. */
        if( facts->tool == HK_TOOL(EDITOR_HK_TOOL_SELECT) || facts->tool == HK_TOOL(EDITOR_HK_TOOL_MOVE) )
            contexts |= EDITOR_HK_CTX_M;
        else
            contexts |= EDITOR_HK_CTX_N;
    }
    else
    {
        contexts |= EDITOR_HK_CTX_N;
        if( facts->spawn_count > 0 )
            contexts |= EDITOR_HK_CTX_S;
        if( facts->tool == HK_TOOL(EDITOR_HK_TOOL_PLACE) && facts->loc_pick )
            contexts |= EDITOR_HK_CTX_P;
    }
    if( facts->rect_valid )
        contexts |= EDITOR_HK_CTX_R;
    if( facts->graph_focus )
        contexts |= EDITOR_HK_CTX_Q;
    contexts |= facts->camera_unity ? EDITOR_HK_CTX_U : EDITOR_HK_CTX_D;
    return contexts;
}

#undef HK_TOOL

/* ---- editor_hotkeys.ini ------------------------------------------------------------ */

const char*
Editor_HotkeysPath(void)
{
    const char* configured = getenv("TORIRS_EDITOR_HOTKEYS");

    if( !configured )
        return EDITOR_HOTKEYS_DEFAULT_PATH;
    return *configured ? configured : NULL;
}

static void
hk_report(
    struct Editor_HotkeysReport* report,
    int line,
    const char* why,
    const char* text,
    int text_len)
{
    if( !report )
        return;
    report->ignored++;
    if( report->line_count >= EDITOR_HOTKEYS_REPORT_MAX )
        return;
    if( text_len > 48 )
        text_len = 48;
    snprintf(report->lines[report->line_count++], EDITOR_HOTKEYS_REPORT_LINE, "line %d: %s: %.*s", line, why,
        text_len, text);
}

int
Editor_HotkeysDecode(
    struct Editor_Hotkeys* hk,
    const void* data,
    int size,
    struct Editor_HotkeysReport* report)
{
    const char* text = data;
    int pos = 0;
    int line_no = 0;
    int applied = 0;
    int in_hotkeys = 1;

    assert(hk);
    assert(data || size == 0);
    assert(size >= 0);
    if( report )
        memset(report, 0, sizeof(*report));
    hk_set_defaults(hk);
    while( pos < size )
    {
        int start = pos;
        int end;
        int eq;
        char key[64];
        char value[128];
        int key_len;
        int value_len;
        struct Editor_Chord chords[EDITOR_HK_CHORDS];
        int chord_count = 0;
        int bad = 0;
        int action;

        while( pos < size && text[pos] != '\n' )
            pos++;
        end = pos;
        pos++;
        line_no++;
        while( start < end && (text[start] == ' ' || text[start] == '\t') )
            start++;
        while( end > start && (text[end - 1] == '\r' || text[end - 1] == ' ' || text[end - 1] == '\t') )
            end--;
        if( start >= end || text[start] == ';' || text[start] == '#' )
            continue;
        if( text[start] == '[' )
        {
            in_hotkeys = end - start == 9 && memcmp(text + start, "[hotkeys]", 9) == 0;
            if( !in_hotkeys )
                hk_report(report, line_no, "not the [hotkeys] section", text + start, end - start);
            continue;
        }
        if( !in_hotkeys )
        {
            hk_report(report, line_no, "outside [hotkeys]", text + start, end - start);
            continue;
        }
        for( eq = start; eq < end && text[eq] != '='; eq++ )
            ;
        if( eq >= end )
        {
            hk_report(report, line_no, "no '='", text + start, end - start);
            continue;
        }
        key_len = eq - start;
        while( key_len > 0 && (text[start + key_len - 1] == ' ' || text[start + key_len - 1] == '\t') )
            key_len--;
        value_len = end - (eq + 1);
        if( key_len <= 0 || key_len >= (int)sizeof(key) || value_len >= (int)sizeof(value) )
        {
            hk_report(report, line_no, "too long", text + start, end - start);
            continue;
        }
        memcpy(key, text + start, (size_t)key_len);
        key[key_len] = '\0';
        memcpy(value, text + eq + 1, (size_t)value_len);
        value[value_len] = '\0';
        if( strcmp(key, "version") == 0 )
            continue;
        action = Editor_HotkeyFind(key);
        if( action < 0 )
        {
            hk_report(report, line_no, "unknown action", text + start, end - start);
            continue;
        }
        /* The value: empty = unbound, else chords split on ','. Strip an
         * inline comment first. */
        for( char* c = value; *c; c++ )
            if( *c == ';' || *c == '#' )
            {
                *c = '\0';
                break;
            }
        {
            char* p = value;
            while( *p == ' ' || *p == '\t' )
                p++;
            if( *p )
                for( ;; )
                {
                    char* comma = strchr(p, ',');
                    if( comma )
                        *comma = '\0';
                    if( chord_count >= EDITOR_HK_CHORDS || !Editor_ChordParse(p, &chords[chord_count]) )
                    {
                        bad = 1;
                        break;
                    }
                    chord_count++;
                    if( !comma )
                        break;
                    p = comma + 1;
                }
        }
        if( bad )
        {
            hk_report(report, line_no, "bad chord", text + start, end - start);
            continue;
        }
        memset(&hk->bind[action].chord, 0, sizeof(hk->bind[action].chord));
        for( int c = 0; c < chord_count; c++ )
            hk->bind[action].chord[c] = chords[c];
        hk->bind[action].stamp = hk->stamp_next++;
        applied++;
    }
    memset(hk->repeat_held, 0, sizeof(hk->repeat_held));
    hk->serial++;
    if( report )
        report->applied = applied;
    return applied;
}

void
Editor_HotkeysEncode(
    const struct Editor_Hotkeys* hk,
    char** out_data,
    int* out_size)
{
    int const cap = 512 + EDITOR_HK_COUNT * 96;
    char* buffer;
    int used;

    assert(hk);
    assert(out_data);
    assert(out_size);
    buffer = ToriMem_Alloc(ToriMem_Process(), (size_t)cap);
    assert(buffer);
    used = snprintf(buffer, (size_t)cap,
        "; ToriRSMapEd hotkeys (docs/MAPED_HOTKEYS.md lists every action id).\n"
        "; Only bindings that differ from the defaults; `<action> =` is unbound.\n"
        "[hotkeys]\nversion = %d\n",
        EDITOR_HOTKEYS_VERSION);
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        int first = 1;
        if( Editor_HotkeysIsDefault(hk, a) )
            continue;
        used += snprintf(buffer + used, (size_t)(cap - used), "%s =", k_actions[a].id);
        for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
        {
            char one[48];
            if( hk->bind[a].chord[c].key == TORIRSK_UNKNOWN )
                continue;
            Editor_ChordFormat(hk->bind[a].chord[c], one, (int)sizeof(one));
            used += snprintf(buffer + used, (size_t)(cap - used), "%s %s", first ? "" : ",", one);
            first = 0;
        }
        used += snprintf(buffer + used, (size_t)(cap - used), "\n");
        assert(used < cap);
    }
    *out_data = buffer;
    *out_size = used;
}

/* ---- one frame ---------------------------------------------------------------- */

unsigned
Editor_HotkeyHeldMods(const struct Editor_HotkeyInput* in)
{
    unsigned mods = 0;

    assert(in);
    if( in->held[TORIRSK_CTRL] )
        mods |= EDITOR_HK_MOD_CTRL;
    if( in->held[TORIRSK_SHIFT] )
        mods |= EDITOR_HK_MOD_SHIFT;
    if( in->held[TORIRSK_ALT] )
        mods |= EDITOR_HK_MOD_ALT;
    return mods;
}

/* The modifier a held action may carry on top of its own: the fly-faster
 * chord's key, when that is a modifier (Shift by default). */
static unsigned
hk_speed_mod(const struct Editor_Hotkeys* hk)
{
    return hk_mod_of_key(hk->bind[EDITOR_HK_CAM_FAST].chord[0].key);
}

/* Does `chord` match exactly the modifiers held (its own key's bit aside)? */
static int
hk_mods_exact(
    struct Editor_Chord chord,
    unsigned held_mods)
{
    return (held_mods & ~hk_mod_of_key(chord.key)) == chord.mods;
}

/* A held action: its key and modifiers held; only the speed modifier may be
 * held on top. The fly-faster action itself takes any modifiers. */
static int
hk_held_match(
    const struct Editor_Hotkeys* hk,
    int action,
    struct Editor_Chord chord,
    const struct Editor_HotkeyInput* in,
    unsigned held_mods)
{
    unsigned extra;

    if( chord.key == TORIRSK_UNKNOWN || !in->held[chord.key] )
        return 0;
    if( action == EDITOR_HK_CAM_FAST )
        return 1;
    held_mods &= ~hk_mod_of_key(chord.key);
    if( (held_mods & chord.mods) != chord.mods )
        return 0;
    extra = held_mods & ~(unsigned)chord.mods;
    return (extra & ~hk_speed_mod(hk)) == 0;
}

static int
hk_live(
    int action,
    unsigned contexts)
{
    return (k_actions[action].contexts & contexts) != 0;
}

/* Of two actions on one chord, the later binding wins; a tie goes to the
 * later row of the table. */
static int
hk_beats(
    const struct Editor_Hotkeys* hk,
    int a,
    int b)
{
    if( hk->bind[a].stamp != hk->bind[b].stamp )
        return hk->bind[a].stamp > hk->bind[b].stamp;
    return a > b;
}

void
Editor_HotkeysResolve(
    struct Editor_Hotkeys* hk,
    const struct Editor_HotkeyInput* in,
    struct Editor_HotkeyFrame* out)
{
    unsigned contexts = in ? (in->contexts | EDITOR_HK_CTX_G) : 0;
    unsigned held_mods;
    int candidate[EDITOR_HK_COUNT];
    struct Editor_Chord candidate_chord[EDITOR_HK_COUNT];
    int candidate_count = 0;

    assert(hk);
    assert(in);
    assert(out);
    /* A caller that states no camera scheme (the tests, an older App) is on
     * the default one: exactly one of D / U is live every frame. */
    if( !(contexts & (EDITOR_HK_CTX_D | EDITOR_HK_CTX_U)) )
        contexts |= EDITOR_HK_CTX_D;
    memset(out, 0, sizeof(*out));
    held_mods = Editor_HotkeyHeldMods(in);

    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        enum Editor_HotkeyKind const kind = k_actions[a].kind;
        int const live = hk_live(a, contexts);

        if( kind == EDITOR_HK_HELD )
        {
            for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
                if( live && hk_held_match(hk, a, hk->bind[a].chord[c], in, held_mods) )
                {
                    out->active[a] = 1;
                    out->claimed[hk->bind[a].chord[c].key] = 1;
                    out->consumed[hk->bind[a].chord[c].key] = 1;
                    break;
                }
            continue;
        }

        {
            int edge = -1;
            int still = -1;
            for( int c = 0; c < EDITOR_HK_CHORDS; c++ )
            {
                struct Editor_Chord const chord = hk->bind[a].chord[c];
                if( chord.key == TORIRSK_UNKNOWN || !hk_mods_exact(chord, held_mods) )
                    continue;
                if( live && (in->held[chord.key] || in->down[chord.key]) )
                    out->claimed[chord.key] = 1;
                if( in->down[chord.key] && edge < 0 )
                    edge = c;
                if( in->held[chord.key] && still < 0 )
                    still = c;
            }
            if( !live )
            {
                hk->repeat_held[a] = 0;
                continue;
            }
            if( edge >= 0 )
            {
                candidate[candidate_count] = a;
                candidate_chord[candidate_count] = hk->bind[a].chord[edge];
                candidate_count++;
                if( kind == EDITOR_HK_REPEAT )
                {
                    hk->repeat_held[a] = 1;
                    hk->repeat_next_ms[a] = in->now_ms + EDITOR_HK_REPEAT_DELAY_MS;
                }
                continue;
            }
            if( kind != EDITOR_HK_REPEAT )
                continue;
            if( still < 0 || !hk->repeat_held[a] )
            {
                hk->repeat_held[a] = 0;
                continue;
            }
            if( in->now_ms >= hk->repeat_next_ms[a] )
            {
                candidate[candidate_count] = a;
                candidate_chord[candidate_count] = hk->bind[a].chord[still];
                candidate_count++;
                hk->repeat_next_ms[a] += EDITOR_HK_REPEAT_PERIOD_MS;
                /* A long frame does not bank a burst of repeats. */
                if( hk->repeat_next_ms[a] <= in->now_ms )
                    hk->repeat_next_ms[a] = in->now_ms + EDITOR_HK_REPEAT_PERIOD_MS;
            }
        }
    }

    /* One action per chord: a clash fires the later binding only. */
    for( int i = 0; i < candidate_count; i++ )
    {
        int beaten = 0;
        for( int j = 0; j < candidate_count && !beaten; j++ )
            if( j != i && candidate_chord[j].key == candidate_chord[i].key
                && candidate_chord[j].mods == candidate_chord[i].mods && hk_beats(hk, candidate[j], candidate[i]) )
                beaten = 1;
        if( beaten || out->count >= EDITOR_HK_FIRED_MAX )
            continue;
        out->fired[out->count] = candidate[i];
        out->fired_chord[out->count] = candidate_chord[i];
        out->count++;
        out->consumed[candidate_chord[i].key] = 1;
    }
}

int
Editor_HotkeysCapture(
    const struct Editor_HotkeyInput* in,
    int allow_modifier,
    struct Editor_Chord* out)
{
    unsigned const held_mods = in ? Editor_HotkeyHeldMods(in) : 0;
    int modifier_key = TORIRSK_UNKNOWN;

    assert(in);
    assert(out);
    for( int k = TORIRSK_UNKNOWN + 1; k < TORIRSK_COUNT; k++ )
    {
        if( !in->down[k] )
            continue;
        if( Editor_HotkeyKeyIsModifier(k) )
        {
            if( modifier_key == TORIRSK_UNKNOWN )
                modifier_key = k;
            continue;
        }
        out->key = (uint8_t)k;
        out->mods = (uint8_t)held_mods;
        return 1;
    }
    if( allow_modifier && modifier_key != TORIRSK_UNKNOWN )
    {
        out->key = (uint8_t)modifier_key;
        out->mods = (uint8_t)(held_mods & ~hk_mod_of_key(modifier_key));
        return 1;
    }
    return 0;
}
