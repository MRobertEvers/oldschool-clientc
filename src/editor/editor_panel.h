#ifndef SRC_EDITOR_EDITOR_PANEL_H
#define SRC_EDITOR_EDITOR_PANEL_H

/**
 * The map editor's panel: tool, brush, palettes, save and bake.
 *
 * Built entirely on ToriRSChrome, and that is a hard rule rather than a
 * convenience. The editor exists to repair content, so its chrome must not
 * depend on the content being repairable — it has to open on a half-broken
 * bake, a foreign revision, or a square whose configs are mid-edit. ToriRSChrome
 * has its fonts compiled in and reaches no cache, which is exactly that
 * property; a panel built from cache interfaces would go dark precisely when
 * it is needed.
 *
 * The dividing line the panel keeps: CHROME IS BAKED, CONTENT IS DRAWN. Row
 * text, boxes and the dropdown arrow come from the overlay's own primitives.
 * The palette ENTRIES are content — underlay and overlay names read out of the
 * cache at runtime — and degrade to plain ids when the configs cannot be read,
 * so the palette still operates on a cache the editor cannot fully decode.
 */

#include "editor.h"

struct ToriRSChrome;
struct App;

/** Which edit the next click makes. */
enum Editor_Tool
{
    /** Read-only: report what is under the cursor. */
    EDITOR_TOOL_SELECT = 0,
    EDITOR_TOOL_HEIGHT,
    EDITOR_TOOL_UNDERLAY,
    EDITOR_TOOL_OVERLAY,
    EDITOR_TOOL_FLAGS,
    /** Stamp the catalog's picked loc at the clicked tile. */
    EDITOR_TOOL_LOC_PLACE,
    /** Move the SELECTED loc to the clicked tile. */
    EDITOR_TOOL_LOC_MOVE,
    EDITOR_TOOL_COUNT
};

/** Palette entries the dropdowns borrow. Sized for the flotype/underlay id
 *  space, which is a byte for underlays and a u16 for overlays. */
#define EDITOR_PALETTE_MAX 512
#define EDITOR_PALETTE_LABEL_MAX 32

/** Session-placed spawns awaiting save. Bounded generously: hand-placing
 *  hundreds of npcs in one sitting is not the workflow this serves. */
#define EDITOR_SPAWN_MAX 256

struct Editor_SpawnEntry
{
    /** 0 npc, 1 obj (matches the .spawn file's two sections). */
    int is_obj;
    int id;
    /** ABSOLUTE world tile, as the .spawn grammar stores. */
    int abs_x;
    int abs_z;
    int level;
    int count; /* obj stack size; 1 for npcs */
    char name[48];
};

/** Catalog rows the loc/npc/obj picker holds. A world load brings a few
 *  hundred locs into the provider; this bounds a region, not the cache. */
#define EDITOR_CATALOG_MAX 512
#define EDITOR_CATALOG_LABEL_MAX 48

/** Rows the multiloc variant list holds: the shell plus its transform table.
 *  Transform tables are short -- a door is two rungs, a quest prop a handful
 *  -- and the cap only bounds what is LISTED, never what the cache holds. */
#define EDITOR_LOC_VARIANT_MAX 33

/**
 * Squares the browser can hold coordinates for, and rows it can show.
 *
 * Two different numbers on purpose. Every square in the tree is SEARCHABLE --
 * holding 4096 coordinate pairs costs 32KB and means the filter can reach any
 * of them -- while only 512 are DRAWN at once, because a dropdown nobody can
 * scroll to the end of is not a list. Capping what is searchable would make
 * squares silently unreachable; capping what is drawn only makes the user
 * type.
 */
#define EDITOR_SQUARE_MAX 4096
#define EDITOR_SQUARE_ROWS 512

/**
 * The row handles one loc-config readout writes to.
 *
 * Two readouts show a loc record -- the Loc panel's selection and the
 * catalog's multiloc variant -- and they answer the same question, so they
 * share the fill instead of each formatting the record its own way. Placement
 * is deliberately absent: a variant has no tile.
 */
struct Editor_LocRows
{
    int name;
    int desc;
    int cfg;
    int model;
    int render;
    int ops;
};

/** What EDITOR_TOOL_SELECT is latched onto. Own concept from the loc editor's
 *  locedit_* fields (src/app.c) -- that tool targets a loc to nudge/rotate;
 *  this one targets a tile or loc the OTHER tools (height/underlay/overlay/
 *  flags) act on, and the two panels can be open at once with different
 *  subjects. */
enum Editor_SelectionKind
{
    EDITOR_SELECTION_NONE = 0,
    EDITOR_SELECTION_TERRAIN,
    EDITOR_SELECTION_LOC,
    /** A session-placed npc/obj spawn (the editor world has no server, so
     *  every npc and obj on screen is one this session placed). */
    EDITOR_SELECTION_NPC,
    EDITOR_SELECTION_OBJ
};

struct Editor_Panel
{
    /**
     * The editor session, for publishing shared state (selection, tool) to
     * this connection's Client. Borrowed from the App, which owns both; NULL
     * in a panel that has no session yet, where publishing is a no-op.
     *
     * The panel PUBLISHES on its own latching and APPLIES on the server's
     * echo (Editor_PanelApplySharedState) — the same rule mirrors follow
     * for the document, which is what lets a controller connection with no
     * world follow a viewer connection's clicks.
     */
    struct Editor* editor;
    /** Set while applying a received state fact, so the apply path's own
     *  latching does not re-publish what the server just said. */
    int applying_shared_state;

    /** Chrome scale the panels were last placed for. A display change moves
     *  them; see Editor_PanelPlaceForScale. */
    int placed_scale;
    /** Where the stacker last parked the Squares panel, so it can tell its own
     *  placement from one the user dragged and stop following in that case.
     *  See EDITOR_PANEL_SQUARE_Y for why the two panels must not overlap. */
    int square_stack_y;
    int panel;
    int built;

    /* Rows. */
    int row_status;
    int row_square;
    int row_tile;
    int row_authored;
    int dd_tool;
    int dd_underlay;
    int dd_overlay;
    int in_height;
    int dd_shape;
    int dd_rotation;
    int cb_flag_block;
    int cb_flag_bridge;
    int cb_flag_roof;
    int cb_flag_below;
    int dd_level;
    int dd_vis;
    int cb_vis_solo;
    int dd_loc_shape;
    int dd_loc_rot;
    /** The File/Edit bar across the top of the screen. */
    int menubar_panel;
    int menu_file;
    int menu_edit;
    int menu_view;
    /** "Delete selection": shown whenever something deletable is selected.
     *  The separate Delete tool this replaced made deletion a MODE, and a
     *  mode for a one-shot act is a tool switch you have to undo afterwards. */
    int item_delete;

    int visible;
    enum Editor_Tool tool;

    /**
     * Palette storage. The dropdowns borrow these, so they must outlive every
     * Build — which is why they live in the panel rather than on a stack.
     */
    char underlay_labels[EDITOR_PALETTE_MAX][EDITOR_PALETTE_LABEL_MAX];
    char const* underlay_options[EDITOR_PALETTE_MAX];
    int underlay_ids[EDITOR_PALETTE_MAX];
    int underlay_count;

    char overlay_labels[EDITOR_PALETTE_MAX][EDITOR_PALETTE_LABEL_MAX];
    char const* overlay_options[EDITOR_PALETTE_MAX];
    int overlay_ids[EDITOR_PALETTE_MAX];
    int overlay_count;

    /** Last tile the panel reported on, so the readout only rebuilds on change. */
    int shown_x;
    int shown_z;
    int shown_level;

    /** EDITOR_TOOL_SELECT's latch: what the other tools' click/readout targets
     *  when it is not just the live hover. sel_scene_x/z/level are valid for
     *  both kinds; sel_element_id/sel_loc_id only for EDITOR_SELECTION_LOC. */
    enum Editor_SelectionKind sel_kind;
    int sel_scene_x;
    int sel_scene_z;
    int sel_level;
    int sel_element_id;
    int sel_loc_id;
    /** The selected loc's placement, held here because the scene element dies
     *  on every reshape (a loc change is a delete + add): the overlay and the
     *  apply path re-find the NEW element by tile + shape when the old id has
     *  gone stale. */
    int sel_shape;
    int sel_angle;

    /* ---- the Loc panel ---------------------------------------------------
     *
     * The selected loc's config, readable: name, examine, the fields that
     * decide how it sits in the world, and its right-click ops. Appears when a
     * loc is selected, goes with the selection. Placement EDITING stays on the
     * tool panel (LocSh/LocRot + Apply): this panel answers "what is this",
     * the tool panel answers "make it different".
     */
    int loc_panel;
    struct Editor_LocRows loc_rows;
    /** Where it sits, which only a placed loc has. */
    int loc_row_place;
    /** "View tile": re-selects the loc's own tile as TERRAIN, so the tool
     *  panel flips to the ground under the loc -- height, flags, overlays --
     *  at the loc's level, without hunting for a bare pixel of it to click. */
    int loc_row_view_tile;
    /** Selection changed away from a loc; the tick hides the panel. Set by the
     *  terrain/clear paths, which have no ToriRSChrome handle to hide it with. */
    int loc_panel_stale;
    /** The tile the terrain fields were last seeded from, so seeding re-runs
     *  only when the latch moves -- not on the user's own edits. */
    int seeded_x;
    int seeded_z;
    int seeded_level;

    /* ---- the loc / npc / obj catalog -------------------------------------
     *
     * The picker: what is loaded, filtered by a typed substring, in a list you
     * choose from. Its own panel rather than rows on the tool panel, so it can
     * sit down the left of the screen at full height -- a catalog is a column,
     * and folding it into the tool panel would make both of them worse.
     */
    /* ---- the square browser ---------------------------------------------
     *
     * Every square the content tree ships, from EditorHost's square_list --
     * a host call rather than a directory read up here, because a browser
     * panel has no directory to read in the browser.
     */
    int square_panel;
    int sq_dd_list;
    int sq_in_search;
    int sq_row_current;
    int sq_item_open;
    /** Every square the host listed. Searchable in full. */
    int sq_coords[EDITOR_SQUARE_MAX * 2];
    int sq_total;
    /** The filtered view: labels drawn, and the index each row came from. */
    char sq_labels[EDITOR_SQUARE_ROWS][16];
    char const* sq_options[EDITOR_SQUARE_ROWS];
    int sq_row_index[EDITOR_SQUARE_ROWS];
    int sq_count;
    /** The filter the view was built for, so it rebuilds only on change. */
    char sq_shown_search[EDITOR_CATALOG_LABEL_MAX];
    /** Filled once; the content tree does not gain squares mid-session. */
    int sq_listed;
    /** A square the browser asked for. The app drains it and starts the load;
     *  the panel never calls the world loader itself. */
    int sq_open_pending;
    int sq_open_x;
    int sq_open_z;

    int catalog_panel;
    int cat_row_count;
    int cat_dd_kind;
    int cat_in_search;
    int cat_dd_list;
    int cat_row_picked;
    /** The picked entry's rendered model, host-fed (see the preview updater
     *  in app.c). */
    int cat_view;
    int cat_reset_view;

    /** Which kind the list is showing (enum CacheProvider_CatalogKind). */
    int cat_kind;
    /** The filter the list was last built for, so it rebuilds only on change. */
    char cat_shown_search[EDITOR_CATALOG_LABEL_MAX];
    int cat_shown_kind;
    /** Rebuilt when the filter changes or the world reloads. */
    int cat_built_epoch;

    char cat_labels[EDITOR_CATALOG_MAX][EDITOR_CATALOG_LABEL_MAX];
    char const* cat_options[EDITOR_CATALOG_MAX];
    int cat_ids[EDITOR_CATALOG_MAX];
    int cat_count;

    /* ---- the picked loc's transform table ("multiloc") --------------------
     *
     * A multiloc is a SHELL: LocType.transforms is the list of real locs the
     * varbit/varp picks between, and the shell itself usually carries no model
     * or ops of its own. Previewing the shell therefore answers nothing -- a
     * blank well and an empty op row -- which is why these rows exist: the
     * variant list makes each rung of the table pickable, and the preview and
     * the config readout below it follow the pick rather than the shell.
     *
     * Row 0 is always the shell. Rungs are addressed by their INDEX in the
     * table, because that index is the varbit/varp value that selects them --
     * the number a piece of content would have to write to make this rung the
     * live one -- and the last entry is the fallback for any value past the
     * end. A -1 rung is a legal entry meaning "nothing is drawn at this
     * value"; it is listed, and picking it previews nothing on purpose.
     *
     * Placement is unaffected: Place and the Edit menu's swap still stamp
     * cat_picked_id, the shell, because the shell is what a map stores.
     */
    int cat_row_multi;
    int cat_dd_variant;
    /** The chosen row's record, in the same rows the Loc panel uses -- same
     *  helper, so the two readouts cannot drift apart. */
    struct Editor_LocRows cat_var_rows;
    char cat_var_labels[EDITOR_LOC_VARIANT_MAX][EDITOR_CATALOG_LABEL_MAX];
    char const* cat_var_options[EDITOR_LOC_VARIANT_MAX];
    /** The loc each row previews; -1 for the shell row and for a -1 rung. */
    int cat_var_ids[EDITOR_LOC_VARIANT_MAX];
    int cat_var_count;
    /** Which row is chosen. 0 is the shell, so the default previews the pick. */
    int cat_var_choice;
    /** The pick, the kind and the resident-record count the variant list was
     *  built for, so it rebuilds when the pick moves, when the list flips to
     *  npcs or objs (which have no transform rows here), and again when a
     *  queued rung lands and can finally contribute its name. */
    int cat_var_shown_id;
    int cat_var_shown_kind;
    int cat_var_shown_epoch;

    /**
     * Edit level: -1 follows the pick, 0..3 pins a plane.
     *
     * The CACHE level, which is what a `.jm2` stores and therefore the only one
     * an edit can mean -- a column's draw and paint levels are derived and
     * differ on bridge decks (see World_TerrainDrawLevel). Pinning matters when
     * shaping a plane you are not standing on.
     */
    int edit_level;

    /**
     * Vis level: which planes the world VIEW draws. -1 shows every level.
     *
     * Distinct from edit_level, which says what a click means. Editing an
     * upper floor is unworkable while the floors above it are still painted
     * over it, and the roof check that caps the mask in the game has no
     * player to read here, so the editor states the cap itself.
     *
     * 0..3 means the levels 0..vis_level, cumulative exactly as the client's
     * view floor is -- VIS_BELOW's "revealed from the level below" only reads
     * correctly against a cumulative mask. `vis_solo` narrows it to the one
     * plane, for the case the floors underneath are the clutter.
     */
    int vis_level;
    int vis_solo;

    /*
     * Session spawn list: what Place stamped, what Save writes.
     *
     * KNOWN BOUNDARY, stated so it is not mistaken for an oversight: unlike
     * tiles and locs, spawns are NOT in the authoritative document. They live
     * in whichever connection placed them, and its Save writes them through
     * the server's file layer (spawn_save) — so they persist correctly, but
     * a second Client does not see them appear, and undo does not reach them.
     *
     * Closing it is a document-format change, not a wiring one: an
     * EDITOR_CMD_SPAWN kind, spawn storage on Editor_Square, a wider command
     * on the wire, and the server emitting the `.spawn` file for its dirty
     * squares the way it already emits `.jm2`/`.jl2`. Everything else about
     * the relay is in place to carry it.
     */
    struct Editor_SpawnEntry spawns[EDITOR_SPAWN_MAX];
    int spawn_count;
    int spawns_dirty;
    /** The selected spawn's index, when sel_kind is NPC/OBJ. */
    int sel_spawn;

    /** The chosen entry, or -1. This is what a place tool would stamp. */
    int cat_picked_id;
    char cat_picked_name[EDITOR_CATALOG_LABEL_MAX];
};

/** Construct the rows. Call once, after the overlay exists. */
/**
 * Re-place every panel at its default spot for the chrome's current scale.
 *
 * Called on a scale change: panel positions are pixel counts, and a 2x chrome
 * makes every panel twice as wide, so 1x-authored origins put them on top of
 * each other. @see Editor_PanelTick, which notices the change.
 */
void
Editor_PanelPlaceForScale(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui);

void
Editor_PanelInit(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui);

void
Editor_PanelSetVisible(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui,
    int visible);

/**
 * Per-frame: refresh the readout from the hovered tile, and act on whatever
 * the user activated since the last call.
 *
 * Takes the App because acting on a click needs the world (what tile is under
 * the cursor) and the editor session (what to do about it) at once.
 */
void
Editor_PanelTick(
    struct Editor_Panel* panel,
    struct App* app);

/**
 * Apply the current tool to a tile, as one undoable command.
 *
 * Separate from the tick so a brush drag can call it per tile inside one
 * stroke, and so the click path and any scripted path land in the same place.
 */
int
Editor_PanelApplyToolAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/**
 * Record a loc move or rotate in the DOCUMENT, so it survives a save.
 *
 * The loc editor already re-places the loc in the scene through
 * App_WorldLocChange, which is what makes the move visible. That is a
 * client-side scene edit and nothing more — it never touched the square's
 * `.jl2`, so the move was gone on the next world reload and could never be
 * saved. This is the other half: the same move as an undoable command against
 * the authored loc list.
 *
 * Coordinates are SCENE tiles, as the loc editor holds them; the square and
 * tile they belong to are resolved here.
 *
 * Returns 1 when a command was recorded. 0 means there is no editor session, or
 * the square the loc sits in is not open in the document — a loc can be visible
 * in the scene while its square was never loaded for editing.
 */
/** Latch the SELECT tool onto a ground tile -- either a plain click (the
 *  hovered tile) or the "Select terrain" minimenu row (the exact tile the
 *  row named). Clears any loc latch: one panel, one subject. */
void
Editor_PanelSelectTerrain(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** Latch the SELECT tool onto an exact loc, by scene element id -- the
 *  "Select wall/object/decor" minimenu rows' handler. Comes from the same
 *  pick/classify/dedup pipeline the minimenu itself uses, so it disambiguates
 *  a tile carrying a wall AND a decor AND a ground loc exactly the way the
 *  row that was clicked named it. No-op (selection unchanged) if the element
 *  is not a live scenery element. */
void
Editor_PanelSelectLoc(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id);

/** The level an edit means: the panel's pinned plane, or the pick's when the
 *  Level row is on "auto". Callers driving a click pass this rather than the
 *  hover level, or pinning a plane would do nothing. */
int
Editor_PanelEditLevel(
    struct Editor_Panel const* panel,
    struct App const* app);

/** The painter level mask the Vis row asks for: a bit per drawn plane, or 0
 *  for "no opinion, draw them all". The paint path ORs nothing onto this --
 *  a mask of 0 leaves the viewport's own mask alone. */
uint8_t
Editor_PanelVisLevelMask(
    struct Editor_Panel const* panel);

/** Stamp the catalog's picked loc at a scene tile: document command + the live
 *  scene change. 0 when nothing is picked, or the square is not open. */
int
Editor_PanelPlaceLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** Remove the first loc on a tile, whatever layer it is on. */
int
Editor_PanelDeleteLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** Delete every loc on a tile as one undo step. @return how many went. */
int
Editor_PanelClearLocsAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/** Apply the current tool to the SELECTION rather than to a click. This is the
 *  select-then-operate half: pick a subject once, then act on it as many times
 *  as you like without hunting for it again with the cursor.
 *  @return what the tool returned, or 0 when nothing is selected. */
int
Editor_PanelApplyToSelection(
    struct Editor_Panel* panel,
    struct App* app);

/** What the catalog's model-view well should be showing: the picked entry,
 *  except while a multiloc VARIANT row is chosen, where it is that rung's loc.
 *  Locs only -- npcs and objs have no transform rows -- and -1 for "nothing to
 *  draw", which covers both an empty pick and a deliberately blank -1 rung.
 *  The preview updater in app.c keys its cache on this, so choosing a rung
 *  re-renders the well the same way choosing a new pick does. */
int
Editor_PanelCatalogPreviewId(
    struct Editor_Panel const* panel);

/** The pose the Place tool would stamp right now: the LocSh/LocRot dropdowns
 *  with sane defaults. For the hover ghost, which must match the commit. */
void
Editor_PanelGhostSpec(
    struct Editor_Panel* panel,
    struct App* app,
    int* out_shape,
    int* out_angle);

/** Rewrite the selected loc's placement -- id, shape, rotation -- as ONE
 *  undoable command, with the scene updated to match. The workhorse behind
 *  Apply-to-selection on a loc and the Edit menu's swap-to-catalog-pick. */
int
Editor_PanelReplaceSelectedLoc(
    struct Editor_Panel* panel,
    struct App* app,
    int new_loc_id,
    int new_shape,
    int new_angle,
    int new_level);

/** Move the selected loc to a scene tile: one undoable command + the scene
 *  change. Refuses a cross-square move, as the underlying record does. */
int
Editor_PanelMoveSelectedLocTo(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z);

/** Record a session spawn placement (the scene add is the caller's; this is
 *  the document half). @return the entry index or -1 when full. */
int
Editor_PanelSpawnAdd(
    struct Editor_Panel* panel,
    struct App* app,
    int is_obj,
    int id,
    char const* name,
    int scene_x,
    int scene_z,
    int level,
    int count);

/** Emit + save every square's edited .spawn file via the host. Called from
 *  Save-all so spawns ride the same explicit save as tiles and locs. */
int
Editor_PanelSpawnsSave(
    struct Editor_Panel* panel,
    struct App* app);

/** Delete one session spawn by index: scene despawn + list removal. */
int
Editor_PanelDeleteSpawn(
    struct Editor_Panel* panel,
    struct App* app,
    int index);

/** Delete whatever is selected. @return 1 when something was deleted. */
int
Editor_PanelDeleteSelection(
    struct Editor_Panel* panel,
    struct App* app);

/** Delete one EXACT loc by scene element id -- the minimenu's "Delete Wall"
 *  rows' handler, layer-precise where the tile-based delete takes the first
 *  loc it finds. */
int
Editor_PanelDeleteLocByElement(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id);

/** Clear the SELECT tool's latch without touching the world. */
void
Editor_PanelClearSelection(
    struct Editor_Panel* panel,
    struct App* app);

/**
 * Apply a shared-state fact from this connection's Client — the receiving
 * half of the selection relay. The publishing half is the latch functions
 * above, which announce what they latched; this is what a fact from ANY of
 * the Client's connections (this one's own echo included — applying it is
 * idempotent) does to the panel. Unknown keys are ignored: the key space is
 * open, and a panel build that predates a key must not break the session.
 */
void
Editor_PanelApplySharedState(
    struct Editor_Panel* panel,
    struct App* app,
    uint32_t key,
    const int32_t* values,
    int count);

int
Editor_PanelRecordLocEdit(
    struct Editor_Panel* panel,
    struct App* app,
    int from_scene_x,
    int from_scene_z,
    int level,
    int loc_id,
    int shape,
    int from_angle,
    int to_scene_x,
    int to_scene_z,
    int to_angle);

#endif
