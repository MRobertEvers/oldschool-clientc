#ifndef RS_CS2_HOST_H
#define RS_CS2_HOST_H

#include "cs2vm2/cs2vm2_host.h"
#include "game/rs_clientop.h"
#include "game/rs_entity_overlay.h"
#include "game/rs_highlight.h"
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
struct RS_Chat;
struct LootStore;
struct ToriRS_Component;

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
    RS_CS2_SOCIAL_SEND_MESSAGE_PUBLIC,
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

/** Settings-panel row uses buffered for the App between two frames. A person
 *  cannot click eight rows in one frame; the depth is for a frame that stalls,
 *  not for a burst. */
#define RS_CS2_HOST_SETTINGS_ACTIONS_MAX 8

/** Colour rows the panel is expected to hold at once. The rev-239 cache's read
 *  hub (script_4181) switches on 49 setting ids across every category; this is
 *  that with headroom, and a row past it is refused and logged rather than
 *  evicting one that is on screen. */
#define RS_CS2_HOST_SETTINGS_COLOURS_MAX 96

/* Cache script option ids used by interface 116's audio panel, plus the one
 * game option that is not audio: "hide roofs", which GETREMOVEROOFS /
 * SETREMOVEROOFS (3111/3112) name directly and which the world render reads. */
#define RS_CS2_GAMEOPTION_HIDE_ROOFS 1
#define RS_CS2_GAMEOPTION_MUSIC_VOLUME 7
#define RS_CS2_GAMEOPTION_SOUND_VOLUME 8
#define RS_CS2_GAMEOPTION_AREA_VOLUME 9
#define RS_CS2_DEVICEOPTION_MASTER_VOLUME 19
/* All Settings > Display: "Interface scaling mode". The cache's enum_4033
 * stores these values directly in device option 15. */
#define RS_CS2_DEVICEOPTION_UI_SCALE_MODE 15
#define RS_CS2_UI_SCALE_MODE_NEAREST 0
#define RS_CS2_UI_SCALE_MODE_LINEAR 1
#define RS_CS2_UI_SCALE_MODE_BICUBIC 2
/* "Interface scaling" (All Settings > Display). The row is built by cache
 * script_3850 and applied by script_3967 case 79 -> script_3054, whose whole
 * body is `deviceoption_set(27, max(~script3333, min(400, v)))`; the label
 * comes back through script_9116 case 79 -> `deviceoption_get(27)` rendered as
 * "<n>%". So the id is a PERCENTAGE and its domain is stated by the script,
 * not by us: ~script3333 is 100 on desktop (175 on mobile) and the ceiling is
 * 400.
 *
 * It is not in the rev-239 Java client's device-option table (deob class64
 * lists ids -1/2/3/4/5/6/14/19/22 and throws "Unrecognized device option" for
 * anything else) — the row is gated behind ~script100, which is true only for
 * the enhanced/mobile client types, and this client reports clienttype 10.
 * There is therefore no reference implementation to copy; the semantics below
 * are the ones the scripts state. */
#define RS_CS2_DEVICEOPTION_UI_SCALE 27
#define RS_CS2_UI_SCALE_MIN 100
#define RS_CS2_UI_SCALE_MAX 400

/* Setting-struct params the panel itself reads, and this client reads with it.
 * `param_1078` is the row KIND -- 9 is the colour row -- and 1077 / 1086 / 1230
 * are that row's setting id, its title and the swatch it shows before anyone
 * has picked one. */
#define RS_CS2_PARAM_SETTING_ID 1077
#define RS_CS2_PARAM_SETTING_KIND 1078
#define RS_CS2_PARAM_SETTING_LABEL 1086
#define RS_CS2_PARAM_SETTING_COLOUR_DEFAULT 1230
#define RS_CS2_SETTING_KIND_COLOUR 9

/**
 * A colour row's swatch, clicked.
 *
 * Everything the App needs to put a picker on screen and write the answer
 * back, resolved here because this is the side that can see the cache: the
 * struct behind the row is loaded (the panel read its title out of it to draw
 * the row) and the varp behind it was learned when the row was built.
 */
struct RS_CS2SettingsColourRequest
{
    /** `param_1077`, the id every settings hub switches on. */
    int setting_id;
    /** The varp holding `colour + 1`, or -1 when the read hub never named one
     *  -- which is a row this client cannot write, and is said so out loud
     *  rather than written to varp -1. */
    int varp_id;
    /** Current value as 0xRRGGBB: the varp less one, or `default_colour` when
     *  the varp is 0, which is what "never chosen" is stored as. */
    int colour;
    /** `param_1230`, the swatch the panel draws before anyone picks. */
    int default_colour;
    /** The component the op was dispatched on, so a picker can open beside the
     *  swatch instead of in the middle of the screen. -1 when unknown. */
    int component_id;
    /** `param_1086`, the row's own title ("Tile highlight colour"). */
    char label[64];
};

/* Settings-panel setting ids (struct param_1077), as switched on by the cache's
 * settings hubs script_3962 (read) and script_3967 (apply). Only the ones this
 * client has to recognise by name are listed. */
#define RS_CS2_SETTING_CLIENT_LAYOUT 12
#define RS_CS2_SETTING_UI_SCALE 79
#define RS_CS2_SETTING_UI_SCALE_MODE 169
#define RS_CS2_OPTION_MAX 64

/**
 * The two option tables.
 *
 * There is no third one: CLIENTOPTION_GET/SET (3209/3210) is the *generic*
 * form, and the reference resolves its id against the device table first and
 * the game table second (rev-239 deob, Statics 3209/3210 — `class64` device
 * options, `class67` game options). A private third table would silently
 * swallow a script that sets a volume through the generic op and reads it back
 * through GAMEOPTION_GET. See RS_CS2Host_ClientOptionKind.
 */
enum RS_CS2OptionKind
{
    RS_CS2_OPTION_GAME = 0,
    RS_CS2_OPTION_DEVICE = 1,
    RS_CS2_OPTION_KIND_COUNT = 2
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
    /** Message body: MESSAGE_PRIVATE, MESSAGE_PUBLIC and CHEAT. */
    char text[RS_CS2_HOST_SOCIAL_TEXT_LEN];
    /** public / private / trade: CHAT_SETMODE only. */
    int modes[3];
    /** Packed colour/effect the line is spoken in: MESSAGE_PUBLIC only. High
     *  byte colour, low byte effect. */
    int colour_effect;
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
    /** A matching container changed while this hook was hidden. An unhide pass
     *  resumes only hooks carrying this bit, not every globally stale hook. */
    uint8_t pending_unhide;
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
    /** A matching varp changed while this hook was hidden. */
    uint8_t pending_unhide;
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
    /** A matching skill changed while this hook was hidden. */
    uint8_t pending_unhide;
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
    /** The message store the CHAT_GETHISTORY* opcodes read and
     *  RS_CS2Host_ChatAdd writes. May be NULL (a headless harness with no
     *  chatbox), in which case the history opcodes answer empty. */
    struct RS_Chat* chat;
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

    /*
     * The scene, for the two ops that ask about it: LOC_FIND (6803) and
     * COORD_INSCENE (6951).
     *
     * Callbacks for the same reason the one above is one -- the scene lives on
     * the App and this header stays clear of the world layer. Both NULL is a
     * host with no world, where LOC_FIND finds nothing and COORD_INSCENE says
     * no; every static-overlay script then declines to draw, which is the
     * truthful answer for a client that has not loaded a map.
     *
     * `loc_at_coord` returns nonzero when a loc of `loc_type` stands on
     * `coord`, filling `*out_layer` (World_LocShapeToLayer) and up to
     * `name_cap` bytes of its name.
     */
    int (*loc_at_coord)(
        void* user,
        int coord,
        int loc_type,
        int* out_layer,
        char* out_name,
        int name_cap);
    int (*coord_in_scene)(void* user, int coord);
    void* world_user;

    bool has_pending;
    struct CS2VM_HostRequest pending;

    struct VarCManager* varcs; /* client-variable store; may be NULL */
    struct LootStore* loot;   /* client-native loot tracker; may be NULL */

    int client_clock;

    /* The local player's packed coord (plane<<28 | x<<14 | z), refreshed by the
     * App each frame. Cache scripts branch on it - the raid HUDs decide which
     * panel to show from where the player is standing - and -1 means "the world
     * has no local player yet", which reads as no tile rather than as tile
     * zero. See CS2VM2_Op_Coord. */
    int local_coord;
    /**
     * Where the local player is WALKING to, packed the same way, or -1.
     *
     * Opcode 3330, and the destination-tile highlight's whole input:
     * clientscript 5210 guards it as `if (_3330 ! null)` before marking the
     * tile, so -1 has to mean "not walking anywhere" and not tile zero. The
     * map flag is the source, exactly as it is for the plugin api's
     * `dest_x`/`flag_x` -- the route queue trails behind the player and never
     * holds the destination at all.
     */
    int dest_coord;

    /**
     * The tile the pointer is over, packed the same way, or -1.
     *
     * Backs `_6950` when no client op is being dispatched. That op is the
     * "current tile target": during a client op it is the tile the row was
     * built for, and outside one it is the mouseover -- clientscript 5197
     * ("Highlight hovered tile") reads it with no client op in sight.
     */
    int hover_coord;

    /**
     * The three tile-highlight refresh scripts, by cache id.
     *
     * They take no arguments and read their subject from a var or an opcode --
     * 5204 reads `coord`, 5210 reads `_3330`, 5197 reads `_6950` -- so the
     * client's whole job is to RE-RUN each one when its subject changes.
     * Nothing in the cache calls them; the reference client does, on the same
     * three edges.
     *
     * Held here beside `script_settings_client_mode` for the same reason: a
     * cache id this client has to know by number belongs in one place where it
     * can be checked against the cache, not spelled inline at the call site.
     */
    int script_highlight_hover_tile;
    int script_highlight_current_tile;
    int script_highlight_dest_tile;

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
    /** True once a clientscript has chosen the default window mode
     *  (SETDEFAULTWINDOWMODE), as opposed to the boot config having stated one.
     *  game/rs_prefs.c saves only the former: the manifest's `--window-mode`
     *  configures a run, and a run's configuration must not quietly become the
     *  player's saved setting. */
    bool default_window_mode_from_script;
    bool window_mode_dirty;
    /** Display-panel client layout mode 0/1/2 (Fixed / Classic / Modern).
     *  Stashed when [clientscript,settings_client_mode] (cache script_3998)
     *  calls setwindowmode; drained to WINDOW_STATUS so the server remounts. */
    int client_layout_mode;
    bool client_layout_dirty;
    /** Cache id of settings_client_mode (pack name script_3998). Dialect/cache
     *  surface for observing the dropdown's mode arg on SETWINDOWMODE. */
    int script_settings_client_mode;
    /** Cache id of the All Settings panel's client-side apply hub (script_3967)
     *  and of the varbit its first statement writes (9657, "the setting the
     *  player just changed").
     *
     *  Together they are this client's only view of a selection made in the
     *  All Settings panel: the panel is built entirely by clientscripts, and a
     *  dropdown row's whole apply path is `~script3967(<setting id>, <choice>,
     *  -1)`. Every setting that hub knows is applied inside it — except the
     *  client layout (setting 12), which has no case there in any cache this
     *  client can read, so picking a layout in All Settings changed nothing at
     *  all. See RS_CS2Host_Exec's varbit case. */
    int script_settings_client_apply;
    int varbit_settings_last_changed;
    /**
     * Setting ids the All Settings panel has named since the App last looked.
     *
     * Every one of the four apply hubs (3965 toggle, 3966 slider, 3967
     * dropdown, 3969 button) opens by writing the setting id to
     * `varbit_settings_last_changed`, so that write IS the panel telling the
     * client which row was used -- and it is the ONLY trace a button row
     * leaves, because 3969's switch has no case for either of the two buttons
     * in the Activities category.
     *
     * Queued rather than latched in the varbit, because the varbit cannot say
     * "pressed twice": a second press writes the same value and the change
     * gate in the var layer drops it. A queue also keeps the reader out of the
     * running script, which is the same rule every other host request here
     * follows.
     *
     * `value` is the hub's chosen value where it has one and -1 where it does
     * not. Overflow drops the OLDEST, since a settings action nobody drained
     * for eight rows is a stalled frame and the newest press is the one the
     * user is waiting on.
     */
    /**
     * What the cache asked this client to highlight.
     *
     * The HIGHLIGHT_* family (7000..7044) is how the settings panel's
     * Activities category actually reaches the client: 125 clientscripts read
     * a varbit and a colour row and describe a group here. See rs_highlight.h.
     *
     * Held on the host and not in the App, for the same reason every other
     * script-written state is: it is written from inside a running script,
     * through the host request path, and the App reads it afterwards.
     */
    struct RS_HighlightState highlight;

    /**
     * The client-owned right-click rows the cache installed, and what the one
     * being dispatched is about.
     *
     * The other half of the highlight story: the groups were being set up all
     * along, and nothing was ever put in them because the scripts that do the
     * putting read their subject out of here. See rs_clientop.h.
     */
    struct RS_ClientOpState clientop;

    /**
     * Interface components the cache hung off things in the world.
     *
     * The other half of the Activities category: what is not a highlight is one
     * of these. The layer each overlay names by `component_id` lives in the
     * UITree under the `entity_overlay` builtin, so `cc_*` reaches it the same
     * way it reaches a panel; the App projects the anchor and moves it each
     * frame. See rs_entity_overlay.h.
     */
    struct RS_OverlayState overlay;

    int settings_action_id[RS_CS2_HOST_SETTINGS_ACTIONS_MAX];
    int settings_action_value[RS_CS2_HOST_SETTINGS_ACTIONS_MAX];
    int settings_action_count;

    /**
     * Settings varbit writes, waiting to be mirrored to the SERVER.
     *
     * ## Why the server has to be told, and why nothing tells it
     *
     * Ten rows of the Activities category are decided server-side -- the
     * Agility / Slayer / Blast Furnace helpers, the clue helper's marker, arrow
     * and infobox, the iron loot warnings, the boss health overlay and the
     * max-hit threshold. Every one of them reads a varbit whose base varp is an
     * ORDINARY SERVER VARP (`ironman_var_1`, `options_varp`, `options_mobile`
     * ...), and the panel writes it with `VarPManager_SetVarbitOptimistic` --
     * the client's own copy only. `VarPManager_ApplySync` overwrites that copy
     * from `var_serv` the moment the server speaks about the varp, so the write
     * is not merely invisible to the server, it is not durable here either.
     *
     * Nothing in the revision closes that gap:
     *
     *   - rev239's client prot table (`3rd/rsprot/gen/rev239_prot.h`) carries no
     *     varp, varbit or settings packet. `SET_CHATFILTERSETTINGS` is the only
     *     settings-shaped entry and it is about chat filters.
     *   - the reference client does not transmit either: NXT's
     *     `ClientVarCache::SetVarbit` writes `m_var` and returns, and `m_varServ`
     *     is written only by the inbound `VARP_*` handlers.
     *   - the panel does not ask the server: there is no `if_triggerop` or
     *     `cc_triggerop` anywhere in interface 134's script family, and the
     *     cache's own server-applied row kind (`~script3968`) has an empty
     *     switch, which none of these rows uses.
     *
     * So the reference server holds these varps by a path this revision's prot
     * table does not show, and the client's write is a prediction of a value the
     * server is expected to already agree with.
     *
     * ## What this client does instead, and why it is CLIENT_CHEAT
     *
     * The App drains this queue and sends `::setting <varbit> <value>` over
     * `CLIENT_CHEAT`, which ToriRSServer applies to the player's varps.
     *
     * CLIENT_CHEAT rather than a new opcode, deliberately. Adding a client
     * packet id that rev239 does not define would make this client unable to
     * talk to a real rev239 server at all -- an unknown opcode is not ignored,
     * it desynchronises the stream, because the reader takes the packet's LENGTH
     * from the prot table. CLIENT_CHEAT is a real rev239 client packet with a
     * var-u8 string payload, so a server that does not know the command answers
     * "unknown command" or says nothing, and the connection survives. A wire
     * extension that degrades to a no-op is the only kind worth having here.
     *
     * ## What is mirrored, and what is not
     *
     * Only writes made INSIDE an All Settings apply hub. The root script id of
     * the frame that wrote `%varbit9657` is remembered, and a varbit write is
     * mirrored only while that same script is the root -- so the 510
     * clientscripts in this cache that write a varbit for some other reason
     * (a quest stage, a panel's scroll position) say nothing to the server,
     * which is right: those are the server's own state and it already knows.
     *
     * Learned rather than tabulated, the same way `settings_colour_varp` is: the
     * hub announces itself by writing 9657 as its first statement, so nothing
     * here has to carry a list of hub script ids to keep in step with the cache.
     */
    int settings_mirror_varbit[RS_CS2_HOST_SETTINGS_ACTIONS_MAX];
    int settings_mirror_value[RS_CS2_HOST_SETTINGS_ACTIONS_MAX];
    int settings_mirror_count;
    /** The apply hub's own script id, learned from the frame that wrote 9657.
     *  -1 before the panel has ever applied anything. */
    int settings_mirror_root_script;

    /**
     * The All Settings panel's COLOUR rows, and the one the player just
     * clicked.
     *
     * A colour row (`param_1078 = 9`, built by clientscript 4182) hangs
     * `settings_colour_input_click` off its swatch, and that script's whole
     * body is `~settings_op_checker` -- a click sound and, on a blocked row,
     * the "you cannot change this" message. There is no apply in the cache
     * because there is none to write: the reference opens a picker of its own
     * from here and writes the row's varp itself. So does this client; see
     * RS_CS2Host_ScriptStarted.
     *
     * `settings_colour_varp` is LEARNED rather than tabulated. The read hub
     * `settings_get_colour` (script_4181) is a switch from setting id to
     * `calc(%var<n> - 1)`, so the varp behind a row is stated only inside that
     * script -- and the row builder calls it while laying the row out, which
     * is necessarily before anyone can click the swatch. Watching the varp
     * read it performs therefore answers "which varp is this row" from the
     * cache itself, for every colour row and every revision, instead of from
     * a fifty-line table this file would have to keep in step by hand.
     *
     * To check one by hand:
     *     3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 \
     *         --rev osrs239 --out /tmp/cs2 4181
     */
    int settings_colour_setting[RS_CS2_HOST_SETTINGS_COLOURS_MAX];
    int settings_colour_varp[RS_CS2_HOST_SETTINGS_COLOURS_MAX];
    int settings_colour_count;
    /** Cache ids of the two scripts above -- the op script whose run IS the
     *  click, and the read hub whose varp read names the row's varp. */
    int script_settings_colour_click;
    int script_settings_colour_get;
    /** The click waiting for the App to open a picker for it. One slot and not
     *  a queue: a second click before the first is drained is the same person
     *  changing their mind about which row they meant, and the newer one is
     *  the one they are looking at. */
    struct RS_CS2SettingsColourRequest settings_colour_request;
    bool settings_colour_pending;
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
    /* Two tables, not three — see enum RS_CS2OptionKind. */
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

    /** Raised whenever the interface scale (device option 27) changes value.
     *  Drained by the App, which owns the canvas — same shape as
     *  `window_mode_dirty`. The scale itself is NOT a separate field: it lives
     *  in `device_options[RS_CS2_DEVICEOPTION_UI_SCALE]` so the two spellings
     *  the cache uses for it (deviceoption 27 and the UIZOOM_* opcode family)
     *  cannot disagree. */
    bool ui_scale_dirty;

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
    /** Set when a message reached the chat store. Same shape as misc and
     *  friend -- CC/IF_SETONCHATTRANSMIT carries no trigger list, so every
     *  registered hook re-runs. This is the stamp the reference bumps in
     *  `addChatMessage`, and it is what makes the cache's own
     *  `[proc,rebuildchatbox]` run: the chatbox is 500 text components the
     *  scripts fill, and without this dispatch nothing ever asks them to. */
    int chat_transmit_dirty;
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

/** Arm the var- and inv-transmit hooks a component record declares in the
 *  cache (`onVarpTransmit`/`varpTriggers`, `onInvTransmit`/`inventoryTriggers`).
 *  The runtime `if_seton*transmit` opcodes write the same two tables; this is
 *  the same registration for the half a script never runs. Call it while baking
 *  a pack, before the mount's initial transmit dispatch. */
void
RS_CS2_RegisterCacheTransmitHooks(
    struct RS_CS2Host* host,
    struct ToriRS_Component const* src);

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

/** Point the CHAT_* history opcodes at the client's message store. */
void
RS_CS2Host_SetChat(
    struct RS_CS2Host* host,
    struct RS_Chat* chat);

/**
 * Add a message and tell the chatbox scripts about it.
 *
 * The one entry point for anything that has a host, because the two halves
 * belong together: a message the store holds but the transmit channel never
 * announced is a line the cache's chatbox will not draw until something else
 * happens to repaint it. Stamps the client clock, which is the host's to know.
 */
void
RS_CS2Host_ChatAdd(
    struct RS_CS2Host* host,
    int type,
    char const* name,
    char const* sender,
    char const* text);

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
/**
 * Pop the oldest All Settings row use, FIFO. False when the queue is empty.
 *
 * `*out_value` is the row's chosen value, or -1 for a row whose apply hub
 * carries none (every toggle, and both of the Activities buttons). The App
 * drains this once per frame and hands it to the plugin layer, which is where
 * the builtins that implement those rows live.
 */
bool
RS_CS2Host_TakeSettingsAction(
    struct RS_CS2Host* host,
    int* out_setting_id,
    int* out_value);

/**
 * Pop the oldest settings varbit write waiting to be mirrored to the server.
 *
 * FIFO, and false when the queue is empty. See `settings_mirror_varbit` for why
 * a client-side settings write has to reach the server at all.
 */
bool
RS_CS2Host_TakeSettingsMirror(
    struct RS_CS2Host* host,
    int* out_varbit_id,
    int* out_value);

/**
 * Queue a settings varbit write for the server directly, bypassing the "was a
 * hub on the stack" test.
 *
 * For the writers that are not the panel: `TORIRS_SIM_VARBIT`, which exists
 * precisely because nothing in the cache writes these varbits and a headless
 * run has no panel to click. A simulated write that the server never heard
 * about would make every server-side row untestable from a headless run, which
 * is the only way most of them can be tested at all.
 */
void
RS_CS2Host_QueueSettingsMirror(
    struct RS_CS2Host* host,
    int varbit_id,
    int value);

/**
 * A clientscript is about to run, with its arguments already in its locals.
 *
 * The one seam this client has on a script's ARGUMENTS. A hook that fires from
 * inside an opcode (the way the client-layout apply does, off the varbit write
 * that opens its hub) can only see scripts that execute an opcode worth
 * watching, and `settings_colour_input_click` executes none: it plays the
 * panel's click sound and returns. Its arguments -- which row was clicked, and
 * whether the row is enabled -- are the whole of what it says, and they exist
 * only here.
 *
 * @param component_id the component the op was dispatched on, or -1.
 *
 * Deliberately narrow: it claims one script id and ignores every other, so the
 * per-script cost is one integer compare on a path that runs for every hook in
 * the cache.
 */
void
RS_CS2Host_ScriptStarted(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int component_id);

/**
 * Take the colour row waiting for a picker, if any.
 *
 * One shot: the App opens its picker on the request and owns the row from
 * there, so a second frame with nothing new to report answers false rather
 * than re-opening what is already up.
 */
bool
RS_CS2Host_TakeSettingsColourRequest(
    struct RS_CS2Host* host,
    struct RS_CS2SettingsColourRequest* out);

/** The varp a colour row writes, or -1 when the read hub has not named one
 *  yet. See `settings_colour_varp` for where the answer comes from. */
int
RS_CS2Host_SettingsColourVarp(
    struct RS_CS2Host const* host,
    int setting_id);

/**
 * Drop one scripted entity overlay and the UITree layer it owns.
 *
 * For the App, which is the only thing that can tell that an overlay's subject
 * has left the world -- the script that made it gets no event for that, and an
 * overlay whose npc despawned would otherwise sit in the table forever holding
 * a layer that projects nowhere.
 *
 * A free index is a no-op, matching RS_OverlayDestroy.
 */
void
RS_CS2Host_OverlayReap(struct RS_CS2Host* host, int index);

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

/**
 * Which table a bare CLIENTOPTION id names: device if that table has the id,
 * game otherwise, and -1 when neither does.
 *
 * The reference throws "Unrecognized client option %d" on the -1 case; this
 * client reports and carries on, because a settings id from a newer cache is
 * not a reason to kill the script that mentioned it.
 */
int
RS_CS2Host_ClientOptionKind(int option_id);

/**
 * Whether this option is one the reference keeps on disk.
 *
 * Not every option is device state. Brightness (device option 6) is applied
 * the moment it is set and never written to the preferences file — the
 * reference's option handler calls the gamma helper directly rather than a
 * `class79` setter — so restoring it at boot would be this client inventing
 * persistence the reference does not have. game/rs_prefs.c asks this before
 * writing an entry and before honouring one it read.
 */
int
RS_CS2Host_OptionPersists(
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

/**
 * The interface scale as a percentage, clamped to
 * RS_CS2_UI_SCALE_MIN..RS_CS2_UI_SCALE_MAX. Never 0 — an option table that
 * nothing has written still answers 100 here, so callers can divide by it.
 */
int
RS_CS2Host_UiScalePercent(
    struct RS_CS2Host const* host);

/** The interface presentation filter selected by device option 15. Always one
 *  of RS_CS2_UI_SCALE_MODE_NEAREST..RS_CS2_UI_SCALE_MODE_BICUBIC. */
int
RS_CS2Host_UiScaleMode(
    struct RS_CS2Host const* host);

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
