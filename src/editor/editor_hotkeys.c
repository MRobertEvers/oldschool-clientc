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
    [EDITOR_HK_EDIT_NUDGE_N] = { "edit.nudge_n", "Edit", "Nudge north", "Nudge selection N / S / W / E", L, REPEAT,
                                 { "up" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_S] = { "edit.nudge_s", "Edit", "Nudge south", "Nudge selection N / S / W / E", L, REPEAT,
                                 { "down" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_W] = { "edit.nudge_w", "Edit", "Nudge west", "Nudge selection N / S / W / E", L, REPEAT,
                                 { "left" }, NULL },
    [EDITOR_HK_EDIT_NUDGE_E] = { "edit.nudge_e", "Edit", "Nudge east", "Nudge selection N / S / W / E", L, REPEAT,
                                 { "right" }, NULL },
    [EDITOR_HK_EDIT_COPY] = { "edit.copy", "Edit", "Copy tiles", "Copy tiles", R, EDGE, { "ctrl+c" }, NULL },
    [EDITOR_HK_EDIT_PASTE] = { "edit.paste", "Edit", "Paste tiles", "Paste tiles at the hovered tile", W, EDGE,
                               { "ctrl+v" }, NULL },
    [EDITOR_HK_EDIT_FILL] = { "edit.fill", "Edit", "Fill tiles", "Fill tiles", R, EDGE, { "ctrl+shift+f" }, NULL },
    [EDITOR_HK_EDIT_CLEAR] = { "edit.clear", "Edit", "Clear tiles", "Clear tiles", R, EDGE, { "shift+delete" },
                               NULL },
    [EDITOR_HK_EDIT_MARK_AREA] = { "edit.mark_area", "Edit", "Mark area from rect", "Mark area from rect", R, EDGE,
                                   { "ctrl+shift+m" }, NULL },

    [EDITOR_HK_TOOL_SELECT] = { "tool.select", "Tools", "Select", "| Select |", G, EDGE, { "1" }, NULL },
    [EDITOR_HK_TOOL_RECT] = { "tool.rect", "Tools", "Rectangle", "| Rectangle |", G, EDGE, { "2" }, NULL },
    [EDITOR_HK_TOOL_RAISE] = { "tool.raise", "Tools", "Raise", "| Raise |", G, EDGE, { "3" }, NULL },
    [EDITOR_HK_TOOL_LOWER] = { "tool.lower", "Tools", "Lower", "| Lower |", G, EDGE, { "4" }, NULL },
    [EDITOR_HK_TOOL_FLATTEN] = { "tool.flatten", "Tools", "Flatten", "| Flatten |", G, EDGE, { "5" }, NULL },
    [EDITOR_HK_TOOL_SMOOTH] = { "tool.smooth", "Tools", "Smooth", "| Smooth |", G, EDGE, { "6" }, NULL },
    [EDITOR_HK_TOOL_PAINT_UNDERLAY] = { "tool.paint_underlay", "Tools", "Paint underlay", "Paint underlay", G, EDGE,
                                        { "7" }, NULL },
    [EDITOR_HK_TOOL_PAINT_OVERLAY] = { "tool.paint_overlay", "Tools", "Paint overlay", "Paint overlay", G, EDGE,
                                       { "8" }, NULL },
    [EDITOR_HK_TOOL_PAINT_FLAGS] = { "tool.paint_flags", "Tools", "Paint flags", "Paint flags", G, EDGE, { "9" },
                                     NULL },
    [EDITOR_HK_TOOL_PLACE] = { "tool.place", "Tools", "Place loc", "Place loc", G, EDGE, { "0" }, NULL },
    [EDITOR_HK_TOOL_MOVE] = { "tool.move", "Tools", "Move loc", "Move loc", G, EDGE, { "m" }, NULL },
    [EDITOR_HK_TOOL_ROTATE] = { "tool.rotate", "Tools", "Rotate loc (tool)", "Rotate loc (tool)", G, EDGE, { "t" },
                                NULL },
    [EDITOR_HK_TOOL_DELETE] = { "tool.delete", "Tools", "Delete loc (tool)", "Delete loc (tool)", G, EDGE, { "x" },
                                NULL },
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
    [EDITOR_HK_LEVEL_PIN0] = { "level.pin0", "Level", "Pin level 0", "Pin level 0..3", G, EDGE, { "ctrl+0" }, NULL },
    [EDITOR_HK_LEVEL_PIN1] = { "level.pin1", "Level", "Pin level 1", "Pin level 0..3", G, EDGE, { "ctrl+1" }, NULL },
    [EDITOR_HK_LEVEL_PIN2] = { "level.pin2", "Level", "Pin level 2", "Pin level 0..3", G, EDGE, { "ctrl+2" }, NULL },
    [EDITOR_HK_LEVEL_PIN3] = { "level.pin3", "Level", "Pin level 3", "Pin level 0..3", G, EDGE, { "ctrl+3" }, NULL },
    [EDITOR_HK_LEVEL_AUTO] = { "level.auto", "Level", "Level Auto", "Level Auto", G, EDGE, { "ctrl+4" }, NULL },
    [EDITOR_HK_LEVEL_ABOVE] = { "level.above", "Level", "Show above", "Show above on/off", G, EDGE, { "ctrl+5" },
                                NULL },

    [EDITOR_HK_CAM_FORWARD] = { "camera.forward", "Camera", "Fly forward", "Fly forward / back / left / right", G,
                                HELD, { "w" }, NULL },
    [EDITOR_HK_CAM_BACK] = { "camera.back", "Camera", "Fly back", "Fly forward / back / left / right", G, HELD,
                             { "s" }, NULL },
    [EDITOR_HK_CAM_LEFT] = { "camera.left", "Camera", "Fly left", "Fly forward / back / left / right", G, HELD,
                             { "a" }, NULL },
    [EDITOR_HK_CAM_RIGHT] = { "camera.right", "Camera", "Fly right", "Fly forward / back / left / right", G, HELD,
                              { "d" }, NULL },
    [EDITOR_HK_CAM_DOWN] = { "camera.down", "Camera", "Fly down", "Fly down / up", G, HELD, { "f" }, NULL },
    [EDITOR_HK_CAM_UP] = { "camera.up", "Camera", "Fly up", "Fly down / up", G, HELD, { "r" }, NULL },
    [EDITOR_HK_CAM_FAST] = { "camera.fast", "Camera", "Fly faster", "Fly faster", G, HELD, { "shift" }, NULL },
    [EDITOR_HK_CAM_TURN_LEFT] = { "camera.turn_left", "Camera", "Turn left", "Turn left / right, tilt up / down", N,
                                  HELD, { "left" }, NULL },
    [EDITOR_HK_CAM_TURN_RIGHT] = { "camera.turn_right", "Camera", "Turn right", "Turn left / right, tilt up / down",
                                   N, HELD, { "right" }, NULL },
    [EDITOR_HK_CAM_TILT_UP] = { "camera.tilt_up", "Camera", "Tilt up", "Turn left / right, tilt up / down", N, HELD,
                                { "up" }, NULL },
    [EDITOR_HK_CAM_TILT_DOWN] = { "camera.tilt_down", "Camera", "Tilt down", "Turn left / right, tilt up / down", N,
                                  HELD, { "down" }, NULL },
    [EDITOR_HK_CAM_FRAME] = { "camera.frame", "Camera", "Frame the selection",
                              "Frame the selection, else the hovered square", G, EDGE, { "home" }, NULL },
    [EDITOR_HK_CAM_FRAME_SQUARE] = { "camera.frame_square", "Camera", "Frame the square's centre",
                                     "Frame the current square's centre", G, EDGE, { "ctrl+home" }, NULL },

    [EDITOR_HK_VIEW_HOTKEYS] = { "view.hotkeys", "View", "Hotkeys panel", "Hotkeys panel", G, EDGE, { "f1" },
                                 "hotkeys" },
    [EDITOR_HK_VIEW_TERRAIN] = { "view.terrain", "View", "Terrain", "| Terrain |", G, EDGE, { "f2" }, "terrain" },
    [EDITOR_HK_VIEW_PALETTE] = { "view.palette", "View", "Palette", "| Palette |", G, EDGE, { "f3" }, "palette" },
    [EDITOR_HK_VIEW_SQUARES] = { "view.squares", "View", "Squares", "| Squares |", G, EDGE, { "f4" }, "squares" },
    [EDITOR_HK_VIEW_LOCS] = { "view.locs", "View", "Locs", "| Locs |", G, EDGE, { "f5" }, "locs" },
    [EDITOR_HK_VIEW_SPAWNS] = { "view.spawns", "View", "Spawns", "| Spawns |", G, EDGE, { "f6" }, "spawns" },
    [EDITOR_HK_VIEW_INSPECTOR] = { "view.inspector", "View", "Inspector", "| Inspector |", G, EDGE, { "f7" },
                                   "inspector" },
    [EDITOR_HK_VIEW_CATALOG] = { "view.catalog", "View", "Catalog", "| Catalog |", G, EDGE, { "f8" }, "catalog" },
    [EDITOR_HK_VIEW_PREVIEW] = { "view.preview", "View", "Preview", "| Preview (the Catalog pick's model) |", G,
                                 EDGE, { "ctrl+shift+p" }, "preview" },
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
    [EDITOR_HK_VIEW_NPCBUILDER] = { "view.npcbuilder", "View", "NPC builder",
                                    "Every other panel the View menu lists", G, EDGE, { NULL }, "npcbuilder" },
    [EDITOR_HK_VIEW_SHELL] = { "view.shelleditor", "View", "Shell editor", "Every other panel the View menu lists",
                               G, EDGE, { NULL }, "shelleditor" },
    [EDITOR_HK_VIEW_IFACE] = { "view.iface", "View", "Interface editor", "Every other panel the View menu lists", G,
                               EDGE, { NULL }, "iface" },
    [EDITOR_HK_VIEW_CS2] = { "view.cs2", "View", "CS2", "Every other panel the View menu lists", G, EDGE, { NULL },
                             "cs2" },
    [EDITOR_HK_VIEW_LIVE] = { "view.live", "View", "Live", "Every other panel the View menu lists", G, EDGE,
                              { NULL }, "live" },
    [EDITOR_HK_VIEW_TRIGGERS] = { "view.triggers", "View", "Triggers", "| Triggers |", G, EDGE, { "ctrl+e" },
                                  "triggers" },
    [EDITOR_HK_VIEW_HIDE_PANELS] = { "view.hide_panels", "View", "Hide / show panels",
                                     "Hide / show every panel but World", G, EDGE, { "tab" }, NULL },
    [EDITOR_HK_VIEW_RESET_LAYOUT] = { "view.reset_layout", "View", "Reset layout", "Reset layout", G, EDGE,
                                      { "ctrl+shift+r" }, NULL },
    [EDITOR_HK_VIEW_SNAP] = { "view.snap", "View", "Snap to grid", "Snap to grid (toggle)", G, EDGE, { "ctrl+g" }, NULL },
    [EDITOR_HK_VIEW_SNAP_HOLD] = { "view.snap_hold", "View", "Snap while dragging (hold)",
                                   "Snap while dragging a panel (hold; the toggle is untouched)", G, HELD,
                                   { "ctrl" }, NULL },
    [EDITOR_HK_VIEW_POPOUT] = { "view.popout", "View", "Pop out panel",
                                "Pop out the panel under the pointer into its own window (World when over none)", G,
                                EDGE, { "shift+f12" }, NULL },
    [EDITOR_HK_VIEW_CLOSE_PANEL] = { "view.close_panel", "View", "Close panel",
                                     "Hide the panel under the pointer (the View menu brings it back)", G, EDGE,
                                     { "ctrl+w" }, NULL },
};

#undef G
#undef W
#undef L
#undef R
#undef S
#undef N
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
    static const char* const k_names[6] = { "G", "W", "L", "R", "S", "no loc" };
    int used = 0;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( int i = 0; i < 6; i++ )
    {
        if( !(contexts & (1u << i)) )
            continue;
        used += snprintf(out + used, (size_t)(out_size - used), "%s%s", used ? ", " : "", k_names[i]);
        if( used >= out_size )
            return;
    }
}

/* L excludes S and N: both say no loc is selected. */
static int
hk_ctx_exclusive(
    unsigned a,
    unsigned b)
{
    if( a == EDITOR_HK_CTX_L )
        return b == EDITOR_HK_CTX_S || b == EDITOR_HK_CTX_N;
    if( b == EDITOR_HK_CTX_L )
        return a == EDITOR_HK_CTX_S || a == EDITOR_HK_CTX_N;
    return 0;
}

int
Editor_HotkeyContextsOverlap(
    unsigned a,
    unsigned b)
{
    for( int i = 0; i < 6; i++ )
    {
        if( !(a & (1u << i)) )
            continue;
        for( int j = 0; j < 6; j++ )
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

static int
hk_share_chord(
    const struct Editor_Hotkeys* hk,
    int a,
    int b)
{
    for( int i = 0; i < EDITOR_HK_CHORDS; i++ )
    {
        struct Editor_Chord const ca = hk->bind[a].chord[i];
        if( ca.key == TORIRSK_UNKNOWN )
            continue;
        for( int j = 0; j < EDITOR_HK_CHORDS; j++ )
        {
            struct Editor_Chord const cb = hk->bind[b].chord[j];
            if( cb.key == ca.key && cb.mods == ca.mods )
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

#if defined(__APPLE__)
#define HK_CTRL_TEXT "Cmd"
#else
#define HK_CTRL_TEXT "Ctrl"
#endif

static void
hk_key_display(
    int key,
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
        { TORIRSK_CTRL, HK_CTRL_TEXT }, { TORIRSK_ALT, "Alt" },       { TORIRSK_TAB, "Tab" },
        { TORIRSK_SPACE, "Space" },    { TORIRSK_LEFT, "Left" },      { TORIRSK_RIGHT, "Right" },
        { TORIRSK_UP, "Up" },          { TORIRSK_DOWN, "Down" },      { TORIRSK_PAGE_UP, "PgUp" },
        { TORIRSK_PAGE_DOWN, "PgDn" }, { TORIRSK_COMMA, "," },        { TORIRSK_PERIOD, "." },
        { TORIRSK_MINUS, "-" },        { TORIRSK_EQUALS, "=" },       { TORIRSK_LBRACKET, "[" },
        { TORIRSK_RBRACKET, "]" },     { TORIRSK_SEMICOLON, ";" },    { TORIRSK_APOSTROPHE, "'" },
        { TORIRSK_GRAVE, "`" },        { TORIRSK_SLASH, "/" },        { TORIRSK_BACKSLASH, "\\" },
        { TORIRSK_HOME, "Home" },      { TORIRSK_END, "End" },
    };
    const char* name;

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
Editor_ChordDisplay(
    struct Editor_Chord chord,
    char* out,
    int out_size)
{
    char key[16];

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( chord.key == TORIRSK_UNKNOWN )
        return;
    hk_key_display(chord.key, key, (int)sizeof(key));
    snprintf(out, (size_t)out_size, "%s%s%s%s", (chord.mods & EDITOR_HK_MOD_CTRL) ? HK_CTRL_TEXT "+" : "",
        (chord.mods & EDITOR_HK_MOD_SHIFT) ? "Shift+" : "", (chord.mods & EDITOR_HK_MOD_ALT) ? "Alt+" : "", key);
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
    unsigned const contexts = in ? (in->contexts | EDITOR_HK_CTX_G) : 0;
    unsigned held_mods;
    int candidate[EDITOR_HK_COUNT];
    struct Editor_Chord candidate_chord[EDITOR_HK_COUNT];
    int candidate_count = 0;

    assert(hk);
    assert(in);
    assert(out);
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
