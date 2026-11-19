#ifndef SRC_APP_APP_MAPEDITOR_STATE_H
#define SRC_APP_APP_MAPEDITOR_STATE_H

/*
 * The map editor's working state as the App layer's editor units keep it:
 * what app_map_editor.c, app_editor_view.c, app_editor_hotkeys.c and
 * app_record_thumbs.c held as file statics before it had a home, grouped by
 * what each part is for. It needs App-layer types, so it is defined here and
 * hangs off struct MapEditorApp (editor/map_editor_app.h `state`) by a
 * forward-declared pointer.
 *
 * Made with the app (app_map_editor_app_open, ToriMem_Editor()) and freed
 * with it (app_map_editor_app_close), after app_map_editor_shutdown has
 * flushed the files and closed the listener. Every value a static started at
 * other than zero is set by app_mapeditor_state_new.
 *
 * Not here, and why (each stays a file static of its unit):
 *   - app_map_editor.c g_app_edit_mirror: the EDIT core is App-free on
 *     purpose; edit_world_test drives app_map_editor_edit_seed/_step with no
 *     App and no MapEditorApp.
 *   - app_editor_view.c g_app: the web page's "spawn view" callback has no
 *     context to find the App through.
 *   - editor/editor_view.c's boot pose, boot panel and exit request: set by
 *     main.c's argument parsing before any App exists, and read by main.c's
 *     loop (process launch state, not the running editor's).
 *
 * One header of the App layer: nothing outside src/app may include it.
 */

#include "app/app_internal.h"
#include "app/app_record_thumbs.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_view.h"
#include "editor/loc_editor_selection.h"
#include "editor/map_editor_ghost.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/task_editor_hotkeys_io.h"
#include "editor/task_editor_layout_io.h"
#include "editor/task_editor_prefs_io.h"
#include "editor/task_editor_recent_io.h"
#include "engine/uitree_scene_bridge.h"
#include "input/torirs_keymap.h"
#include "net/rev/packets/pkt_player_appearance.h"

#include <stdint.h>

struct RS_WorldMapOverview;
struct ToriRSMapEdListener;

/* ---- drags: the move gizmo, Ctrl+drag moves, the view's middle drag ---- */

struct MapEditorGizmoDragState
{
    int active;
    struct LocEditorGizmoDrag drag;
    /** The handle under the pointer (R5: hover tells), and the one the
     *  status bar last named; the quarters it last said for a ring drag. */
    int hot;
    int said;
    int said_quarters;
};

/* Ctrl (Command) + drag on a thing moves it (workbench 7.2): the press is
 * pending until the pointer leaves its tile. */
enum AppMoveKind
{
    APP_MOVE_NONE = 0,
    APP_MOVE_LOC,
    APP_MOVE_SPAWN,
    APP_MOVE_PLAYER,
};

struct MapEditorMoveDragState
{
    int pending;
    int active;
    enum AppMoveKind kind;
    /** The scene tile and level of the press; the footprint previewed. */
    int start_x;
    int start_z;
    int level;
    int size;
    /** A dragged spawn's kind and id: the body previewed at the tile under
     *  the pointer (MAP_EDITOR_SPAWN_PREVIEW_ID_* while unresolved: none). */
    enum MapEditorSpawnKind spawn_kind;
    int spawn_id;
};

struct MapEditorViewMiddleDrag
{
    int active;
    int press_x;
    int press_y;
    int last_x;
    int last_y;
};

struct MapEditorDrags
{
    struct MapEditorGizmoDragState gizmo;
    struct MapEditorMoveDragState move;
    struct MapEditorViewMiddleDrag view_mmb;
};

/* ---- the height tools ---------------------------------------------------- */

struct MapEditorHeightWheel
{
    int open;
    int64_t last_ms;
};

struct MapEditorHeightDrag
{
    int armed;
    int applied_before;
    int grid_at_ground;
    /* The ground at the press, in the brush's units (0 is the lowest a tile
     * goes): "lower only" on ground already at 0 moves nothing, and the
     * release must say that rather than blame the grid. */
    int ground;
};

struct MapEditorHeightTools
{
    struct MapEditorHeightWheel wheel;
    struct MapEditorHeightDrag drag;
    int scatter_drag_before;
};

/* ---- the layout and prefs files ------------------------------------------ */

struct MapEditorLayoutFile
{
    /* 0 not started, 1 reading, 2 loaded (saving on change), 3 off. */
    int state;
    const char* path;
    struct EditorLayoutLoad load;
    /* What was last queued for writing, and the last frame's digest. */
    int saved_valid;
    uint32_t saved;
    uint32_t last;
    int still_frames;
};

struct MapEditorPrefsFile
{
    /* 0 not started, 1 reading, 2 loaded (saving on change), 3 off. */
    int state;
    const char* path;
    struct EditorPrefsLoad load;
    uint32_t saved_serial;
};

struct MapEditorFiles
{
    struct MapEditorLayoutFile layout;
    struct MapEditorPrefsFile prefs;
};

/* ---- the panel wells' services: the picker's map, icons, floor swatches -- */

enum
{
    APP_ICON_ASKED_MAX = 1024,
    APP_EDITOR_TEXTURE_OVERLAY_MAX = 1025,
    APP_EDITOR_TEXTURE_SPRITE_MAX = 0x1000,
};

/*
 * Loads a well's icons and sprites asked for, each once: a request outlives
 * the frame that made it, and an icon is asked for every frame its cell is
 * drawn. A small open-addressed set per kind; when one fills it is cleared
 * (a few requests go out twice, nothing goes out never).
 */
struct MapEditorIconAsked
{
    int obj[APP_ICON_ASKED_MAX];
    int model[APP_ICON_ASKED_MAX];
    int sprite[APP_ICON_ASKED_MAX];
    int obj_count;
    int model_count;
    int sprite_count;
};

struct MapEditorWells
{
    /** The square picker's world map: created on first use, never freed
     *  (its bakes live in the scene). */
    struct RS_WorldMapOverview* worldmap;
    int worldmap_load_asked;
    /** The DOM well's picture versions, per picker slot (web). */
    uint32_t map_versions[UITREE_SCENE_EDITOR_PICKER_MAP_SLOTS];
    struct MapEditorIconAsked icon_asked;
    /** Floor swatches asked once each: a record or a texture the cache does
     *  not have would otherwise be asked for again every frame. */
    uint8_t asked_overlay[APP_EDITOR_TEXTURE_OVERLAY_MAX];
    uint8_t asked_texture[APP_EDITOR_TEXTURE_SPRITE_MAX];
};

/* ---- the EDIT world: floor rebakes and the Place-loc ghost --------------- */

struct MapEditorEditGhost
{
    int ghost_handle;
    int occupant_handle;
    /* The placement after the occupant in its square's map order: the
     * occupant goes back ahead of it, where the document lists it, not at
     * the end -- the shade replay follows map order. */
    int occupant_next;
    /* The bake the handles belong to: after a rebake they name nothing, and
     * the bake already showed the document without the ghost. */
    uint32_t bake_serial;
};

struct MapEditorEditWorld
{
    /** A floor record the editor defined since the last flush is one ground
     *  in the window may wear: the window is baked again. */
    int floor_rebake;
    struct MapEditorEditGhost ghost;
};

/* ---- the headless poses (TORIRS_EDITOR_HOVER, the level env) ------------ */

struct MapEditorHoverEnv
{
    int resolved;
    int on;
    int abs_x;
    int abs_z;
    int armed;
    char tool[32];
    char pick_kind[8];
    int pick_id;
    int logged;
};

struct MapEditorLevelEnv
{
    int resolved;
    int on;
    int level;
    int has_camera;
    struct EditorViewCamera camera;
    int level_done;
    int camera_done;
};

struct MapEditorEnvPoses
{
    struct MapEditorHoverEnv hover;
    struct MapEditorLevelEnv level;
    /** The level camera has framed its level once. */
    int level_camera_framed;
};

/* ---- spawn and player previews ------------------------------------------- */

struct MapEditorSpawnGhostLoads
{
    int npc_requested;
    int npc_resolved;
    int obj_requested;
    int model_requested;
};

/* The asset requests, one per id for the session. A request outlives the frame
 * and the table item that made it, so its out slot lives here, never moved. */
struct AppSpawnPreviewLoad
{
    int id;
    int is_obj;
    int npc_resolved;
    int model_requested;
};

struct MapEditorSpawnPreviewState
{
    struct MapEditorSpawnPreview preview;
    struct MapEditorSpawnPreviewItem removed[MAP_EDITOR_SPAWN_PREVIEW_MAX];
    struct AppSpawnPreviewLoad loads[MAP_EDITOR_SPAWN_PREVIEW_MAX];
    int load_count;
};

struct MapEditorPlayerGhost
{
    int entity;
    uint32_t load_seq;
    int level;
    int look_serial;
    int slots[APPEARANCE_SLOT_COUNT];
    int colours[APPEARANCE_COLOUR_COUNT];
    int gender;
    int idk_asked;
};

struct MapEditorPlayerPreview
{
    int entity;
    int element_id;
    uint32_t load_seq;
    int look_serial;
    int scene_x;
    int scene_z;
    int level;
    int selected;
};

struct MapEditorSpawns
{
    struct MapEditorSpawnGhostLoads ghost_loads;
    struct MapEditorSpawnPreviewState preview;
    struct MapEditorPlayerGhost player_ghost;
    struct MapEditorPlayerPreview player_preview;
};

/* ---- thumbnails: the Catalog's loc rows, the shape strip, record pictures - */

#define APP_EDITOR_THUMB_SLOTS 256

enum AppLocThumbState
{
    APP_THUMB_FREE = 0,
    /** Its config or a model was asked for. */
    APP_THUMB_LOADING,
    APP_THUMB_READY,
    /** No model at that shape: the empty cell, remembered. */
    APP_THUMB_NONE,
};

struct AppLocThumb
{
    int loc_id;
    int shape;
    enum AppLocThumbState state;
    /** The config / the models were asked for: once, not once a frame. */
    int config_asked;
    int models_asked;
    uint32_t used;
};

enum AppRecordThumbState
{
    THUMB_FREE = 0,
    /** Asked for; the update renders it. */
    THUMB_WANTED,
    THUMB_READY,
    /** The record has no model (a real answer). */
    THUMB_NONE,
};

struct AppRecordThumb
{
    enum AppRecordThumbState state;
    uint32_t kind;
    int id;
    int px;
    int scene_id;
    /** The ask that last touched it: the least recent is evicted. */
    uint32_t used;
    /** The wait the assets were asked for (-1 config, -2 models), once. */
    int asked_stage;
    int asked_frames;
};

/** The shape strip's latches (app_map_editor_shape_strip_update). */
struct MapEditorShapeStrip
{
    int last_loc;
    int last_count;
    int last_shapes[EDITOR_LOC_SHAPE_COUNT];
    int requested_loc;
    uint32_t version;
};

struct MapEditorThumbs
{
    struct AppLocThumb loc[APP_EDITOR_THUMB_SLOTS];
    uint32_t loc_clock;
    double loc_slice_ms;
    int loc_slice_count;
    struct MapEditorShapeStrip strip;
    /** app_record_thumbs.c's cache. */
    struct AppRecordThumb record[APP_RECORD_THUMBS];
    uint32_t record_clock;
};

/** The Preview well's latches (app_map_editor_preview_update). */
struct MapEditorPreviewWell
{
    int last_kind;
    int last_id;
    int last_w;
    int last_h;
    /** The subject the camera was framed for: a new one is framed afresh. */
    int subject_kind;
    int subject_id;
    /** The loc / npc config last asked for, so a config still loading is
     *  asked once, not once a frame. */
    int requested_kind;
    int requested_id;
    /** The subject whose missing models were asked for: once, not once a
     *  frame (seven npc parts a frame for the seconds a cold load takes
     *  piled thousands of duplicate loads into the queue). */
    int models_kind;
    int models_id;
};

/* ---- the view (app_editor_view.c) ---------------------------------------- */

struct MapEditorViewState
{
    /** The one view of this window. */
    struct EditorView view;
    int live;
    /** A pose waiting for its square to load before it can be applied. */
    int pending_apply;
    struct EditorViewCamera pending_camera;
    /** The embedded ToriRSMapEd's loopback door (server=embed): opened by
     *  the first pop-out, polled every frame, closed before the session.
     *  NULL until then, and always NULL in a process whose server is a
     *  daemon. */
    struct ToriRSMapEdListener* embed_listener;
    /** The App camera as the last apply left it: while it is still exactly
     *  that, the view's pose stands as adopted rather than being re-derived
     *  from the eye. */
    int applied_valid;
    struct ToriDraw_Position applied_eye;
    int applied_yaw;
    int applied_pitch;
    /** A popped-out panel that draws no world (EditorView_AppHoldsNoWorld). */
    int no_world;
    /** The Dock button closed this window: said once, then the loop ends. */
    int dock_exit_said;
    /** Frames the TORIRS_EDITOR_VIEW_DRIVE script has seen. */
    int drive_frame;
    /** Spawn requests sent to a SPAWN_VIEW server. */
    int spawn_serial;
};

/* ---- the hotkeys (app_editor_hotkeys.c) ---------------------------------- */

struct MapEditorHotkeysState
{
    /** This frame's resolution; `valid` while it belongs to this frame. */
    struct Editor_HotkeyFrame frame;
    int valid;
    /** The pointer when this frame resolved, for the "panel under the
     *  pointer" actions (pop out, close). */
    int mouse_x;
    int mouse_y;
    /** OSRS key codes of the keys that acted (input_claims' key index). */
    unsigned char consumed_osrs[TORIRS_OSRSKEY_COUNT];
    int consumed_any;

    /* editor_hotkeys.ini: 0 not started, 1 reading, 2 loaded, 3 off. */
    int io_state;
    const char* path;
    struct EditorHotkeysLoad load;
    uint32_t saved_serial;
    /** The table as the file left it: the shutdown flush writes only when a
     *  binding changed since. */
    uint32_t loaded_serial;

    /* editor_recent.ini (the recent squares and the camera scheme), the
     * same machine: 0 not started, 1 reading, 2 loaded, 3 off. */
    int recent_state;
    const char* recent_path;
    struct EditorRecentLoad recent_load;
    uint32_t recent_saved_serial;
    uint32_t recent_loaded_serial;
};

/* ---- the whole ------------------------------------------------------------ */

struct MapEditorState
{
    struct MapEditorDrags drag;
    struct MapEditorHeightTools height;
    struct MapEditorFiles files;
    struct MapEditorWells wells;
    struct MapEditorEditWorld edit;
    struct MapEditorEnvPoses env;
    struct MapEditorSpawns spawns;
    struct MapEditorThumbs thumbs;
    struct MapEditorPreviewWell preview_well;
    struct MapEditorViewState view;
    struct MapEditorHotkeysState hotkeys;
};

/** A state with every member at the value its static started at, in the
 *  EDITOR group. Never NULL. */
struct MapEditorState*
app_mapeditor_state_new(void);

/** Release a state from app_mapeditor_state_new. NULL is accepted. */
void
app_mapeditor_state_free(struct MapEditorState* state);

/** The editor's working state. Needs the map editor (asserts). */
struct MapEditorState*
app_mapeditor_state(struct App const* app);

#endif
