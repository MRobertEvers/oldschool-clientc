#ifndef SRC_EDITOR_IFACE_IFACE_PANELS_H
#define SRC_EDITOR_IFACE_IFACE_PANELS_H

/**
 * The interface editor's and CS2 tools' panels on ToriRSChrome
 * (docs/MAPED_DESIGN.html #cs2), drawing iface_model.h and iface_cs2.h.
 *
 *   Interface   open by name; mount as root / modal / overlay / tab into a
 *               slot; pick-in-view; the component TREE; structure ops (add
 *               child, delete subtree, move up/down, reparent).
 *   Inspector   the selected component's fields as the component struct
 *               groups them, one tab per group; Enter on a field submits an
 *               IF_FIELD; a key/value row adds or removes any field.
 *   Hooks       the CS2 script catalog (search, typed arguments from the
 *               script header), building a hook value for a hook key.
 *   CS2         Run (script + args against the mounted interface, optional
 *               trace, harness case), Trace, Watch (varp/varbit/varc with a
 *               set control), Code (a `.cs2` in a CODE widget, Compile with
 *               every decline listed, Save). No session hot-swap of the
 *               compiled script yet: the change reaches the client on RUN.
 *
 * Nothing here reaches the App. What only the App can do — mount, run a
 * script, write a var, arm picking — is queued as an EditorIfaceRequest for
 * app_iface_editor.c to take; edits go straight to the Editor (Editor_Apply),
 * so every mirror sees them.
 *
 * Input: the host drains the chrome's activations and events and offers each
 * to EditorIfacePanels_OnActivate / _OnEvent, which return 1 when the widget
 * was one of these panels'.
 */

#include "editor/iface/iface_cs2.h"
#include "editor/iface/iface_model.h"

#include <stdint.h>

struct Editor;
struct EditorUi;
struct ToriRSChrome;
struct ToriRSChromeEvent;

enum EditorIfaceRequestKind
{
    EDITOR_IFACE_REQUEST_NONE = 0,
    /** App_OpenRootInterface(interface_id). */
    EDITOR_IFACE_REQUEST_MOUNT_ROOT,
    /** App_OpenSubInterface(target_uid, interface_id, sub_type). */
    EDITOR_IFACE_REQUEST_MOUNT_SUB,
    /** Run `script_id` with `args` against the mounted interface; the active
     *  component is `component_id` (-1 for none). */
    EDITOR_IFACE_REQUEST_RUN,
    /** Write a watched var: `var_kind` (EditorCs2WatchKind), `var_id`,
     *  `value` / `text`. */
    EDITOR_IFACE_REQUEST_VAR_SET,
    /** Arm (`value` 1) or disarm picking a node in the view. */
    EDITOR_IFACE_REQUEST_PICK,
};

#define EDITOR_IFACE_RUN_ARGS_MAX 32

struct EditorIfaceRequest
{
    int kind;
    int interface_id;
    int target_uid;
    int sub_type;
    int script_id;
    int component_id;
    int trace;
    int args[EDITOR_IFACE_RUN_ARGS_MAX];
    int arg_count;
    /** Bit i: position i is a string, the k-th set bit's text in strings[k]. */
    uint64_t string_mask;
    char strings[4][EDITOR_CMD_VALUE_MAX];
    int string_count;
    int var_kind;
    int var_id;
    int value;
    char text[1024];
};

#define EDITOR_IFACE_REQUEST_QUEUE 16
#define EDITOR_IFACE_TREE_ROWS_MAX 400
#define EDITOR_IFACE_FIELD_ROWS_MAX 96
#define EDITOR_IFACE_ARG_ROWS_MAX 12

struct EditorIfacePanels
{
    int built;
    /** Paths the CS2 tools need (copied at Configure). */
    char content_dir[1024];
    char cache_dir[1024];
    char cs2_bin[1024];
    char names_dir[1024];
    char work_dir[1024];

    struct EditorIfaceModel model;
    char iface_name[EDITOR_IFACE_NAME_MAX];

    /* Interface panel. */
    int panel_iface;
    int in_open;
    int bt_open;
    int dd_mount;
    int in_slot;
    int bt_mount;
    int cb_pick;
    int bt_add;
    int bt_delete;
    int bt_up;
    int bt_down;
    int in_reparent;
    int bt_reparent;
    int lb_status;

    /* Component tree (its own panel: rebuilt on every model change). */
    int panel_tree;
    int tree_rows[EDITOR_IFACE_TREE_ROWS_MAX];
    int tree_row_position[EDITOR_IFACE_TREE_ROWS_MAX];
    int tree_row_count;
    uint32_t tree_generation;
    int tree_selected_shown;

    /* Inspector. */
    int panel_inspector;
    int inspector_tabs;
    int inspector_tab;
    int field_rows[EDITOR_IFACE_FIELD_ROWS_MAX];
    struct EditorIfaceField fields[EDITOR_IFACE_FIELD_ROWS_MAX];
    int field_count;
    int in_field_key;
    int in_field_value;
    int bt_field_set;
    int bt_field_remove;
    uint32_t inspector_generation;
    int inspector_selected;

    /* Hooks. */
    int panel_hooks;
    struct EditorCs2Catalog catalog;
    int catalog_loaded;
    int in_hook_search;
    int vl_scripts;
    int search_hits[4096];
    int search_count;
    char search_shown[64];
    int picked_script;
    int dd_hook_key;
    int arg_rows[EDITOR_IFACE_ARG_ROWS_MAX];
    int bt_hook_set;
    int lb_hook;

    /* CS2. */
    int panel_cs2;
    int cs2_tabs;
    int in_run_script;
    int in_run_args;
    int cb_run_trace;
    int bt_run;
    int bt_harness;
    int lb_run;
    int vl_trace;
    struct EditorCs2Trace trace;
    int in_watch;
    int bt_watch_add;
    int vl_watch;
    int in_watch_value;
    int bt_watch_set;
    int bt_watch_remove;
    struct EditorCs2Watch watch;
    int in_code_name;
    int bt_code_open;
    int code;
    int bt_compile;
    int bt_code_save;
    int vl_output;
    struct EditorCs2Code code_model;
    char code_key[EDITOR_DOC_KEY_MAX];
    struct EditorCs2CompileResult* compile;

    struct EditorIfaceRequest requests[EDITOR_IFACE_REQUEST_QUEUE];
    int request_count;

    /** Whether the companion panels are showing (they follow the Interface
     *  panel, which the View menu toggles). */
    int shown;
    /** The App half's state (app/app_iface_editor.c owns and frees it). */
    void* app_state;
};

/** Zero the state and set every handle to -1. No chrome is touched. */
void
EditorIfacePanels_Init(struct EditorIfacePanels* panels);

/** Releases the models and buffers (never the chrome). NULL-tolerant. */
void
EditorIfacePanels_Free(struct EditorIfacePanels* panels);

/**
 * Where the CS2 tools find things: the content tree (scripts/ for the
 * catalog), the cache (--cache), the compiler, RuneStar's names (may be ""),
 * and a scratch directory compiles and harness cases write under.
 */
void
EditorIfacePanels_Configure(
    struct EditorIfacePanels* panels,
    const char* content_dir,
    const char* cache_dir,
    const char* cs2_bin,
    const char* names_dir,
    const char* work_dir);

/** Create the four panels (hidden) on `ui`. */
void
EditorIfacePanels_Build(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui);

/** Show or hide the interface editor's panels / the CS2 panel. */
void
EditorIfacePanels_ShowInterface(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int visible);

void
EditorIfacePanels_ShowCs2(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int visible);

/** Open `if:<name>` through the editor (DOC_OPEN) and point the panels at it.
 *  Returns 1 when the document is open. */
int
EditorIfacePanels_OpenInterface(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    const char* name);

/** Per frame: follow the document (another mirror's edit), refresh rows. */
void
EditorIfacePanels_Tick(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor);

/** A traced run finished: show the captured instructions. */
void
EditorIfacePanels_TraceDone(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    const char* summary);

/** Select the node for full component id `component_id` (a pick). */
void
EditorIfacePanels_SelectComponent(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    int component_id);

/** One drained activation; 1 when it was one of these panels' widgets. */
int
EditorIfacePanels_OnActivate(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    int widget);

/** One drained chrome event (CODE edits); 1 when it was ours. */
int
EditorIfacePanels_OnEvent(
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor,
    const struct ToriRSChromeEvent* event);

/** The next request for the App, or 0 when none is queued. */
int
EditorIfacePanels_TakeRequest(
    struct EditorIfacePanels* panels,
    struct EditorIfaceRequest* out);

/** The IFACE document's current text (NULL when none is open). */
const char*
EditorIfacePanels_Text(
    const struct EditorIfacePanels* panels,
    struct Editor* editor,
    size_t* out_length);

/* ---- registry entry points (editor/editor_ui.h; iface_register.c) ------ */

/**
 * Register the interface editor ("iface": the Interface, Components,
 * Inspector and Hooks panels) and the CS2 tools ("cs2") with the editor's
 * panel registry. Both share one EditorIfacePanels (a run's active component
 * is the tree's selection), made by whichever registers first. The panels
 * start hidden; the View menu opens them. With `ui->app` set, each frame
 * also runs the App half (app/app_iface_editor.h): mounts, runs, watch
 * values, refill.
 */
void
Editor_Iface_Register(struct EditorUi* ui);

void
Editor_Cs2_Register(struct EditorUi* ui);

/** The shared panels behind a registered "iface" or "cs2" module's self. */
struct EditorIfacePanels*
Editor_Iface_PanelsOf(void* self);

#endif
