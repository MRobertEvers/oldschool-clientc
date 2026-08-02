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
struct RS_PlayerStats;
struct CS2VM2_Thread;
struct UITreeSceneBridge;
struct RS_WorldMapState;
struct RS_Social;

/*
 * Outbound social requests a CS2 script made.
 *
 * The friends panel's buttons are pure CS2: "Add Friend" runs the cache's own
 * onop, which opens a name prompt, and Enter runs clientscript 681, which calls
 * friend_add / ignore_del / chat_setfilter / chat_sendprivate. Each of those
 * has to reach the server, and this host has no network pointer — so the
 * request is parked here and the App drains it on the next tick, which is the
 * shape `logout_requested` and `close_modal_requested` already use. Unlike
 * those two these carry arguments, and more than one can arrive in a tick
 * (script 681 sends a CHAT_SETMODE and a private message in a single run), so
 * it is a small queue rather than a flag.
 */
enum RS_CS2SocialSendKind
{
    RS_CS2_SOCIAL_SEND_NONE = 0,
    RS_CS2_SOCIAL_SEND_FRIEND_ADD,
    RS_CS2_SOCIAL_SEND_FRIEND_DEL,
    RS_CS2_SOCIAL_SEND_IGNORE_ADD,
    RS_CS2_SOCIAL_SEND_IGNORE_DEL,
    RS_CS2_SOCIAL_SEND_CHAT_SETMODE,
    RS_CS2_SOCIAL_SEND_MESSAGE_PRIVATE,
    RS_CS2_SOCIAL_SEND_CHEAT,
};

#define RS_CS2_HOST_SOCIAL_SEND_MAX 8
#define RS_CS2_HOST_SOCIAL_NAME_LEN 32
#define RS_CS2_HOST_SOCIAL_TEXT_LEN 200

/* Pending IF_CALLONRESIZE requests. 16 is well past what this cache asks for —
 * the seventeen call sites are all one-per-script and the longest chain a
 * listener starts is two deep — and the queue reports an overflow rather than
 * dropping quietly, because a panel that never ran its own builder is a blank
 * panel with no other symptom. */
#define RS_CS2_HOST_CALL_ON_RESIZE_MAX 16

struct RS_CS2SocialSend
{
    int kind; /* enum RS_CS2SocialSendKind */
    /** Target player: the four list ops and MESSAGE_PRIVATE. */
    char name[RS_CS2_HOST_SOCIAL_NAME_LEN];
    /** Message body: MESSAGE_PRIVATE and CHEAT. */
    char text[RS_CS2_HOST_SOCIAL_TEXT_LEN];
    /** public / private / trade: CHAT_SETMODE only. */
    int modes[3];
};

/*
 * How many components may register a transmit hook at once.
 *
 * 128 was not enough, and the way it failed is the reason these are 512 now.
 * The rev-230 gameframe alone registers **131** distinct var-transmit hooks
 * before any panel is open (measured, not estimated). So the *next* panel to
 * mount got NULL out of `rs_cs2_acquire_var_transmit_hook` and its
 * `if_setonvartransmit` was dropped on the floor: the Tool Leprechaun's store
 * drew perfectly, its counts were right at mount, and then nothing on it ever
 * updated again — the panel showed 0/100 for a rake the player had just
 * deposited, while the sidebar beside it (whose hooks are *inv* transmits, a
 * different table with room left) updated correctly.
 *
 * That is the exact failure class docs/REV230_UI_BLANK_PANELS.md §0 is about:
 * the client silently dropped part of a script's data and let the script carry
 * on. Overflow now says so once per table as well (see the acquire functions),
 * because a cap that is reached quietly is a cap that gets diagnosed as a
 * missing packet.
 */
#define RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX 512
#define RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX 512
#define RS_CS2_HOST_TRANSMIT_TRIGGER_MAX 32
/** Var ids remembered per tick for transmit-hook matching; past this the tick
 *  degrades to "every hook re-runs" (correct, just not selective). */
#define RS_CS2_HOST_VAR_CHANGED_MAX 64
/* Must match CS2VM_HostRequest_IF_SetOnInvTransmit.int_args — the copy below is
 * a sizeof(dest) memcpy, so the two cannot differ. */
#define RS_CS2_HOST_TRANSMIT_INT_ARG_MAX CS2VM_SETON_INT_ARG_MAX

struct RS_CS2InvTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    /** String args by arg position (see CS2VM_HostRequest str_arg_mask docs).
     *  Replayed into the hook script's string locals on dispatch. */
    uint64_t str_arg_mask;
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
    uint64_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
    /** var_change_serial this hook last fired for (0 = never fired). */
    uint32_t last_seen_serial;
};

/*
 * Stat-transmit hooks are the third channel of the same reactive loop as inv
 * and var, and the struct is deliberately the same shape as the other two: a
 * component, a script, its captured args, the stat ids that trigger it, and the
 * serial it last fired for.
 */
struct RS_CS2StatTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    uint64_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
    /** stat_change_serial this hook last fired for (0 = never fired). */
    uint32_t last_seen_serial;
};

struct RS_CS2Host
{
    struct UITree* tree;
    struct CacheProvider* provider;
    struct InvManager* invs;
    struct VarPManager* varps;        /* may be NULL */
    /** Skill levels and xp, for the STAT / STAT_BASE / STAT_XP opcodes. May be
     *  NULL, in which case those read 0 — which is what the skills tab used to
     *  show unconditionally. */
    struct RS_PlayerStats* stats;
    /** The client's friend / ignore store, backing the FRIEND_* / IGNORE_*
     *  opcodes. May be NULL, in which case friend_count answers 0 and the
     *  panels draw their empty state — which is exactly what they did before
     *  those opcodes had handlers at all. */
    struct RS_Social* social;
    /** The three chat filter modes, borrowed from RS_UISlots — the same three
     *  ints the IF1 privacy bar cycles. A pointer rather than a copy because
     *  CHAT_SETFILTER and the privacy bar are two writers of one value and the
     *  second copy is the bug this avoids. NULL leaves the getters at 0. */
    int* chat_filter_mode;
    /** This client's world id, backing MAP_WORLD. Mirrors RS_Social.node_id.  */
    int map_world;
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
    /** Group id of the open gameframe root, backing IF_GETTOP (-1 = none open).
     *  Scripts branch the whole chrome on this: toplevel_kind maps it to a
     *  layer-lookup key (161 fixed / 164,165 resizable / 548 legacy), and the
     *  tooltip + notification layers are enum-resolved from that key. A wrong
     *  value silently builds those overlays under a component of an interface
     *  that was never opened, so nothing draws. */
    int top_interface_id;
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

    /** Set by IF_CLOSE (3103) — an interface's close button. Drained by the
     *  App's tick, which sends CLOSE_MODAL; the server is what actually
     *  unmounts, so nothing here touches the tree. */
    bool close_modal_requested;

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

    /** The orbit camera as scripts see it, backing CAM_GETANGLE_XA/YA (5505/5506)
     *  and written by CAM_FORCEANGLE (5504). Units are the reference's
     *  orbitCameraPitch / orbitCameraYaw: pitch 128..383, yaw 0..2047 — not the
     *  renderer's. The app mirrors its live orbit camera in here once per logic
     *  tick (RS_CS2Host_SetCameraAngles), so a getter reads the real camera even
     *  though this host has no pointer to it. */
    int cam_angle_x;
    int cam_angle_y;
    /** Raised by CAM_FORCEANGLE and cleared by RS_CS2Host_TakeCameraForce. The
     *  app consumes it on the way back so a script snap survives the mirror
     *  write that would otherwise overwrite it on the next tick. */
    bool cam_angle_forced;

    struct RS_CS2InvTransmitHook inv_transmit_hooks[RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX];
    int inv_transmit_hook_count;

    struct RS_CS2VarTransmitHook var_transmit_hooks[RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX];
    int var_transmit_hook_count;

    struct RS_CS2StatTransmitHook stat_transmit_hooks[RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX];
    int stat_transmit_hook_count;

    /** Set when IF_SETHIDE unhides a subtree (TS markWidgetsLoaded). Consumed once
     *  per logic tick by RS_CS2_PumpTransmits; per-hook last_seen_serial gating
     *  keeps already-fired hooks from re-running. */
    int widgets_loaded_dirty;
    /** Set when a varp/varc value actually changed this tick (via
     *  RS_CS2Host_NotifyVarChanged, wired to the var managers' change callbacks).
     *  RS_CS2_PumpTransmits consumes it once per tick to re-dispatch var-transmit
     *  hooks, so interfaces react to value changes and not only to unhide. */
    int var_transmit_dirty;
    /** Set when an inventory container's contents actually changed this tick
     *  (via RS_CS2Host_NotifyInvChanged, wired to the UPDATE_INV_* handlers and
     *  to local slot swaps). Consumed once per tick by RS_CS2_PumpTransmits. */
    int inv_transmit_dirty;
    /** Set when a skill's level or experience changed this tick (via
     *  RS_CS2Host_NotifyStatChanged, wired to UPDATE_STAT). Without it the
     *  skills tab paints once at build time — against the zeroes it starts
     *  with — and nothing ever asks it to paint again. */
    int stat_transmit_dirty;
    int stat_changed_ids[RS_CS2_HOST_VAR_CHANGED_MAX];
    int stat_changed_count;
    int stat_changed_all;
    /** Set when a "misc" transmit value changed — run energy and run weight at
     *  this revision. There is no id set beside it, unlike the var/inv/stat
     *  registries: the misc hooks take no trigger arguments, so every
     *  registered hook re-runs and a single flag is the whole state.
     *  Wired to UPDATE_RUNENERGY / UPDATE_RUNWEIGHT via
     *  RS_CS2Host_NotifyMiscChanged; without it the run orb showed its
     *  build-time value until some unrelated interface event repainted it. */
    int misc_transmit_dirty;
    /** Set when the friend/ignore store changed — a friend's world, a list
     *  entry, the friend-server status or the chat filter modes. No id set
     *  beside it for the same reason as misc: IF_SETONFRIENDTRANSMIT carries no
     *  trigger list, so every registered hook re-runs. Wired to the four social
     *  packets via RS_CS2Host_NotifyFriendChanged. Without it the friends panel
     *  paints once at mount and never again. */
    int friend_transmit_dirty;
    /** Which container ids changed since the last dispatch, mirroring
     *  var_changed_ids. `inv_changed_all` means "re-run every inv hook". */
    int inv_changed_ids[RS_CS2_HOST_VAR_CHANGED_MAX];
    int inv_changed_count;
    int inv_changed_all;
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
    uint32_t stat_change_serial;

    /** Live pointer position in canvas coords, backing MOUSE_GETX / MOUSE_GETY
     *  (-1,-1 when the pointer is off the canvas, as the reference reports).
     *  Refreshed by RS_CS2_SyncMouseState. Unlike event_mouse_x/y below this is
     *  the *current* pointer, not the position latched for a hook dispatch:
     *  the tooltip/notification layers place themselves at it every frame. */
    int mouse_x;
    int mouse_y;

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

    /** Outbound social requests a CS2 script queued this tick; drained by
     *  RS_CS2Host_TakeSocialSend. Overflow drops the newest and says so once —
     *  silently losing a friend add would look exactly like a server bug. */
    struct RS_CS2SocialSend social_send[RS_CS2_HOST_SOCIAL_SEND_MAX];
    int social_send_count;
    int social_send_head;

    /** Components whose on-resize listener IF_CALLONRESIZE asked to run,
     *  drained by the App's tick through RS_CS2Host_TakeCallOnResize.
     *
     *  Queued rather than run in place because the request is handled from
     *  inside a running CS2 script and this host has no task runner to nest a
     *  second one on — the same arrangement `close_modal_requested` and
     *  `social_send` use. In this cache the call is the *last* statement of
     *  every site that makes it (script1911's window setup, script1906's tab
     *  click), so deferring reorders nothing observable; a site that needed the
     *  listener to have finished before the next statement would need a real
     *  nested run, and would be a finding rather than a tweak.
     *
     *  A listener may queue another, so the drain loops. */
    int call_on_resize[RS_CS2_HOST_CALL_ON_RESIZE_MAX];
    int call_on_resize_count;
    int call_on_resize_head;
};

void
RS_CS2Host_Init(
    struct RS_CS2Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps,
    struct VarCManager* varcs);

/** Give the host the player's skill table. Separate from Init because the
 *  stats outlive a host re-init and Init has enough parameters already. */
void
RS_CS2Host_SetStats(
    struct RS_CS2Host* host,
    struct RS_PlayerStats* stats);

/** Signal that a skill changed: bumps stat_change_serial and flags a
 *  stat-transmit re-dispatch. The stat half of the same reactive loop
 *  NotifyVarChanged and NotifyInvChanged drive. Pass -1 for "all". */
void
RS_CS2Host_NotifyStatChanged(
    struct RS_CS2Host* host,
    int stat_id);

/** Signal that a "misc" transmit value changed — run energy or run weight —
 *  and flag a misc-transmit re-dispatch for the tick. No id: the misc hooks
 *  carry no trigger set, so every registered hook re-runs. Call it only when
 *  the value ACTUALLY changed; the walk touches every component. */
void
RS_CS2Host_NotifyMiscChanged(struct RS_CS2Host* host);

/** Give the host the client's friend/ignore store, the three chat filter modes
 *  it shares with the privacy bar, and this client's world id. Separate from
 *  Init for the same reason SetStats is: all three outlive a host re-init.
 *  `filter_modes` points at RS_UISlots.chat_filter_mode — three ints indexed by
 *  RS_UI_CHAT_FILTER_{PUBLIC,PRIVATE,TRADE}. */
void
RS_CS2Host_SetSocial(
    struct RS_CS2Host* host,
    struct RS_Social* social,
    int* filter_modes,
    int world);

/** Signal that the friend/ignore store changed and flag a friend-transmit
 *  re-dispatch for the tick. No id: IF_SETONFRIENDTRANSMIT carries no trigger
 *  set, so every registered hook re-runs. Wired to UPDATE_FRIENDLIST,
 *  UPDATE_IGNORELIST, FRIENDLIST_LOADED and CHAT_FILTER_SETTINGS. */
void
RS_CS2Host_NotifyFriendChanged(struct RS_CS2Host* host);

/** Pop the oldest queued outbound social request, FIFO. Returns false when the
 *  queue is empty. The App drains this once per tick and turns each entry into
 *  a packet; nothing else may consume it. */
bool
RS_CS2Host_TakeSocialSend(
    struct RS_CS2Host* host,
    struct RS_CS2SocialSend* out);

/** Pop the oldest component id queued by IF_CALLONRESIZE, FIFO. Returns false
 *  when the queue is empty. The App drains this once per tick and runs each
 *  component's on-resize listener; nothing else may consume it. */
bool
RS_CS2Host_TakeCallOnResize(
    struct RS_CS2Host* host,
    int* out_component_id);

/** Signal that a varp/varc value changed: bumps var_change_serial and flags a
 *  var-transmit re-dispatch for the tick. Wired to the var managers' change
 *  callbacks; safe to call with any/no var id. */
void
RS_CS2Host_NotifyVarChanged(
    struct RS_CS2Host* host,
    int var_id);

/** Signal that an inventory container changed: bumps inv_change_serial and
 *  flags an inv-transmit re-dispatch for the tick. This is the container half
 *  of the same reactive loop NotifyVarChanged drives — without it the CS2
 *  scripts that paint an inventory only ever run at interface-build time, so a
 *  container that arrives afterwards (which is always, for a server-driven
 *  inventory) never reaches the screen. `container_id` < 0 means "unknown,
 *  re-run every hook". */
void
RS_CS2Host_NotifyInvChanged(
    struct RS_CS2Host* host,
    int container_id);

void
RS_CS2Host_SetBridge(
    struct RS_CS2Host* host,
    struct UITreeSceneBridge* bridge);

/** Mirror the live orbit camera into the host so CAM_GETANGLE_XA/YA and
 *  CAM_GETYAW answer with the real thing. Call once per logic tick, in script
 *  units (pitch 128..383, yaw 0..2047). A pending CAM_FORCEANGLE wins: the
 *  mirror is skipped until RS_CS2Host_TakeCameraForce has handed it over. */
void
RS_CS2Host_SetCameraAngles(
    struct RS_CS2Host* host,
    int angle_x,
    int angle_y);

/** Consume a pending CAM_FORCEANGLE. Returns true once per script snap and
 *  writes the requested angles; returns false when nothing forced them. */
bool
RS_CS2Host_TakeCameraForce(
    struct RS_CS2Host* host,
    int* out_angle_x,
    int* out_angle_y);

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
