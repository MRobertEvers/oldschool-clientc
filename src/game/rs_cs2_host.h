#ifndef RS_CS2_HOST_H
#define RS_CS2_HOST_H

#include "cs2vm2/cs2vm2_host.h"
#include "input/torirs_keymap.h"

#include <stdbool.h>
#include <stdint.h>

struct UITree;
struct CacheProvider;
struct InvManager;
struct VarPManager;
struct VarCManager;
struct CS2VM2_Thread;
struct UITreeSceneBridge;
struct RS_WorldMapState;

#define RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_TRANSMIT_TRIGGER_MAX 32
/** Var ids remembered per tick for transmit-hook matching; past this the tick
 *  degrades to "every hook re-runs" (correct, just not selective). */
#define RS_CS2_HOST_VAR_CHANGED_MAX 64
/* Must match CS2VM_HostRequest_IF_SetOnInvTransmit.int_args[32]. */
#define RS_CS2_HOST_TRANSMIT_INT_ARG_MAX 32

struct RS_CS2InvTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    /** String args by arg position (see CS2VM_HostRequest str_arg_mask docs).
     *  Replayed into the hook script's string locals on dispatch. */
    uint32_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
    /** inv_change_serial this hook last fired for (0 = never fired). Hooks fire
     *  once when first dispatched visible, then only when the serial advances
     *  (TS parity: node.lastChangedInvCount vs cycles.changedInvCount). */
    uint32_t last_seen_serial;
};

struct RS_CS2VarTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    /** String args by arg position (see CS2VM_HostRequest str_arg_mask docs). */
    uint32_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
    /** var_change_serial this hook last fired for (0 = never fired). */
    uint32_t last_seen_serial;
};

struct RS_CS2Host
{
    struct UITree* tree;
    struct CacheProvider* provider;
    struct InvManager* invs;
    struct VarPManager* varps;        /* may be NULL */
    struct UITreeSceneBridge* bridge; /* may be NULL until set */

    bool has_pending;
    struct CS2VM_HostRequest pending;

    /* What the last yield parked for a cache load. A handler whose resource is
     * still missing after this exact wait must complete with a default instead
     * of yielding again: one opcode, one yield. Cleared in RS_CS2Host_Exec on
     * any non-yield return. */
    bool has_awaited;
    enum CS2VM_HostRequestKind awaited_kind;
    int awaited_id;
    int awaited_id2; /* second resource of a two-resource request, else -1 */

    struct VarCManager* varcs; /* client-variable store; may be NULL */

    int client_clock;
    int viewport_w;
    int viewport_h;
    /** Follow-camera trailing height, backing CAM_SET/GETFOLLOWHEIGHT. The
     *  orbit-camera render path in app.c does not consume this yet; it is stored
     *  so a script that sets it can read the same value back. */
    int cam_follow_height;
    /** IF_GETTOP / client type (default 80). */
    int client_type;
    /** Audio volumes, backing the SET/GETVOLUME* opcodes (3203..3208). The port
     *  has no audio mixer yet; the host owns the values so a settings panel that
     *  sets one reads the same value back (round-trip, like cam_follow_height). */
    int volume_music;
    int volume_sounds;
    int volume_area_sounds;
    /** Minimap zoom (2..8), backing MINIMAP_SETZOOM / GETZOOM. Host-owned so a
     *  script setting it reads the same value back; the port has no minimap-zoom
     *  render path consuming it yet. */
    int minimap_zoom;

    /** Set by LOGOUT (5630); nothing consumes it yet — the client has no logout
     *  flow wired up, so this just records that a script asked for one. */
    bool logout_requested;

    /** Viewport FOV/zoom, backing VIEWPORT_SETFOV/SETZOOM/CLAMPFOV/GETFOV/GETZOOM.
     *  Host-owned so SET/CLAMP round-trips through the matching GET; defaults
     *  match the values these getters returned before they were host-routed. */
    int viewport_fov;
    int viewport_fov_max;
    int viewport_zoom;
    int viewport_zoom_max;

    /** UI zoom, backing UIZOOM_SET/GET/RESET (GETDEFAULT is a fixed constant,
     *  not read from here). Host-owned so it round-trips like the other
     *  settings values above. */
    int ui_zoom;

    /** Backing CAM_GETYAW. There is no setter opcode and no live link yet from
     *  this host to the render-side camera (app->world_camera.yaw, reached via
     *  the separate UITree host bus RS_CS2Host cannot see) — 0 (facing north)
     *  until something wires the real value in. */
    int cam_yaw;

    struct RS_CS2InvTransmitHook inv_transmit_hooks[RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX];
    int inv_transmit_hook_count;

    struct RS_CS2VarTransmitHook var_transmit_hooks[RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX];
    int var_transmit_hook_count;

    /** Set when IF_SETHIDE unhides a subtree (TS markWidgetsLoaded). Consumed once
     *  per logic tick by RS_CS2_PumpTransmits; per-hook last_seen_serial gating
     *  keeps already-fired hooks from re-running. */
    int widgets_loaded_dirty;
    /** Set when a varp/varc value actually changed this tick (via
     *  RS_CS2Host_NotifyVarChanged, wired to the var managers' change callbacks).
     *  RS_CS2_PumpTransmits consumes it once per tick to re-dispatch var-transmit
     *  hooks, so interfaces react to value changes and not only to unhide. */
    int var_transmit_dirty;
    /** Which var ids changed since the last dispatch (TS changedVarps parity).
     *  A var-transmit hook re-runs only when the change touches one of its
     *  triggers: rev230's gameframe writes a clock varc every tick, and a
     *  dispatch that ignored the id re-ran every hook — rebuilding whole widget
     *  lists (cc_deleteall + cc_create) 50 times a second for nothing.
     *  var_changed_all means "assume every hook" (overflow, or an unhide, where
     *  the trigger set says nothing about what must be re-run). */
    int var_changed_ids[RS_CS2_HOST_VAR_CHANGED_MAX];
    int var_changed_count;
    int var_changed_all;
    /** Bumped when a var/inv value actually changes. Start at 1 so freshly
     *  registered hooks (last_seen_serial=0) fire once on first dispatch; the
     *  bump lets already-fired hooks re-run when a value changes. */
    uint32_t var_change_serial;
    uint32_t inv_change_serial;

    /** Live CS2 event locals for script arg substitution (drag / mouse). */
    int event_mouse_x;
    int event_mouse_y;
    int event_drag_target_id;
    int event_drag_target_child_index;

    /**
     * Live onKey event locals. The naming follows the reference and is INVERTED
     * relative to canonical OSRS clientscript naming: event_key_typed carries
     * the OSRS internal KEY CODE (-1 when the event is a typed character), and
     * event_key_pressed carries the CHARACTER code (0 when it is a key code).
     * See struct LibToriRS_KeyEvent -- both ends must stay inverted together.
     */
    int event_key_typed;
    int event_key_pressed;

    /** Op index (1..10) for the hook being dispatched; 1 is the primary
     *  left-click op, which is what every mouse-driven dispatch uses. */
    int event_op_index;
    int event_op_subindex;

    /** Per-frame key state snapshot, indexed by OSRS internal code, backing the
     *  KEYHELD and KEYPRESSED opcodes. Refreshed by RS_CS2_SyncKeyState. */
    unsigned char osrs_key_held[TORIRS_OSRSKEY_COUNT];
    unsigned char osrs_key_pressed[TORIRS_OSRSKEY_COUNT];

    /** World map view state, backing the WORLDMAP_* opcodes. Owned here. */
    struct RS_WorldMapState* worldmap;

    /** Item-name search state, backing OC_FIND/OC_FINDNEXT/OC_FINDRESET.
     *  `item_search_results` is a malloc'd, ascending-sorted array of the
     *  `item_search_count` matched obj ids (capacity `item_search_cap`);
     *  `item_search_index` is the FINDNEXT cursor. Rebuilt by each OC_FIND,
     *  walked by OC_FINDNEXT, cleared by OC_FINDRESET. Freed in RS_CS2Host_Free. */
    int* item_search_results;
    int item_search_count;
    int item_search_cap;
    int item_search_index;

    /** Active DB find-iterator (DB_FIND* build it, DB_FINDNEXT walks it).
     *  `db_find_rows` is a malloc'd copy of the matched row ids (count
     *  `db_find_count`); `db_find_cursor` is the FINDNEXT cursor. Freed in
     *  RS_CS2Host_Free. */
    int* db_find_rows;
    int db_find_count;
    int db_find_cursor;
};

void
RS_CS2Host_Init(
    struct RS_CS2Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps,
    struct VarCManager* varcs);

/** Signal that a varp/varc value changed: bumps var_change_serial and flags a
 *  var-transmit re-dispatch for the tick. Wired to the var managers' change
 *  callbacks; safe to call with any/no var id. */
void
RS_CS2Host_NotifyVarChanged(
    struct RS_CS2Host* host,
    int var_id);

void
RS_CS2Host_SetBridge(
    struct RS_CS2Host* host,
    struct UITreeSceneBridge* bridge);

/** Advance CLIENTCLOCK once per game tick. */
void
RS_CS2Host_Tick(struct RS_CS2Host* host);

/** Releases what the host owns (the world map state); the host itself is the
 *  caller's storage. */
void
RS_CS2Host_Free(struct RS_CS2Host* host);

/**
 * CS2VM2 host_exec callback. Expects CS2VM_USER(thread) == RS_CS2Host*.
 * Never reads disk: missing clientscript / component / sprite / font / enum /
 * struct / obj / model stages into host->pending and returns YIELD.
 */
int
RS_CS2Host_Exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request);

#endif /* RS_CS2_HOST_H */
