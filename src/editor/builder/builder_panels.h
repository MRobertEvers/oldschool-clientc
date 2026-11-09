#ifndef SRC_EDITOR_BUILDER_BUILDER_PANELS_H
#define SRC_EDITOR_BUILDER_BUILDER_PANELS_H

/**
 * The builder panels on the editor's chrome (docs/MAPED_DESIGN.html
 * #locbuilder, #npcbuilder; MAPED_IMPLEMENTATION.md phase 3 `builders`):
 *
 *   Editor_LocBuilder_Register    the loc interaction builder: subject, the
 *                                 op/ap/use-on triggers and their steps, the
 *                                 server fields with the door and ladder
 *                                 presets, Regenerate / Check / Save / Detach
 *   Editor_NpcBuilder_Register    the same for an npc, plus the dialogue
 *                                 graph: drawn in a GRAPH well, outlined as a
 *                                 TREE (the accessible form, and the one the
 *                                 browser chrome tab uses)
 *   Editor_ShellEditor_Register   the multiloc / multinpc shell table with
 *                                 the value column computed, and the preview
 *                                 control that sets STATE_VAR_PREVIEW
 *
 * Those three (builder_session.c) bind the panels to the client's struct
 * Editor -- Editor_Apply, the mirror's documents, the catalog. The panels
 * themselves (builder_panels.c) reach the world only through struct
 * Builder_Services, so a test drives them over a fake and the real chrome
 * model without linking the client.
 */

#include "builder_chat.h"
#include "builder_emit.h"
#include "builder_lint.h"
#include "builder_ssc.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_npc.h"

#include <stdint.h>

struct EditorUi;

/** What the panels learn about a loc or npc record from the catalog. */
struct Builder_RecordInfo
{
    int found;
    int id;
    char name[BUILDER_NAME_MAX];
    /** The cache's `name=`, the menu's noun. */
    char display[BUILDER_NAME_MAX];
    /** op1..op5 as the cache states them; "" when absent. */
    char ops[5][BUILDER_NAME_MAX];
    /** An npc's movement statements from the merged record (editor_npc.h),
     *  for the editor's "will:" line; `has_movement` 0 when the service
     *  could not say (no catalog). */
    int has_movement;
    struct Editor_NpcMovement movement;
};

#define BUILDER_EXISTING_MAX 64

/** The triggers the tree declares for one subject (catalog TRIGGER rows),
 *  with the strings owned here. */
struct Builder_ExistingList
{
    char names[BUILDER_EXISTING_MAX][BUILDER_TRIGGER_NAME_MAX];
    char files[BUILDER_EXISTING_MAX][256];
    struct Builder_ExistingTrigger rows[BUILDER_EXISTING_MAX];
    int count;
};

/** One spawn of the editor's npc (the NPC editor's Spawns tab, its minimap):
 *  a `.spawn` row of an open square and which file holds it. */
struct Builder_NpcSpawn
{
    int x;
    int z;
    int level;
    /** 1 the edited file, 0 the generated one or a quest's / skill's. */
    int edited;
    /** A quest's or skill's own .spawn file. */
    int scripted;
    /** A generated row the edited file hides. */
    int suppressed;
    /** The row's own spelling of the npc ("hans" or "#3105"). */
    char name[64];
};

/** What the minimap knows of one tile (tile_info): the loaded scene's
 *  collision flags (engine/world_builder/collision_map.h COLL_FLAG_*) and
 *  the square's jm2 settings byte (1 block, 2 link-below, 4 roof). */
struct Builder_TileInfo
{
    /** 1 when the tile's square is loaded in the world (collision known). */
    int in_scene;
    /** 1 when its square is open in the editor (settings known). */
    int in_doc;
    int collision;
    int settings;
};

struct Builder_Services
{
    void* user_data;
    /** Commands to the authority, and the mirror's text of a document. */
    struct Builder_Submit submit;
    /** Save one document on the authority: 1 written, 0 nothing to save,
     *  -1 refused. May be NULL (Save then says it is unavailable). */
    int (*save)(void* user_data, enum Editor_DocKind kind, const char* key);
    /** The record `name` (or, with name "", `id`) of a loc / npc. Returns
     *  1 and fills `out` when found. May be NULL. */
    int (*record)(
        void* user_data,
        enum Builder_Subject subject,
        const char* name,
        int id,
        struct Builder_RecordInfo* out);
    /** Every trigger the tree declares for `subject`. May be NULL (no
     *  conflict check: the panel says so). */
    int (*triggers)(void* user_data, const char* subject, struct Builder_ExistingList* out);
    /** Publish a shared-state field (STATE_VAR_PREVIEW). May be NULL. */
    int (*state_set)(void* user_data, uint32_t key, const int32_t* values, int count);
    /** Copy the loc / npc definition `name` to `new_name` with a fresh id
     *  (SAVE_AS, one undo step). 1 when the copy was made. May be NULL
     *  (Copy then says it is unavailable). */
    int (*copy_record)(void* user_data, enum Builder_Subject subject, const char* name, const char* new_name);
    /** The lint's names (builder_lint.h): 1 when the tree names a var
     *  (`kind` BUILDER_NAME_VAR: a varp or varbit record) or a proc
     *  (BUILDER_NAME_PROC: a [proc,<name>] trigger), 0 when it does not, -1
     *  when it cannot say (no catalog). May be NULL. Either way the unknown
     *  var / proc rows are then not checked, and the panel says so. Asked
     *  once per name per open subject. */
    int (*name_known)(void* user_data, int kind, const char* name);
    /** The chat-box preview's head: a scene sprite the host rendered of
     *  `npc`'s chathead with expression `expr` (GRAPH_IMAGE's id, its
     *  version in `out_version`), 1 when there is one. May be NULL: the
     *  preview draws the head's box and says no preview pipeline is bound. */
    int (*chat_head)(void* user_data, const char* npc, const char* expr, int* out_scene_id, uint32_t* out_version);
    /** "From selection": the record name the editor's selection points at
     *  for `subject` -- the Spawns panel's anchor row, else the Catalog's
     *  pick (an npc), the selected loc (a loc) -- into `out`. 0 when
     *  nothing is selected. May be NULL (the button then says so). */
    int (*selection)(void* user_data, enum Builder_Subject subject, char* out, size_t out_size);
    /** "Open in editor": `rel_path` under server/scripts at `line` in the
     *  external editor (the Triggers panel's TORIRS_EDITOR_OPEN_CMD). 1 when
     *  it opened. May be NULL. */
    int (*open_file)(void* user_data, const char* rel_path, int line);

    /* The NPC editor's Movement and Spawns tabs (docs/NPC_EDITOR_DESIGN.md
     * 3.4-3.6). All may be NULL: the tabs then say what is missing. */
    /** `npc`'s movement statements as the session's documents hold them
     *  (unsaved edits included), and into `target` the file an edit would
     *  be written to (3.8). 0 when the record cannot be opened. */
    int (*movement)(void* user_data, const char* npc, struct Editor_NpcMovement* out, char* target, size_t target_size);
    /** Set one field (`key`, `value`; "" removes) of `npc`'s record in the
     *  write target, one undo step named `what`. Commands taken, 0 nothing
     *  changed, -1 refused. */
    int (*movement_set)(void* user_data, const char* npc, const char* key, const char* value, const char* what);
    /** Replace `npc`'s route and pin its ledger (Editor_NpcDocSetRoute),
     *  one undo step named `what`. Commands taken, 0 nothing, -1 refused. */
    int (*route_set)(void* user_data, const char* npc, const struct Editor_NpcWaypoint* waypoints, int count,
        enum Editor_NpcMode mode, const char* what);
    /** What the minimap draws for ABSOLUTE tile (x, z, level). */
    void (*tile_info)(void* user_data, int x, int z, int level, struct Builder_TileInfo* out);
    /** Steps of the shortest walk between two tiles on `level`: 0+, -1 no
     *  path, -2 not in the loaded scene (EditorUiAppServices path_steps). */
    int (*path_steps)(void* user_data, int ax, int az, int bx, int bz, int level);
    /** `npc`'s (`id`'s) spawns across the open squares, at most `max`. */
    int (*spawns)(void* user_data, const char* npc, int id, struct Builder_NpcSpawn* out, int max);
    /** Put the World's camera over the tile; 0 when its square is not loaded. */
    int (*goto_tile)(void* user_data, int x, int z, int level);
    /** The Spawns tab's edits through the Spawns panel: select `spawn` there
     *  (1 when it could), place `npc` at the latched tile (commands taken),
     *  delete / suppress `spawn` (commands taken). */
    int (*spawn_select)(void* user_data, const struct Builder_NpcSpawn* spawn);
    int (*spawn_place)(void* user_data, const char* npc, int x, int z, int level);
    int (*spawn_delete)(void* user_data, const struct Builder_NpcSpawn* spawn);
    /** The panel is destroyed: free what the services kept for it. May be
     *  NULL. */
    void (*release)(void* user_data);
    /** TORIRS_EDITOR_NPC's `photo`: lay chrome panel `panel` over the whole
     *  right column, the other View-menu panels closed, so a headless
     *  photograph shows the tab. May be NULL. */
    void (*photo_place)(void* user_data, int panel);
};

#define BUILDER_NAME_VAR 0
#define BUILDER_NAME_PROC 1

/** Register the panels over explicit services (what the tests call). The
 *  services are copied. */
void
Builder_LocPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services);

void
Builder_NpcPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services);

void
Builder_ShellPanelRegister(
    struct EditorUi* ui,
    const struct Builder_Services* services);

/* ---- the client binding (builder_session.c) ------------------------------- */

void
Editor_LocBuilder_Register(struct EditorUi* ui);

void
Editor_NpcBuilder_Register(struct EditorUi* ui);

void
Editor_ShellEditor_Register(struct EditorUi* ui);

/* ---- what the tests read -------------------------------------------------- */

/** The panel names Editor_UiFindPanel knows them by. The npc one is the
 *  NPC editor (docs/NPC_EDITOR_DESIGN.md): one tabbed window -- the View
 *  menu's "NPC editor", registered under the name the palette, the
 *  Encounters and Quest panels and the tests have always used. */
#define BUILDER_PANEL_LOC "locbuilder"
#define BUILDER_PANEL_NPC "npcbuilder"
#define BUILDER_PANEL_SHELL "shelleditor"

/** The NPC editor's tabs (ToriRSChrome_PanelActiveTab on its panel). */
enum Builder_NpcTab
{
    BUILDER_NPC_TAB_MOVEMENT = 0,
    BUILDER_NPC_TAB_SPAWNS = 1,
    BUILDER_NPC_TAB_INTERACTIONS = 2,
    BUILDER_NPC_TAB_DIALOGUE = 3,
    BUILDER_NPC_TAB_COUNT
};

/* ---- what the Movement tab's tests read -------------------------------- */

/** The route as the Movement tab holds it (the record's patrol rows), the
 *  selected waypoint and each leg's step count (-1 no path, -2 unknown:
 *  the scene is not loaded, -3 a teleport across levels). */
struct Builder_RouteView
{
    const struct Editor_NpcWaypoint* waypoints;
    int count;
    int selected;
    const int* legs;
    /** The effective behaviour, and the file an edit is written to. */
    const struct Editor_NpcEffective* effective;
    const char* target;
    /** The minimap well, and the well-local box of waypoint `i`
     *  (Builder_RouteWaypointBox). */
    int w_minimap;
    int tool;
};

int
Builder_RouteInspect(
    struct EditorUi* ui,
    struct Builder_RouteView* out);

/** The minimap's well-local box of waypoint `index`; 0 when off the view. */
int
Builder_RouteWaypointBox(
    struct EditorUi* ui,
    int index,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

/** The minimap's well-local centre of ABSOLUTE tile (x, z); 0 off view. */
int
Builder_RouteTilePoint(
    struct EditorUi* ui,
    int x,
    int z,
    int* out_x,
    int* out_y);

struct Builder_PanelView
{
    /** The model the panel holds; NULL before a subject is open. */
    const struct Builder_Model* model;
    int read_only;
    const struct Builder_Issues* issues;
    /** The last Check's result; status NOT_RUN before the first. */
    const struct Builder_SscResult* check;
    /** The panel's status line. */
    const char* status;
    /** The dialogue (NPC builder; zero for the loc builder): the lint rows,
     *  the selected node (-1 none) and chat page, the selected node's text
     *  laid out at the chat box's line rule, and whether the new-node chip
     *  row is open (a port dropped on empty space). */
    const struct Builder_LintRows* lint;
    int sel_node;
    int sel_page;
    const struct Builder_ChatLayout* layout;
    int chips_open;
    /** The GRAPH wells: the graph, the text's pages, the chat-box preview
     *  (-1 on the loc builder). */
    int w_graph;
    int w_pages;
    int w_preview;
};

/** What a builder panel registered as `name` currently shows; 0 when there
 *  is no such panel. */
int
Builder_PanelInspect(
    struct EditorUi* ui,
    const char* name,
    struct Builder_PanelView* out);

/** Script the panels the way a user would, for the tests and for replayed
 *  input: open a subject, regenerate, check, save, detach. Each returns 1 on
 *  success. */
int
Builder_PanelOpenSubject(
    struct EditorUi* ui,
    const char* name,
    const char* subject);

/** Replace the panel's model (as an edit) and regenerate. */
int
Builder_PanelSetModel(
    struct EditorUi* ui,
    const char* name,
    const struct Builder_Model* model);

int
Builder_PanelCheck(
    struct EditorUi* ui,
    const char* name);

int
Builder_PanelSaveAll(
    struct EditorUi* ui,
    const char* name);

int
Builder_PanelDetach(
    struct EditorUi* ui,
    const char* name);

/**
 * The dialogue graph's hotkeys (editor_hotkeys.h, context Q): Auto-layout
 * (Ctrl+L), New node (N) and Delete (edit.delete while the graph holds the
 * keyboard) on the NPC builder's graph. Returns 1 when the action was the
 * graph's -- done, or refused with the reason in the status bar -- and 0
 * when it is not (Delete without the graph's focus: the World's delete runs).
 */
int
Builder_GraphHotkey(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action);

/** 1 while the NPC builder's dialogue graph well holds the chrome's
 *  keyboard: context Q (editor_hotkeys.h). Any other GRAPH well (the
 *  palettes, the chip strips, the Locs strip, the square picker) is not. */
int
Builder_GraphFocused(struct EditorUi* ui);

/**
 * The frame's keys while the dialogue graph holds the keyboard. A focused
 * well is a chrome focus, so the App's text-field gate keeps
 * Editor_HotkeysResolve shut and the chrome keeps letters away from the
 * well: this is the one way N and Ctrl+L reach the graph from the keyboard.
 * Each down edge of `in` (with the held modifiers) that is a context-Q row
 * runs through Builder_GraphHotkey; `out` is filled as the resolver fills
 * it (the fired actions and chords, their keys claimed and consumed).
 * Returns the count fired; 0 and an empty `out` when the graph is not
 * focused.
 */
int
Builder_GraphKeys(
    struct EditorUi* ui,
    const struct Editor_HotkeyInput* in,
    struct Editor_HotkeyFrame* out);

/** Select dialogue node `index` (-1 none) on the NPC builder, as a click on
 *  its GRAPH item or its outline row does. Returns 0 without one. */
int
Builder_PanelSelectNode(
    struct EditorUi* ui,
    int index);

/** Activate lint row `row` (as shown, after the find box) as a click on it
 *  does: the node (or trigger step) it names is selected. */
int
Builder_PanelOpenLint(
    struct EditorUi* ui,
    int row);

/** The shell editor's current shell, or NULL before one is open. */
const struct Builder_Shell*
Builder_ShellPanelShell(struct EditorUi* ui);

#endif
