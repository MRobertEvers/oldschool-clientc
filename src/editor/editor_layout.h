#ifndef SRC_EDITOR_EDITOR_LAYOUT_H
#define SRC_EDITOR_EDITOR_LAYOUT_H

/**
 * The editor's dock layout as a file: `editor_layout.ini`, a device-local
 * preference like `preferences.ini` (src/game/rs_prefs.h) and kept beside it,
 * but its own file -- it belongs to the editor, is written by the editor's
 * own debounce, and a client that never opens the editor never reads it.
 *
 * This module is the pure half: the file's text <-> struct EditorLayoutFile.
 * No chrome, no registry, no IO. editor_ui.c turns the live panels into a
 * file struct and back (Editor_UiLayoutCapture / Editor_UiLayoutApply);
 * task_editor_layout_io.c carries the text across the platform IO queue
 * (ToriRS_IO_QueueFileRead / QueueFileWrite), which is a disk file on the
 * desktop and an IndexedDB record on the web (platform_web_io.js ->
 * host.readClientFile / host.writeClientFile).
 *
 * The format (hand-editable; unknown sections and keys are ignored, a missing
 * key keeps the panel's current value):
 *
 *     [layout]
 *     version = 1
 *     surface_w = 1530      ; the surface the layout was made on, device px
 *     surface_h = 1006
 *     scale = 1             ; the chrome scale then
 *     snap = 1              ; View > Snap to grid
 *
 *     [panel world]         ; one section per registry module, by its name
 *     visible = 1
 *     user_placed = 0       ; 1 = the user dragged or resized it
 *     x = 38                ; geometry in chrome pixels AT SCALE 1, so a
 *     y = 22                ; layout saved at 2x fits at 1x and back
 *     w = 1155              ; fixed width / height, 0 = sized to content
 *     h = 984
 *     maximised = 0         ; 1 = maximised over the dock (title double-click);
 *                           ; x/y/w/h and user_placed are then the box it
 *                           ; restores to
 *
 *     [group right-top]     ; one section per panel group (editor_ui.h), by
 *     visible = 1           ; its name: the same keys as a panel, the box
 *     user_placed = 0       ; being the whole group (its tab strip and the
 *     x = 1224              ; shown tab under it)
 *     y = 22
 *     w = 306
 *     h = 420
 *     maximised = 0
 *     shown = catalog       ; the module whose tab is in front
 *
 * Workspaces (workbench 6.8, design section 3 "Workspaces"): `[layout]` says
 * which one is current (`workspace = build`), and each workspace's own layout
 * is a section of one line per panel and per group, the same fields in a
 * row (a "-" for a field the capture did not state):
 *
 *     [workspace terrain]
 *     surface = 1530 1006 1  ; the surface and scale it was made on
 *     ; panel.<module> = visible user_placed x y w h maximised
 *     panel.squares = 1 0 1224 330 306 300 0
 *     ; group.<name> = visible user_placed x y w h maximised shown
 *     group.right-mid = 1 0 1224 22 306 300 0 terrain
 *
 * The [panel] and [group] sections above are the CURRENT workspace's live
 * layout, as before workspaces (a file from before them has no [workspace]
 * section and is the Build workspace's); its [workspace] section, written
 * beside them, holds the same. A workspace with no section starts from its
 * default (editor_ui.h, Editor_UiSetWorkspace).
 *
 * A module that is in a group keeps its [panel] section, with only `visible`
 * in it: whether it is open in its group's strip. Its geometry is the
 * group's. A file written before groups existed (no [group] sections, every
 * [panel] with its own box) still applies: each member's `visible` says
 * whether it is in its strip, its box is ignored, and the default layout docks
 * the group.
 *
 * What the editor does with it (Editor_UiLayoutApply, the policy):
 *   - snap and every panel's `visible` are applied whatever the surface: what
 *     the user opened and closed is not a property of the window size;
 *   - a panel with `user_placed = 1` gets its saved geometry back (scaled to
 *     the current chrome scale and pulled back onto the surface if the
 *     surface shrank) and the default layout leaves it there;
 *   - every other panel is placed by the default layout (Editor_UiLayout)
 *     first, which also decides who is docked in the column; then, when the
 *     file was made for the SAME surface (surface_w, surface_h) and the same
 *     scale, each of those panels takes the file's x/y/w/h as well -- the
 *     column comes back exactly as the user left it (heights shared out
 *     after a panel was closed, say). On a DIFFERENT surface (a stale file:
 *     another monitor, a resized window) their boxes are ignored and the
 *     default layout's answer for this surface stands, rather than forcing a
 *     layout made for other dimensions.
 *   - any later surface or scale change re-runs the default layout for the
 *     unplaced panels, as without a file; View > Reset layout returns to the
 *     modules' default set and placement (not to the file), and the editor
 *     then saves that.
 *
 * Contract violations assert (CLAUDE.md); a malformed value is a hand-edit
 * and is skipped, not an abort.
 */

#define EDITOR_LAYOUT_VERSION 1
/** Panels one file holds (the registry's own cap, EDITOR_UI_PANELS_MAX). */
#define EDITOR_LAYOUT_PANELS_MAX 48
/** Groups one file holds (the registry's own cap, EDITOR_UI_GROUPS_MAX). */
#define EDITOR_LAYOUT_GROUPS_MAX 8
/** [workspace] sections one file holds (the editor's four, room for more a
 *  later build may name; a section past it is skipped). */
#define EDITOR_LAYOUT_WORKSPACES_MAX 8
#define EDITOR_LAYOUT_NAME_MAX 32
/** `TORIRS_EDITOR_LAYOUT` unset: this name, resolved by the platform the way
 *  `preferences.ini` is (the working directory, or IndexedDB on the web). */
#define EDITOR_LAYOUT_DEFAULT_PATH "editor_layout.ini"

struct EditorLayoutPanel
{
    char name[EDITOR_LAYOUT_NAME_MAX];
    /** Each `has_*` says the file stated it; an unstated field is left alone. */
    int has_visible;
    int visible;
    int has_user_placed;
    int user_placed;
    /** Chrome pixels at scale 1. */
    int has_x;
    int x;
    int has_y;
    int y;
    int has_w;
    int w;
    int has_h;
    int h;
    /** Maximised over the dock (a double-click on its title): x/y/w/h and
     *  user_placed above are then the box it restores to. */
    int has_maximised;
    int maximised;
};

/** A [group <name>] section: the panel keys, the box being the group's, and
 *  which member's tab is in front. */
struct EditorLayoutGroup
{
    char name[EDITOR_LAYOUT_NAME_MAX];
    int has_visible;
    int visible;
    int has_user_placed;
    int user_placed;
    /** Chrome pixels at scale 1: the whole group, strip and shown tab. */
    int has_x;
    int x;
    int has_y;
    int y;
    int has_w;
    int w;
    int has_h;
    int h;
    int has_maximised;
    int maximised;
    /** The registry module whose tab is in front ("" when unstated). */
    int has_shown;
    char shown[EDITOR_LAYOUT_NAME_MAX];
};

/** A [workspace <name>] section: one workspace's layout, the panels and
 *  groups as the top-level sections hold them, and the surface and scale it
 *  was made for (0 when unstated). */
struct EditorLayoutWorkspace
{
    char name[EDITOR_LAYOUT_NAME_MAX];
    int surface_w;
    int surface_h;
    int scale;
    struct EditorLayoutPanel panels[EDITOR_LAYOUT_PANELS_MAX];
    int panel_count;
    struct EditorLayoutGroup groups[EDITOR_LAYOUT_GROUPS_MAX];
    int group_count;
};

struct EditorLayoutFile
{
    int version;
    /** The surface (device pixels) and chrome scale the layout was made for;
     *  0 when the file did not say. */
    int surface_w;
    int surface_h;
    int scale;
    int has_snap;
    int snap;
    struct EditorLayoutPanel panels[EDITOR_LAYOUT_PANELS_MAX];
    int panel_count;
    struct EditorLayoutGroup groups[EDITOR_LAYOUT_GROUPS_MAX];
    int group_count;
    /** `workspace =` in [layout]: the current workspace's name. */
    int has_workspace;
    char workspace[EDITOR_LAYOUT_NAME_MAX];
    struct EditorLayoutWorkspace workspaces[EDITOR_LAYOUT_WORKSPACES_MAX];
    int workspace_count;
};

/**
 * Where the layout lives: `TORIRS_EDITOR_LAYOUT` when set, the default name
 * when unset, NULL when set to the empty string -- persistence off, the same
 * rule as TORIRS_PREFS (headless harnesses set it empty so they never read a
 * layout another run left behind).
 */
const char*
EditorLayout_Path(void);

/** Empty file: no surface, no snap, no panels. */
void
EditorLayout_Clear(struct EditorLayoutFile* file);

/**
 * Parse `size` bytes of layout text into `out` (cleared first). Returns 1
 * when the text held a `[layout]` section, 0 otherwise (not a layout file:
 * nothing applies). Panels past EDITOR_LAYOUT_PANELS_MAX, groups past
 * EDITOR_LAYOUT_GROUPS_MAX, names longer than EDITOR_LAYOUT_NAME_MAX - 1 and
 * non-numeric values (but a group's `shown`, which is a name) are skipped.
 */
int
EditorLayout_Decode(
    struct EditorLayoutFile* out,
    const void* data,
    int size);

/**
 * Render `file` as text. `*out_data` is allocated from ToriMem_Process() and
 * is the caller's (ToriMem_Free); `*out_size` excludes the terminating NUL
 * that is also written.
 */
void
EditorLayout_Encode(
    const struct EditorLayoutFile* file,
    char** out_data,
    int* out_size);

/** The panel named `name`, or NULL when the file does not mention it. */
const struct EditorLayoutPanel*
EditorLayout_Find(
    const struct EditorLayoutFile* file,
    const char* name);

/** The panel named `name`, appended when absent. Asserts on a full file. */
struct EditorLayoutPanel*
EditorLayout_Add(
    struct EditorLayoutFile* file,
    const char* name);

/** The group named `name`, or NULL when the file does not mention it. */
const struct EditorLayoutGroup*
EditorLayout_FindGroup(
    const struct EditorLayoutFile* file,
    const char* name);

/** The group named `name`, appended when absent. Asserts on a full file. */
struct EditorLayoutGroup*
EditorLayout_AddGroup(
    struct EditorLayoutFile* file,
    const char* name);

/** The workspace named `name`, or NULL when the file has no section for it. */
const struct EditorLayoutWorkspace*
EditorLayout_FindWorkspace(
    const struct EditorLayoutFile* file,
    const char* name);

/** The workspace named `name`, appended (empty) when absent. Asserts on a
 *  full file. */
struct EditorLayoutWorkspace*
EditorLayout_AddWorkspace(
    struct EditorLayoutFile* file,
    const char* name);

/** The file's top-level panels and groups (the live layout) as workspace
 *  `ws`'s section, with the file's surface and scale. */
void
EditorLayout_WorkspaceFromTop(
    struct EditorLayoutWorkspace* ws,
    const struct EditorLayoutFile* file);

/** Workspace `ws`'s layout as a file's top-level panels and groups, its
 *  surface and scale, ready for Editor_UiLayoutApply; `out` is cleared
 *  first and carries no workspace. */
void
EditorLayout_TopFromWorkspace(
    struct EditorLayoutFile* out,
    const struct EditorLayoutWorkspace* ws);

#endif
