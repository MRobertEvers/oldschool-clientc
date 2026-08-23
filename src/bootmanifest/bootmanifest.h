#ifndef SRC_BOOTMANIFEST_BOOTMANIFEST_H
#define SRC_BOOTMANIFEST_BOOTMANIFEST_H

#include <stdint.h>

/*
 * Boot manifest — one INI file collapsing the whole per-generation boot
 * parameterization (cache identity/dir, protocol rev, transport, host:port,
 * login RSA/CRCs/version, revconfig includes, ui logic). See manifests/manifest_rs254lc.ini
 * / manifests/manifest_osrs233xrsps.ini at the repo root and docs/MULTI_GENERATIONAL_PARITY.md.
 *
 * Schema (house style [type:name] sections, lowercase key=value, ; / # comments):
 *
 *   [cache:boot]  epoch=dat1|dat2  game=rs2|oldschool  revision=<n>
 *                 source=disk|ondemand
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
 *   [editor:boot] content_dir=<path>  repo_root=<path>
 *                server=embed|tcp  host=<h>  port=<n>  client=<n>
 *                panel=inprocess|tab
 *                Turn on the world map editor. The editor is a client of the
 *                ToriRSMapEd server: `server=embed` (default) hosts it in
 *                this process over `content_dir`, `server=tcp` dials a
 *                torirsmaped daemon — the editor's counterpart of [net:boot]
 *                choosing the game server. Stating `content_dir` (or
 *                server=tcp) is what enables it; `repo_root` additionally
 *                allows baking, which only ever runs when the user asks for it.
 *
 *   [features:boot] era=lostcity|osrs|server_routed
 *                 Client-behaviour generation (src/features/features.h): who
 *                 computes a click's route, and which approach model decides
 *                 "close enough to interact". Optional, and usually absent:
 *                 the revision profile states this now, as `[features]` in its
 *                 RevConfig (see src/revconfig/revconfig_profile.h), and a
 *                 profile is shared by every world that boots it. What belongs
 *                 HERE is only what is true of one WORLD — above all a *server*
 *                 that diverges from what the cache implies (xrsps paths
 *                 server-side over a rev-233 cache, so it needs
 *                 era=server_routed), or a lane whose mover is not its
 *                 lineage's. This block overrides the profile, which in turn
 *                 overrides ToriRS_Features_ForCache.
 *                 ground_click_nearest=ring3|box10_rect|none
 *                 Per-item override of the era's unreachable-ground-click
 *                 fallback (enum ToriRS_NearestModel): `ring3` is Client-TS's
 *                 3x3 lowest-step-count ring, `box10_rect` the official OSRS
 *                 21x21 search ranked by squared distance to the target rect,
 *                 `none` no fallback at all. Absent keeps the era's. Only the
 *                 client's own routing reads this — under a server-authoritative
 *                 era the server answers the click, so set the same model there
 *                 (torirsserver: TORIRSSERVER_GROUND_CLICK_NEAREST).
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
 *                 damage_test, debug_overlay, loc_editor_toggle and
 *                 hover_footprint. `a=` is retained verbatim
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
/* Content lanes named by `[content:lanes]`. Matches SSC_LANE_MAX, the number
 * sscompile will accept in one build. */
#define BOOTMANIFEST_LANE_MAX 32
#define BOOTMANIFEST_LANE_CAP 128
/** Which ToriRSMapEd deployment `[editor:boot] server=` names. */
enum BootManifestEditorServer
{
    /**
     * ToriRSMapEd runs inside this process; the wire is a pair of in-memory
     * queues. The default, and the only deployment that needs nothing
     * outside this binary — the analogue of `[net:boot] transport=embed`.
     */
    BOOTMANIFEST_EDITOR_SERVER_EMBED = 0,
    /** The torirsmaped daemon, reached over TCP (`host=`/`port=`). */
    BOOTMANIFEST_EDITOR_SERVER_TCP
};

/** Where `[editor:boot] panel=` puts the command panel. */
enum BootManifestEditorPanel
{
    /**
     * Rows in the renderer's own window, drawn by ToriRSChrome. The default,
     * and the only binding that needs nothing outside this process.
     */
    BOOTMANIFEST_EDITOR_PANEL_INPROCESS = 0,
    /**
     * Web only: a second browser tab running panel.html, talking to the canvas
     * tab over torirs_channel.js. Rejected on a native boot rather than
     * silently ignored -- a native binary has no tab to open, and a manifest
     * that asks for one is stating an intent this build cannot honour.
     */
    BOOTMANIFEST_EDITOR_PANEL_TAB
};

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
    /* [cache:boot] source — 0 = disk (the default), 1 = ondemand: read the
     * cache off the LostCity server named by [net:boot] instead. */
    int cache_on_demand;
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

    /* [editor:boot] content_dir — the revision content root the map editor
     * edits, the directory holding `maps/`. "" = no editor this boot.
     *
     * Stating a directory is what enables the editor: it edits the `.jm2`/
     * `.jl2` text sources rather than the baked cache, so a content root is not
     * one of its options, it is the thing it operates on. */
    char editor_content_dir[512];
    /* [editor:boot] repo_root — where a bake runs from. "" disables baking,
     * which is the right default for a look-only session. Baking is never a
     * side effect of saving; it happens only when the user asks. */
    char editor_repo_root[512];

    /**
     * Which ToriRSMapEd the editor session talks to (`[editor:boot] server=`).
     *
     * The editor's counterpart of `[net:boot]` choosing the game server: the
     * client is one binary, and the manifest decides which server a boot
     * connects to. `embed` (the default) hosts ToriRSMapEd in-process over
     * `content_dir`; `tcp` dials a torirsmaped daemon at `host`/`port`, and
     * the DAEMON's content tree is then the one being edited — `content_dir`
     * here only enables the editor and labels the session.
     */
    enum BootManifestEditorServer editor_server;
    /** Set when `server=` named something unknown; the load fails on it. */
    int editor_server_error;
    /* [editor:boot] host/port — where the torirsmaped daemon listens.
     * "" / 0 = localhost / TORIRSMAPED_DEFAULT_PORT. Only server=tcp reads
     * them. */
    char editor_server_host[128];
    int editor_server_port;
    /* [editor:boot] client — the Client (session group) to join, 0 = new.
     * How a second PROCESS joins a running session: the first prints its id
     * at boot, this states it. Meaningful for server=tcp only. */
    int editor_client_id;

    /**
     * Where the command panel is drawn (`[editor:boot] panel=`).
     *
     * A boot-time choice rather than a runtime toggle because the panel's
     * binding decides what gets constructed: the in-process panel is rows in
     * the renderer's own ToriRSChrome, and the tab panel is a second browser
     * context that has to be opened before it can be talked to.
     */
    enum BootManifestEditorPanel editor_panel;

    /**
     * `[chrome] executor=` -- enum ToriRSChromeExecKind for the plugin window.
     *
     * Defaults to 0 (buffer, the in-canvas chrome), which is also what a
     * manifest that says nothing means and what every build can provide.
     * TORIRS_CHROME_EXECUTOR overrides it, the way TORIRS_CHROME_THEME
     * overrides the theme beside it.
     */
    int chrome_executor;
    /**
     * The key was present.
     *
     * Distinct from the value, because "unset" and "explicitly buffer" are
     * different instructions: unset means "pick something sensible for this
     * gameframe", and an explicit value means "use this one" -- including when
     * what it names is what would have been picked anyway. Without the flag the
     * default cannot be a default, only a lock.
     */
    int chrome_executor_set;
    /** Set when the key named something that is not an executor at all. */
    int chrome_executor_error;
    /**
     * `[chrome] borderless=` -- open the plugin window with no OS frame, so
     * the panel's own title bar and tab strip are what move it.
     *
     * Only the `sdl` executor has a frame to hide; every other presentation
     * either has no window of its own or builds one out of the host's widgets.
     * Off by default: a frameless window is a look, and one a lane has to ask
     * for. TORIRS_CHROME_BORDERLESS overrides it.
     */
    int chrome_borderless;
    /** Set when `panel=` named something unknown; the load fails on it. */
    int editor_panel_error;

    /* [content:lanes] — which gated content lanes `server_scripts` above was
     * compiled from, one `lane=` per line.
     *
     * Build-time information in a boot file, deliberately: the manifest is what
     * a launcher is handed, and "this profile is the Summoning one" was
     * previously encoded only in the SPELLING of the output directory
     * (`build_summoning`), which run-live.sh pattern-matched to pick a make
     * target. A new lane could not be launched at all without teaching the
     * launcher a new suffix. Naming the lanes here says the same thing where a
     * reader can see it, and lets one generic compile serve every profile.
     *
     * A manifest that names none compiles the tree's default lanes, which is
     * what every profile that is not a lane development profile wants. */
    char lanes[BOOTMANIFEST_LANE_MAX][BOOTMANIFEST_LANE_CAP];
    int lane_count;
    int lanes_error; /* lane-count overflow */
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

    /* [features:boot] — client-behaviour era name; "" = fall through to the
     * revconfig profile's `[features] era`, and then to the cache identity
     * (ToriRS_Features_ForCache). */
    char features_era[32];
    /* [features:boot] ground_click_nearest — enum ToriRS_NearestModel, or -1
     * for "not stated, keep the era's". Defaulted in BootManifest_Init. */
    int features_ground_click_nearest;
    /* [features:boot] painter_draw_distance, or 0 for "not stated". */
    int features_painter_draw_distance;
    /* [features:boot] mover — enum ToriRS_MoverModel, or -1 for "not stated,
     * keep the era's". The key exists because ToriRS_Features_ForCache has no
     * era table for the RS2 lanes and drops them all on lostcity, whose mover
     * is the 2004 one; a lane that is not reproducing the 2004 client says so
     * here. Defaulted in BootManifest_Init. */
    int features_mover_model;
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
    /** `[ui:boot] chrome_scale=`: pin the ToriRSChrome zoom (1..4). 0 = unset,
     *  the chrome follows the display's pixel density. */
    int chrome_scale;
    /** `[ui:boot] chrome_checkbox=`: which of the interfaces' two booleans the
     *  chrome's checkboxes wear. 0 = unset, 1 = `tick` (the settings page's
     *  tick/cross), 2 = `box` (the bordered well). One past
     *  enum ToriRSChromeCheckStyle so unset is distinguishable from the
     *  default; BootManifest_Apply subtracts the one. */
    int chrome_checkbox;
    /** `[ui:boot] hidpi=`: render into a device-pixel drawable. 0 = unset, 1 =
     *  on, -1 = explicitly off. Unset keeps the platform default, which is ON
     *  everywhere but the web lane -- so this key exists to DECLINE HighDPI on
     *  a machine whose renderer cannot afford 4x the pixels, not to ask for it.
     *  TORIRS_HIDPI overrides. */
    int hidpi;
    /* `window = WxH` — initial canvas/window size. 0 = unset (the fixed frame).
     * Clamped to the canvas floor by App_SetCanvasSize like any other size. */
    int window_w;
    int window_h;

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
