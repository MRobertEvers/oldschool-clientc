#ifndef SRC_BOOTMANIFEST_BOOTMANIFEST_H
#define SRC_BOOTMANIFEST_BOOTMANIFEST_H

#include <stdint.h>

/*
 * Boot manifest — one INI file collapsing the whole per-generation boot
 * parameterization (cache identity/dir, protocol rev, transport, host:port,
 * login RSA/CRCs/version, revconfig includes, ui logic). See manifest_rs254.ini
 * / manifest_xrsps.ini at the repo root and docs/MULTI_GENERATIONAL_PARITY.md.
 *
 * Schema (house style [type:name] sections, lowercase key=value, ; / # comments):
 *
 *   [cache:boot]  epoch=dat1|dat2  game=rs2|oldschool  revision=<n>
 *                 quirks=none|kronos|void_rs634_no_xteas  dir=<path>  spawn=<x>,<z>
 *   [net:boot]    rev=<name>  transport=tcp|ws|embed  host=<h>  port=<n>
 *                 scripts=<embedded-server compiled script directory>
 *                 ws_host=<h>  ws_port=<n>
 *                 client_version=<n>  rsa_exp=<hex>  rsa_mod=<hex>
 *                 jag_crc=<9 comma-separated int32>
 *                 cheat=<"::" commands, ';'-separated, no leading "::">
 *                 Sent once right after login — the manifest spelling of the
 *                 TORIRS_NET_CHEAT harness hook (env still overrides). What
 *                 lets a manifest state "boot into the Zuk instance" rather
 *                 than every invocation carrying the env var.
 *                 ws_host/ws_port are where a *browser* reaches the same
 *                 server, which is rarely where the native client dials: a
 *                 page has no TCP, so the web build's sockets are WebSockets
 *                 (see BootManifest_ApplyWebEndpoint). LostCity, for one,
 *                 serves the game on 43594/tcp and upgrades / on its web port.
 *   [js5:boot]   enabled=true|false  host=<h>  port=<n>
 *                fallback_port=<n>  revision=<n>
 *                Optional executor-only incremental-cache settings. Omitted
 *                host/port/revision inherit the finalized [net:boot] endpoint
 *                and [cache:boot] revision. fallback_port defaults to 443 only
 *                when the resolved primary is 43594; 0 explicitly disables it.
 *   [features:boot] era=lostcity|osrs|server_routed
 *                 Client-behaviour generation (src/features/features.h): who
 *                 computes a click's route, and which approach model decides
 *                 "close enough to interact". Optional — absent, the era is
 *                 derived from the cache epoch/revision, which is right for
 *                 every cache-only boot. State it when the *server* diverges
 *                 from what the cache implies (xrsps paths server-side over a
 *                 rev-233 cache, so it needs era=server_routed).
 *                 ground_click_nearest=ring3|box10_rect|none
 *                 Per-item override of the era's unreachable-ground-click
 *                 fallback (enum ToriRS_NearestModel): `ring3` is Client-TS's
 *                 3x3 lowest-step-count ring, `box10_rect` the official OSRS
 *                 21x21 search ranked by squared distance to the target rect,
 *                 `none` no fallback at all. Absent keeps the era's. Only the
 *                 client's own routing reads this — under a server-authoritative
 *                 era the server answers the click, so set the same model there
 *                 (mock230: MOCK230_GROUND_CLICK_NEAREST).
 *                 painter_draw_distance=<25..90>
 *                 Painter radius in tiles. Client-TS is fixed at 25. Modern
 *                 OSRS class112.method3959 accepts 25 through 90 inclusive;
 *                 absent keeps the era's value (25 for LostCity, 32 for OSRS).
 *   [render:light]  Optional overrides for the two model-light regimes and the
 *                 two places xrsps diverges from Client-TS (see
 *                 docs/MODEL_LIGHTING.md). Absent keys keep the compiled-in /
 *                 era-table defaults.
 *                 actor_ambient=<n>  actor_attenuation=<n>
 *                 actor_light=<x>,<y>,<z>
 *                 scene_ambient=<n>  scene_attenuation=<n>
 *                 scene_light=<x>,<y>,<z>
 *                 npc_type_ambient_contrast=0|1
 *                 player_head_ambient=<n>   (0 = scene-light like Client-TS)
 *   [ui:boot]     logic=cs1|cs2  chrome=revconfig
 *                 revconfig_ui=<path>  revconfig_cache=<path>  interface_id=<n>
 *                 Every root tree is built by the RevConfig builder — there is
 *                 no second "open interface_id and hope" path. `interface_id` is
 *                 the group a `type=rs_iface` component mounts when it declares
 *                 no `componentno=` of its own, which is how a CS2 gameframe
 *                 gets placed *among* the other root elements instead of being
 *                 the root. (chrome=cache is accepted as a synonym so older
 *                 manifests still load.)
 *   [revconfig:…] Inline RevConfig. Any section the RevConfig loader
 *                 understands can appear in this file under a `revconfig:`
 *                 prefix — `[revconfig:component:world]`,
 *                 `[revconfig:layout:fixed]`, `[revconfig:sprite:…]`, and so on
 *                 — with exactly the syntax the standalone .ini files use. The
 *                 prefix is what keeps the two dialects apart: [ui:gameframe]
 *                 uses free-form keys, several of which (`left`, `top`, …) would
 *                 otherwise read as layout fields. Inline sections load *after*
 *                 revconfig_ui/revconfig_cache, so a manifest can extend a
 *                 shared UI file rather than restate it.
 *   [client:args] arg=<one exact command-line argument>, repeated in argv order
 *                 Optional lower-priority argv parsed after the typed manifest
 *                 fields and before the process command line. The whole value
 *                 after `=` is one token: spaces/backslashes/quotes are literal
 *                 and no shell parsing or expansion occurs. Put comments on
 *                 separate lines. At most 64 arguments. A `--manifest` token
 *                 in option position is rejected by the command-line parser,
 *                 so a manifest cannot recursively replace itself; the same
 *                 token is still legal when consumed as an option value.
 *   [ui:gameframe]  <component>=<interface_id> or
 *                 <parent_interface>:<component>=<interface_id>, one per line
 *                 Sub-interfaces to mount into component slots once the tree is
 *                 up — the offline stand-in for the IF_OPENSUB burst a server
 *                 sends at login. The bare form targets the root interface; the
 *                 qualified form targets a slot on an interface mounted by an
 *                 earlier line (mounts apply in file order). Ignored when a live
 *                 connection is in play, since the server sends the real thing.
 *   [ui:varc]     <varc_id>=<value>, one per line
 *                 Client vars to seed before the root's scripts run — the same
 *                 stand-in, for the var writes that accompany that burst (which
 *                 tab is selected, which chat filters are on). Also skipped when
 *                 networked.
 *   [action:<name>] t=<hardcoded target>  a=<optional argument payload>
 *                 Declares a named developer action. Supported targets are
 *                 camera_forward/back/left/right/up/down, camera_unlock,
 *                 world_reload, paint_toggle/more/less/more_100/less_100,
 *                 spawn_player/npc/obj/projectile/spotanim, entity_spotanim,
 *                 damage_test, and debug_overlay. `a=` is retained verbatim
 *                 and uses target-specific comma-separated named arguments.
 *                 Spawn targets accept `id=<n>` (npc/obj),
 *                 `id=<n>,height=<n>,delay=<n>` (spotanim/entity_spotanim),
 *                 or `model=<n>,seq=<n>` (projectile). TORIRS_SPAWN_* env
 *                 variables still override matching action arguments.
 *   [debug:hotkeys] <key>=<action name>
 *                 Binds letters, digits, or named keys such as comma to a
 *                 declared action. With no bindings, developer hotkeys are off.
 *
 * [cache:boot] epoch/game/revision/quirks are all required. A missing key fails
 * the load with a stated reason (user input, not an internal invariant).
 *
 * Relative path values (dir, revconfig_ui, revconfig_cache) resolve against the
 * directory containing the manifest file; absolute paths pass through. Values
 * in [client:args] retain command-line semantics and are not path-resolved.
 *
 * Lifetime: BootManifest_ApplyToConfig and main's [client:args] pass can point
 * AppConfig string fields straight into this struct's buffers, so a
 * BootManifest handed to App_Init must outlive the App (declare it `static` in
 * main, as main.c does).
 */

struct AppConfig; /* fwd; src/app.h */
struct ToriRS_ExecutorConfig; /* fwd; src/executor_config.h */

/* Void's rev-634 login opens 25 sub-interfaces; leave room to grow. */
#define BOOTMANIFEST_GAMEFRAME_MAX 64

/* INIElement values hold at most 511 bytes plus NUL. Sixty-four tokens covers
 * every current boot option while keeping storage fixed-size and valid for XP
 * and browser builds. */
#define BOOTMANIFEST_CLIENT_ARG_MAX 64
#define BOOTMANIFEST_CLIENT_ARG_CAP 512
#define BOOTMANIFEST_DEBUG_ACTION_MAX 64
#define BOOTMANIFEST_DEBUG_HOTKEY_MAX 64
#define BOOTMANIFEST_DEBUG_NAME_CAP 64
#define BOOTMANIFEST_DEBUG_ARGS_CAP 512

/** One `[ui:gameframe]` entry: mount `interface_id` into component slot
 *  `component` of `parent_interface_id` (0 = the root interface). */
struct BootManifestGameframeMount
{
    int parent_interface_id;
    int component;
    int interface_id;
};

/** One `[ui:varc]` entry: seed client var `id` with `value`. */
struct BootManifestVarcSeed
{
    int id;
    int value;
};

struct BootManifestDebugAction
{
    char name[BOOTMANIFEST_DEBUG_NAME_CAP];
    int target; /* enum AppDebugHotkey, -1 until t= is parsed */
    char args[BOOTMANIFEST_DEBUG_ARGS_CAP];
};

struct BootManifestDebugHotkey
{
    int key; /* enum LibToriRS_KeyCode */
    char action[BOOTMANIFEST_DEBUG_NAME_CAP];
};

struct BootManifest
{
    /* [client:args]: lower-priority command-line layer, in file order. */
    char client_args[BOOTMANIFEST_CLIENT_ARG_MAX][BOOTMANIFEST_CLIENT_ARG_CAP];
    int client_arg_count;
    int client_args_error; /* argument-count overflow */

    /* [cache:boot] — identity (all four required) */
    int cache_game;      /* enum RSCache_Game; UNSET until parsed */
    int cache_epoch;     /* enum RSCache_Epoch; UNSET until parsed */
    int cache_revision;  /* game revision; -1 = unset */
    uint32_t cache_quirks;
    int cache_quirks_set; /* 1 when quirks= was present */
    int cache_kind;      /* enum AppCacheKind derived from epoch; -1 = unset */
    char cache_dir[512]; /* resolved against manifest dir */
    /* Map square to spawn on, "x,z". Both -1 = unset (client default 50,50).
     * Needed because the default is not universally loadable: a keyed cache
     * ships XTEA keys only for the squares it was dumped with, and cache.643
     * has no key for 50,50. */
    int spawn_x;
    int spawn_z;

    /* [net:boot] */
    char rev_name[32];
    char transport[16]; /* "tcp" | "ws"; "" = unset */
    char host[128];
    int port; /* 0 = unset */
    /* Where a browser reaches the same server; "" / 0 = fall back to host/port.
     * Only the web build looks at these — see BootManifest_ApplyWebEndpoint. */
    char ws_host[128];
    int ws_port;
    int client_version; /* 0 = unset; login-block only, not cache identity */
    char rsa_exp[512];
    char rsa_mod[512];
    int32_t jag_crc[9];
    int jag_crc_set;
    /* Login credentials. A dev server auto-creates the account, so carrying them
     * here is what lets `--manifest <file>` be the whole invocation; without
     * them the client falls back to "guest" with an empty password and the
     * server answers "invalid username or password". --user/--pass still win. */
    char user[64];
    char pass[64];
    /* Compiled script pack for the embedded mock server. Relative paths are
     * resolved against the manifest directory; "" keeps the server default. */
    char server_scripts[512];
    /* "::" commands (';'-separated) to send once right after login; "" = none.
     * TORIRS_NET_CHEAT still overrides. */
    char cheat[256];

    /* [js5:boot] -- executor-owned, never copied into AppConfig. */
    int js5_enabled; /* -1 = unset, otherwise 0|1 */
    char js5_host[128];
    int js5_port; /* 0 = unset/inherit */
    int js5_fallback_port;
    int js5_fallback_port_set; /* distinguishes omitted from explicit 0 */
    int js5_revision;          /* 0 = unset/inherit */
    int js5_revision_set;

    /* [features:boot] — client-behaviour era name; "" = derive from the cache
     * identity (ToriRS_Features_ForCache). */
    char features_era[32];
    /* [features:boot] ground_click_nearest — enum ToriRS_NearestModel, or -1
     * for "not stated, keep the era's". Defaulted in BootManifest_Init. */
    int features_ground_click_nearest;
    /* [features:boot] painter_draw_distance, or 0 for "not stated". */
    int features_painter_draw_distance;
    /* [features:boot] ground_click_unbounded / ground_click_offmap — the two
     * permissive ground-click extensions (features.h), 0/1, or -1 for "not
     * stated". Every era table leaves both off, so these keys are how a boot
     * asks for behaviour the reference does not have. */
    int features_ground_click_unbounded;
    int features_ground_click_offmap;

    /* [render:light] — optional overrides. Each *_set flag is 1 when the key
     * was present; App_Init merges set fields over the era/compiled defaults. */
    int actor_ambient;
    int actor_attenuation;
    int actor_light_x;
    int actor_light_y;
    int actor_light_z;
    int actor_ambient_set;
    int actor_attenuation_set;
    int actor_light_set;
    int scene_ambient;
    int scene_attenuation;
    int scene_light_x;
    int scene_light_y;
    int scene_light_z;
    int scene_ambient_set;
    int scene_attenuation_set;
    int scene_light_set;
    int npc_type_ambient_contrast; /* 0|1 */
    int npc_type_ambient_contrast_set;
    int player_head_ambient;
    int player_head_ambient_set;

    /* [ui:boot] */
    int ui_logic;  /* enum AppUiLogic; 0 = unset/default */
    int chrome;    /* 0 unset, 1 revconfig, 2 cache (legacy synonym) */
    char revconfig_ui[512];    /* resolved */
    char revconfig_cache[512]; /* resolved */
    int interface_id;          /* 0 = unset */
    /* Path this manifest was loaded from, kept so the RevConfig builder can read
     * the file's own `[revconfig:…]` sections back. Set only when at least one
     * such section was seen — an empty string means "no inline RevConfig". */
    char revconfig_inline[512];
    /* `windowmode = fixed|resizable` — enum CS2VM_WindowMode, 0 = unset.
     * Declared rather than derived: which of the two the client boots in is a
     * display preference, and the client has nowhere else to keep one (there is
     * no settings save, and the cache's own dropdown script is unbound). */
    int window_mode;
    /* `window = WxH` — initial canvas/window size. 0 = unset (the fixed frame).
     * Clamped to the canvas floor by App_SetCanvasSize like any other size. */
    int window_w;
    int window_h;

    /*
     * [ui:chatbox] — where the chat lines live, for revisions whose chatbox is
     * widgets rather than a surface the client paints (see rs_chat_widgets.h).
     *
     * Declared rather than derived. The alternative was recognising the chatbox
     * by shape — "a scrolling layer with a few hundred identical text children"
     * — which would be a heuristic sitting between the player and every message
     * the server sends. `interface = 0` means this revision has no widget
     * chatbox, which is the correct answer for every dat1 tree.
     */
    int chatbox_interface;
    int chatbox_messages;   /* scrolling layer child */
    int chatbox_first_line; /* first line component child */
    int chatbox_line_count;
    int chatbox_input;       /* typed-input line child; -1 = none */
    int chatbox_line_height; /* 0 = the 14px the components declare */

    /* [ui:gameframe] — component slot -> interface id, in file order. */
    struct BootManifestGameframeMount gameframe[BOOTMANIFEST_GAMEFRAME_MAX];
    int gameframe_count;

    /* [ui:varc] — client var id -> initial value, in file order. */
    struct BootManifestVarcSeed varc[BOOTMANIFEST_GAMEFRAME_MAX];
    int varc_count;

    /* `[action:<name>]` declarations and `[debug:hotkeys]` bindings. */
    struct BootManifestDebugAction debug_actions[BOOTMANIFEST_DEBUG_ACTION_MAX];
    int debug_action_count;
    struct BootManifestDebugHotkey debug_hotkeys[BOOTMANIFEST_DEBUG_HOTKEY_MAX];
    int debug_hotkey_count;
    int debug_hotkey_error;
};

/* Zero the manifest and load `path`. Relative paths resolve against
 * dirname(path). Returns 0 on success, <0 on read/parse failure or a missing
 * required [cache:boot] identity key (a stderr line names the problem). */
int
BootManifest_LoadFile(struct BootManifest* bm, char const* path);

/* Copy the manifest's set fields into cfg. Only fields the manifest actually
 * provided are written, so calling this before CLI flag parsing lets explicit
 * flags override by plain assignment (precedence: CLI > manifest > defaults). */
void
BootManifest_ApplyToConfig(struct BootManifest const* bm, struct AppConfig* cfg);

/* Copy only set [js5:boot] fields into the executor-owned configuration.
 * This is intentionally separate from ApplyToConfig: AppConfig is the stable
 * core-client boundary and must not acquire JS5/network-cache state. */
void
BootManifest_ApplyToExecutorConfig(
    struct BootManifest const* bm,
    struct ToriRS_ExecutorConfig* cfg);

/*
 * Repoint cfg at the endpoint a browser can reach, for hosts whose sockets are
 * WebSockets (the emscripten build). Call after BootManifest_ApplyToConfig and
 * before CLI parsing, so --connect/--port still win.
 *
 * Precedence within this step: TORIRS_WS_HOST / TORIRS_WS_PORT, then the
 * manifest's ws_host / ws_port, then whatever ApplyToConfig already set. The
 * env pair is what lets one manifest serve a server on a non-default web port
 * without editing it.
 *
 * Platform-free on purpose — the caller decides it applies, so this is testable
 * on any host.
 */
void
BootManifest_ApplyWebEndpoint(struct BootManifest const* bm, struct AppConfig* cfg);

#endif
