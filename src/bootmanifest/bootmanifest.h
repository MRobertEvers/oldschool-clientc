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
 *   [net:boot]    rev=<name>  transport=tcp|ws  host=<h>  port=<n>
 *                 ws_host=<h>  ws_port=<n>
 *                 client_version=<n>  rsa_exp=<hex>  rsa_mod=<hex>
 *                 jag_crc=<9 comma-separated int32>
 *                 ws_host/ws_port are where a *browser* reaches the same
 *                 server, which is rarely where the native client dials: a
 *                 page has no TCP, so the web build's sockets are WebSockets
 *                 (see BootManifest_ApplyWebEndpoint). LostCity, for one,
 *                 serves the game on 43594/tcp and upgrades / on its web port.
 *   [features:boot] era=lostcity|osrs|server_routed
 *                 Client-behaviour generation (src/features/features.h): who
 *                 computes a click's route, and which approach model decides
 *                 "close enough to interact". Optional — absent, the era is
 *                 derived from the cache epoch/revision, which is right for
 *                 every cache-only boot. State it when the *server* diverges
 *                 from what the cache implies (xrsps paths server-side over a
 *                 rev-233 cache, so it needs era=server_routed).
 *   [render:light]  Optional overrides for the two model-light regimes and the
 *                 two places xrsps diverges from Client-TS (see
 *                 docs/MODEL_LIGHTING.md). Absent keys keep the compiled-in /
 *                 era-table defaults.
 *                 actor_ambient=<n>  actor_attenuation=<n>
 *                 actor_light=<x>,<y>,<z>
 *                 scene_ambient=<n>  scene_attenuation=<n>
 *                 scene_light=<x>,<y>,<z>
 *                 npc_type_ambient_contrast=0|1
 *                 player_head_ambient=<n>   (0 = leave chathead unlit)
 *   [ui:boot]     logic=cs1|cs2  chrome=revconfig|cache
 *                 revconfig_ui=<path>  revconfig_cache=<path>  interface_id=<n>
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
 *   [spawn:hotkeys]  npc=<id>  obj=<id>  spotanim=<id>
 *                    spotanim_height=<n>  spotanim_delay=<n>
 *                    proj_model=<id>  proj_seq=<id>
 *                 Optional; -1/absent = use the built-in default. Env
 *                 TORIRS_SPAWN_* still overrides (same precedence as
 *                 TORIRS_WORLD_MAP vs [cache:boot] spawn).
 *
 * [cache:boot] epoch/game/revision/quirks are all required. A missing key fails
 * the load with a stated reason (user input, not an internal invariant).
 *
 * Relative path values (dir, revconfig_ui, revconfig_cache) resolve against the
 * directory containing the manifest file; absolute paths pass through.
 *
 * Lifetime: BootManifest_ApplyToConfig points AppConfig string fields straight
 * into this struct's buffers, so a BootManifest handed to App_Init must outlive
 * the App (declare it `static` in main, as main.c does).
 */

struct AppConfig; /* fwd; src/app.h */

/* Void's rev-634 login opens 25 sub-interfaces; leave room to grow. */
#define BOOTMANIFEST_GAMEFRAME_MAX 64

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

struct BootManifest
{
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

    /* [features:boot] — client-behaviour era name; "" = derive from the cache
     * identity (ToriRS_Features_ForCache). */
    char features_era[32];

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
    int chrome;    /* 0 unset, 1 revconfig, 2 cache */
    char revconfig_ui[512];    /* resolved */
    char revconfig_cache[512]; /* resolved */
    int interface_id;          /* 0 = unset */
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

    /* [spawn:hotkeys] — debug spawn-hotkey ids. -1 = unset (built-in default).
     * TORIRS_SPAWN_* env vars still override. */
    int spawn_npc_id;
    int spawn_obj_id;
    int spawn_spotanim_id;
    int spawn_spotanim_height;
    int spawn_spotanim_delay;
    int spawn_proj_model_id;
    int spawn_proj_seq_id;
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
