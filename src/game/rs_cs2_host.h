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
struct LootStore;

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
    /** RESUME_COUNTDIALOG (3104) — not social, and neither is CHEAT above.
     *  What this queue actually is, and has been since CHEAT joined it, is
     *  "outbound packets a CS2 script asked for": the one thing they share is
     *  that the CS2 host has no socket. The number rides in `text` because
     *  that is the form the opcode pops it in. */
    RS_CS2_SOCIAL_SEND_RESUME_COUNTDIALOG,
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

/* Pending CC_TRIGGEROP requests. Same shape and reasoning as
 * RS_CS2_HOST_CALL_ON_RESIZE_MAX above — one queued pair per call site. */
#define RS_CS2_HOST_TRIGGER_OP_MAX 16

/* Pending IF_TRIGGEROPLOCAL → IF_BUTTON1 sends. One click synthesizes one
 * packet; 16 matches the other deferred queues. */
#define RS_CS2_HOST_TRIGGEROPLOCAL_MAX 16

/*
 * Pending sound requests from scripts.
 *
 * Deeper than the other deferred queues because sound is bulk: a single tab
 * switch can fire several interaction sounds, and one bake of a busy interface
 * more. Dropping a UI click sound is not fatal, but the whole point of the
 * queue is that the common case never reaches the limit.
 */
#define RS_CS2_HOST_SOUND_MAX 64

/* Cache script option ids used by interface 116's audio panel. */
#define RS_CS2_GAMEOPTION_MUSIC_VOLUME 7
#define RS_CS2_GAMEOPTION_SOUND_VOLUME 8
#define RS_CS2_GAMEOPTION_AREA_VOLUME 9
#define RS_CS2_DEVICEOPTION_MASTER_VOLUME 19
#define RS_CS2_OPTION_MAX 64

/** Which of the three option tables an option id belongs to. CS2 keeps them
 *  apart (CLIENT/GAME/DEVICEOPTION_GET/SET), and the same id means different
 *  things in each. */
enum RS_CS2OptionKind
{
    RS_CS2_OPTION_CLIENT = 0,
    RS_CS2_OPTION_GAME = 1,
    RS_CS2_OPTION_DEVICE = 2,
    RS_CS2_OPTION_KIND_COUNT = 3
};

/* Backing varps used by interface 116. Its mute icons write these without
 * calling GAMEOPTION_SET / DEVICEOPTION_SET. */
#define RS_CS2_VARP_MUSIC_VOLUME 168
#define RS_CS2_VARP_SOUND_VOLUME 169
#define RS_CS2_VARP_AREA_VOLUME 872
#define RS_CS2_VARP_MASTER_VOLUME 3796
#define RS_CS2_VARP_AREA_OVERRIDE_ENABLED 5588
#define RS_CS2_VARP_AREA_OVERRIDE_VOLUME 5589

/** Complete audio-settings snapshot, in the interface's 0..100 domain. */
struct RS_CS2AudioSettings
{
    int master;
    int music;
    int sounds;
    int area_sounds;
};

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

/** A CC_TRIGGEROP request: which component's on_op to run, and the op index
 *  to report to it as event_opindex. */
struct RS_CS2TriggerOp
{
    int component_id;
    int op_index;
};

/**
 * A sound a script asked for, waiting for the App to play it.
 *
 * Queued rather than played inline for the same reason as the other host
 * requests: this is reached from inside a running CS2 script, which has no
 * business touching the audio queue mid-execution -- and a script that yields
 * and rolls back should not have already made a noise.
 */
struct RS_CS2Sound
{
    /** enum RS_CS2SoundKind. */
    int kind;
    int id;
    int secondary_id;
    int loops;
    /** Client cycles, as the scripts carry them; converted where it is played. */
    int delay;
    int fade_out_delay;
    int fade_out_speed;
    int fade_in_delay;
    int fade_in_speed;
};

enum RS_CS2SoundKind
{
    RS_CS2_SOUND_SYNTH = 0,
    RS_CS2_SOUND_SONG,
    RS_CS2_SOUND_JINGLE,
    RS_CS2_SOUND_SONG_WITHSECONDARY,
};

/** An IF_TRIGGEROPLOCAL request: IF_BUTTON1(component, sub) to send. */
struct RS_CS2TriggerOpLocal
{
    int component_id;
    int sub;
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
    /**
     * The local player's display name, backing CHAT_PLAYERNAME (5015).
     *
     * The one script that made this matter is 223, the chatbox input line:
     * `<icon><chat_playername>: <col=0000ff><typed></col>*`. An empty answer
     * there is not "no name" — the leading colon still draws, so the line reads
     * `: hello*` and looks like a rendering bug rather than a missing opcode.
     * Mirrors `RS_Chat.username`, which the client learns from its own
     * PLAYER_APPEARANCE (the same source the reference's `localPlayer.name`
     * has), so the two cannot disagree.
     */
    char local_player_name[RS_CS2_HOST_SOCIAL_NAME_LEN];
    struct UITreeSceneBridge* bridge; /* may be NULL until set */

    /*
     * The server's IF_SETEVENTS entry for a component, for the opcodes that
     * must read the *effective* flags rather than the cache's. Returns nonzero
     * and fills `*out_events` when the server declared one; zero when it did
     * not, leaving the caller on the widget's decoded flags.
     *
     * Deob `method12093` is that rule exactly, and it is why this reports
     * presence rather than returning a merged answer: a *zero* override is
     * meaningful (it disarms cache-authored flags) and must not read as absent.
     *
     * A callback for the same reason RS_MinimenuBuildCtx.events_for_component
     * is one — the store lives on the App and this header stays clear of the
     * game layer. NULL = no server events, correct for the classic revisions.
     */
    int (*events_override_for_component)(void* user, int com_id, int* out_events);
    void* events_user;

    bool has_pending;
    struct CS2VM_HostRequest pending;

    struct VarCManager* varcs; /* client-variable store; may be NULL */
    struct LootStore* loot;   /* client-native loot tracker; may be NULL */

    int client_clock;
    /** The client canvas, and what GETCANVASSIZE / VIEWPORT_GETEFFECTIVESIZE
     *  return. One of three copies of the canvas size — write it through
     *  App_SetCanvasSize, never here, or the layout and the scripts that read it
     *  back disagree (app.h says what that looks like). */
    int viewport_w;
    int viewport_h;
    /** Window mode (enum CS2VM_WindowMode), backing GET/SETWINDOWMODE and their
     *  `default` siblings. `window_mode_dirty` is raised by a SET and drained by
     *  the App, which owns the canvas and the SDL window — same shape as
     *  `close_modal_requested`. The default pair is what the client would come
     *  up in next boot; nothing persists it yet. */
    int window_mode;
    int default_window_mode;
    bool window_mode_dirty;
    /** Display-panel client layout mode 0/1/2 (Fixed / Classic / Modern).
     *  Stashed when [clientscript,settings_client_mode] (cache script_3998)
     *  calls setwindowmode; drained to WINDOW_STATUS so the server remounts. */
    int client_layout_mode;
    bool client_layout_dirty;
    /** Cache id of settings_client_mode (pack name script_3998). Dialect/cache
     *  surface for observing the dropdown's mode arg on SETWINDOWMODE. */
    int script_settings_client_mode;
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
    /** Audio volumes, backing SET/GETVOLUME* and interface 116's GAMEOPTION /
     *  DEVICEOPTION calls. Values are percentages, matching the CS2 surface. */
    int volume_music;
    int volume_sounds;
    int volume_area_sounds;
    int client_options[RS_CS2_OPTION_MAX];
    int game_options[RS_CS2_OPTION_MAX];
    int device_options[RS_CS2_OPTION_MAX];
    bool audio_settings_dirty;
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

    /** Set by IF_RESUME_PAUSEBUTTON / CC_RESUME_PAUSEBUTTON. Packed component
     *  uid to send as RESUME_PAUSEBUTTON, or -1 when none is pending. Drained
     *  by the App through button_sink.resume_pausebutton. */
    int resume_pausebutton_component_id;

    /** VIEWPORT_SETZOOM/GETZOOM (6201/6204) = reference client.field780 and
     *  field747: the NEAR and FAR endpoints of the FOLLOW CAMERA'S ORBIT
     *  DISTANCE, interpolated over the world viewport height the same way
     *  viewport_zoom_near/far below interpolate the projection scale. Stored
     *  raw (SETFOV's method5659 decode does not apply here). Read every cycle
     *  by app_world_cam_dist_zoom — not a value/max pair. */
    int viewport_zoom;
    int viewport_zoom_max;

    /** VIEWPORT_SETFOV's two arguments, decoded the way the reference client
     *  decodes them (Statics.method5659: (int)pow(2, arg/256 + 7), falling back
     *  to 256 when that is <= 0). They are the NEAR and FAR endpoints of a zoom
     *  interpolated over the world viewport HEIGHT in class159.method5357, not a
     *  value/max pair — see docs/ORANGE_WEDGE.md 2.
     *
     *  This decoded pair is the ONLY thing the reference stores (client.field976
     *  / field801); GETFOV re-encodes it with Statics.method9013 rather than
     *  answering the raw arguments, so the round trip is deliberately lossy —
     *  `viewport_setfov(512, 220)` reads back as 512, 219. Keeping the raw args
     *  beside these to answer GETFOV exactly would be a nicer API and a
     *  divergence, and CLAMPFOV used to overwrite them, which made GETFOV report
     *  the clamp instead of the FOV.
     *
     *  Read by the env-gated TORIRS_WEDGE_SCALE experiment in app.c and by
     *  rs_cs2_viewport_effective_size. */
    int viewport_zoom_near;
    int viewport_zoom_far;

    /**
     * VIEWPORT_CLAMPFOV's four arguments (reference client.field804 / field805
     * / field1040 / field810, set by Statics.method6341 case 6202).
     *
     * They are two independent ranges, not a value/min/max: the first pair
     * bounds the interpolated FOV and the second bounds
     * `height * fov * 512 / (width * 334)`, the quantity class159.method5357
     * letterboxes on. CLAMPFOV does NOT touch the FOV itself — GETFOV keeps
     * answering whatever SETFOV last stored.
     *
     * Each argument falls back to the reference's default when <= 0 (1 for a
     * minimum, 32767 for a maximum), and each maximum is raised to its own
     * minimum. `viewport_clampfov(0, 0, 0, 0)` — what toplevel_resize sends for
     * the ordinary camera — therefore means "no clamp at all".
     */
    int viewport_fov_min;
    int viewport_fov_max_clamp;
    int viewport_aspect_min;
    int viewport_aspect_max;

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
     *  beside it for the same reason as misc: CC/IF_SETONFRIENDTRANSMIT carries no
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

    /** (component, op index) pairs CC_TRIGGEROP asked to run, drained by the
     *  App's tick through RS_CS2Host_TakeTriggerOp. Queued for the same
     *  reason as call_on_resize: reached from inside a running CS2 script,
     *  and this host has no task runner to nest a second one on. */
    struct RS_CS2TriggerOp trigger_op[RS_CS2_HOST_TRIGGER_OP_MAX];
    int trigger_op_count;
    int trigger_op_head;

    /** Sounds SOUND_SYNTH/SONG/JINGLE asked for, drained by the App's tick
     *  through RS_CS2Host_TakeSound. */
    struct RS_CS2Sound sound[RS_CS2_HOST_SOUND_MAX];
    int sound_count;
    int sound_head;

    /** (component, sub) pairs IF_TRIGGEROPLOCAL asked to send as IF_BUTTON1,
     *  drained by the App's tick through RS_CS2Host_TakeTriggerOpLocal. */
    struct RS_CS2TriggerOpLocal triggeroplocal[RS_CS2_HOST_TRIGGEROPLOCAL_MAX];
    int triggeroplocal_count;
    int triggeroplocal_head;
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

/** Pop the oldest queued CC_TRIGGEROP request, FIFO. Returns false when the
 *  queue is empty. The App drains this once per tick and runs each
 *  component's on_op listener with event_opindex set to op_index; nothing
 *  else may consume it. */
/** Pop the oldest queued script sound. False when the queue is empty. */
bool
RS_CS2Host_TakeSound(
    struct RS_CS2Host* host,
    struct RS_CS2Sound* out);

/** Take the latest audio settings after a CS2 SET option. Coalesces slider
 *  drags to one snapshot per app tick. */
bool
RS_CS2Host_TakeAudioSettings(
    struct RS_CS2Host* host,
    struct RS_CS2AudioSettings* out);

/** Apply interface 116's varp-only mute/unmute path to the option store. */
void
RS_CS2Host_SyncAudioVarp(
    struct RS_CS2Host* host,
    int varp_id);

/**
 * The value an option holds on a fresh client.
 *
 * RS_CS2Host_Init seeds the three tables from this, and game/rs_prefs.c omits
 * from the preferences file any option still equal to it. Both need the same
 * answer, so it is one function rather than the same constant written twice —
 * a default stated in two places is a default that drifts, and the symptom
 * there would be a saved file that silently re-mutes the client.
 */
int
RS_CS2Host_OptionDefault(
    int kind,
    int option_id);

/** Read one option table entry. Out-of-range ids read 0, as CS2's GET does. */
int
RS_CS2Host_GetOption(
    struct RS_CS2Host const* host,
    int kind,
    int option_id);

/**
 * Write one option table entry, with the side effects CS2's SET has: the four
 * volume ids are clamped to 0..100, mirrored into the volume_* fields the
 * GETVOLUME* opcodes read, and flagged for the App to push at the mixer.
 *
 * The CS2 SET opcodes and the preferences restore both go through here so a
 * restored volume behaves exactly like a dragged one.
 */
void
RS_CS2Host_SetOption(
    struct RS_CS2Host* host,
    int kind,
    int option_id,
    int value);

bool
RS_CS2Host_TakeTriggerOp(
    struct RS_CS2Host* host,
    struct RS_CS2TriggerOp* out);

/** Pop the oldest queued IF_TRIGGEROPLOCAL request, FIFO. Returns false when
 *  the queue is empty. The App drains this once per tick and sends
 *  IF_BUTTON1(component, sub); nothing else may consume it. */
bool
RS_CS2Host_TakeTriggerOpLocal(
    struct RS_CS2Host* host,
    struct RS_CS2TriggerOpLocal* out);

/**
 * A script-side varp/varbit write: the optimistic value, plus a var-transmit
 * notification when — and only when — the value actually moved.
 *
 * The change gate is load-bearing, not an optimisation. Announcing every write
 * lets a hook that re-asserts the var it watches re-trigger itself forever;
 * announcing none leaves interface 116's slider bobbles grey after the mute
 * icon writes %var3796, because script 7101 re-colours them from that varp's
 * transmit hook and nothing else calls it on that path.
 */
void
RS_CS2Host_ScriptWriteVarp(
    struct RS_CS2Host* host,
    int varp_id,
    int value);

void
RS_CS2Host_ScriptWriteVarbit(
    struct RS_CS2Host* host,
    int varbit_id,
    int value);

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

/**
 * Drop every host-side transmit hook (inv / var / stat) for interface
 * `group_id`, and clear that pack's reactive component listeners
 * (timer, key, transmit, resize, sub_change), including same-group dynamic
 * children. Interaction hooks (click/op/hold/drag) stay on the reused bake —
 * packs like the gameframe install those once and do not re-run on sidebar
 * remount. A block with no remaining interaction slots is freed.
 *
 * Call this when IF_CLOSESUB or a replacing IF_OPENSUB unmounts a group.
 * Hiding alone leaves the nodes in the tree for reuse; without this the
 * host registries keep firing for them (and grow again when onload
 * re-registers under new dynamic uids).
 */
void
RS_CS2Host_ClearHooksForInterfaceGroup(
    struct RS_CS2Host* host,
    int group_id);

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
