#ifndef SRC_APP_H
#define SRC_APP_H

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/uitree_anim.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_builder/uitree_builder.h"
#include "engine/uitree_scene_bridge.h"
#include "engine/torirs_model_inst_cache.h"
#include "features/features.h"
#include "game/rs_audio.h"
#include "game/rs_chat.h"
#include "net/rev/revpacket.h"
#include "game/rs_chat_widgets.h"
#include "game/rs_cs1_host.h"
#include "game/rs_cs2_host.h"
#include "game/rs_entity_sync.h"
#include "game/rs_idk_design.h"
#include "game/rs_if1_buttons.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_player_stats.h"
#include "game/rs_prefs.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "input/torirs_input.h"
#include "inv/inv_manager.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"
#include "toridraw_scene.h"
#include "ui/uitree.h"
#include "ui/uitree_cross.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_hovertext.h"
#include "ui/uitree_interact.h"
#include "varc/varc_manager.h"
#include "game/rs_loot_store.h"
#include "game/rs_hitsplat.h"
#include "game/rs_soundscape.h"
#include "varp/varp_manager.h"
#include "world/world_pickset.h"

struct ToriRS_Frame;
struct ToriRS_PickHits;
struct PktRunClientScript;

#include <stdint.h>

struct World;
struct WorldBuilder;
struct PaintersBuffer;
struct RSCache_Dat1Disk;
struct Dat1BuildCache;
struct Dat2BuildCache;
struct ToriRS_CmdBus;
struct ToriRS_Network;
struct PktNpcInfoOp;
struct PktPlayerInfoOp;

/*
 * Application shell: owns every subsystem and the update loop body, with no
 * platform (SDL) dependency so it compiles headless for tests and the future
 * WASM shell. The platform layer polls input, calls App_RunOnce once per
 * frame, and blits App_Render's pixels.
 */

/* Which on-disk cache format cache_dir holds. dat2 is the js5-era cache
 * (main_file_cache.dat2 + reference tables); dat1 is the 317/254-era one
 * (main_file_cache.dat + jagfile archives). They differ in every decoder, so
 * the whole asset pipeline is selected from this. */
enum AppCacheKind
{
    APP_CACHE_DAT2 = 0,
    APP_CACHE_DAT1 = 1,
};

/* Which interface-logic VM drives the UI. DEFAULT derives from cache_kind
 * (dat1 -> CS1, dat2 -> CS2) so a boot with no manifest behaves exactly as
 * before; a manifest can name it explicitly (`[ui:boot] logic=cs1|cs2`) to
 * decouple it from the cache format. */
enum AppUiLogic
{
    APP_UI_LOGIC_DEFAULT = 0,
    APP_UI_LOGIC_CS1 = 1,
    APP_UI_LOGIC_CS2 = 2,
};

/* Hardcoded targets named by manifest `[action:<name>]` declarations. The
 * manifest's `[debug:hotkeys]` section binds keys to those named actions;
 * nothing is bound in a zero-initialized config. Arrow-key orbit controls are
 * game input rather than developer shortcuts and are intentionally absent. */
enum AppDebugHotkey
{
    APP_DEBUG_HOTKEY_CAMERA_FORWARD,
    APP_DEBUG_HOTKEY_CAMERA_BACK,
    APP_DEBUG_HOTKEY_CAMERA_LEFT,
    APP_DEBUG_HOTKEY_CAMERA_RIGHT,
    APP_DEBUG_HOTKEY_CAMERA_UP,
    APP_DEBUG_HOTKEY_CAMERA_DOWN,
    APP_DEBUG_HOTKEY_CAMERA_UNLOCK,
    APP_DEBUG_HOTKEY_WORLD_RELOAD,
    APP_DEBUG_HOTKEY_PAINT_TOGGLE,
    APP_DEBUG_HOTKEY_PAINT_MORE,
    APP_DEBUG_HOTKEY_PAINT_LESS,
    APP_DEBUG_HOTKEY_PAINT_MORE_100,
    APP_DEBUG_HOTKEY_PAINT_LESS_100,
    APP_DEBUG_HOTKEY_SPAWN_PLAYER,
    APP_DEBUG_HOTKEY_SPAWN_NPC,
    APP_DEBUG_HOTKEY_SPAWN_OBJ,
    APP_DEBUG_HOTKEY_SPAWN_PROJECTILE,
    APP_DEBUG_HOTKEY_SPAWN_SPOTANIM,
    APP_DEBUG_HOTKEY_ENTITY_SPOTANIM,
    APP_DEBUG_HOTKEY_DAMAGE_TEST,
    APP_DEBUG_HOTKEY_DEBUG_OVERLAY,
    APP_DEBUG_HOTKEY_LOC_EDITOR,
    APP_DEBUG_HOTKEY_COUNT
};

#define APP_DEBUG_HOTKEY_MAX 64
#define APP_DEBUG_HOTKEY_ARGS_CAP 512

struct AppDebugHotkeyBinding
{
    enum LibToriRS_KeyCode key;
    enum AppDebugHotkey target;
    char args[APP_DEBUG_HOTKEY_ARGS_CAP];
};

struct AppConfig
{
    char const* cache_dir;
    char const* config_dir;
    char const* script_dir;
    int interface_id;
    enum AppCacheKind cache_kind;
    /** Cache identity from [cache:boot]. All four stated; used by
     *  RSCache_ProfileForIdentity. cache_identity_set is 0 until ApplyToConfig. */
    int cache_game;
    int cache_epoch;
    int cache_revision;
    uint32_t cache_quirks;
    int cache_identity_set;
    /** Map square to spawn on when nothing else selects one. Both -1 = use the client
     *  default (50,50). Set from the manifest `[cache:boot] spawn`; TORIRS_WORLD_MAP still
     *  overrides it, and a server REBUILD_NORMAL overrides both.
     *
     *  This exists because 50,50 is not universally loadable: a keyed cache carries XTEA
     *  keys only for the squares it was dumped with, and an unkeyed square on a keyed
     *  cache yields terrain (which is not encrypted) with **zero locs**. */
    int spawn_x;
    int spawn_z;
    /** Resolved `[debug:hotkeys]` key -> `[action:<name>]` bindings. Empty by
     *  default. `args` retains the action's optional `a=` payload. */
    struct AppDebugHotkeyBinding debug_hotkeys[APP_DEBUG_HOTKEY_MAX];
    int debug_hotkey_count;
    /** RevConfig layout INI — the tree's shape. Every root tree comes from the
     * RevConfig builder; a cache gameframe is one *element* of that layout
     * (`type=rs_iface`), not a competing root. NULL/"" = none, in which case the
     * builder synthesises the single-element layout that mounts interface_id. */
    char const* revconfig_ui_ini;
    /** Companion RevConfig sprite/font INI. NULL/"" = none. */
    char const* revconfig_cache_ini;
    /** File carrying inline `[revconfig:…]` sections — in practice the boot
     * manifest itself. Loaded after the two above, so it extends them. */
    char const* revconfig_inline_ini;
    /** `[ui:gameframe]` — sub-interfaces to mount into the root's component
     *  slots once the tree is built, in order.
     *
     *  A gameframe root is a set of empty holders: the panels that fill it arrive
     *  as an IF_OPENSUB burst at login, so an offline client shows chrome with
     *  nothing in it. This is that burst, stated in the manifest. Points into the
     *  BootManifest, which must outlive the App. NULL/0 = none. */
    struct BootManifestGameframeMount const* gameframe_mounts;
    int gameframe_mount_count;
    /** `[ui:varc]` — client vars to seed before the root's scripts run.
     *
     *  The IF_OPENSUB burst arrives alongside var writes; the gameframe's own
     *  scripts branch on them (which sidebar tab is selected, which chat filters
     *  are lit), so mounting without them shows every panel hidden. Points into
     *  the BootManifest. NULL/0 = none. */
    struct BootManifestVarcSeed const* varc_seeds;
    int varc_seed_count;
    /** `[ui:chatbox]` — where this revision's chat lines live, when the chatbox
     *  is widgets rather than a surface. See rs_chat_widgets.h. Zeroed (and so
     *  disabled) for every revconfig-chrome revision. */
    struct RS_ChatWidgetLayout chatbox;
    /** --connect target "host[:port]". NULL/"" = offline (no networking). */
    char const* connect_target;
    char const* connect_user;
    char const* connect_pass;
    /** `[net:boot] scripts` — compiled script pack for the embedded mock
     * server. MOCK230_SCRIPTS still overrides it. NULL/"" = server default. */
    char const* net_server_scripts;
    /** `[net:boot] cheat` — "::" commands (';'-separated, no leading "::") to
     * send once right after login, e.g. "zuk" to enter the Inferno instance.
     * The manifest spelling of the TORIRS_NET_CHEAT harness hook; the env var
     * still overrides. NULL/"" = none. */
    char const* net_cheat;
    /** Protocol revision name ("lc254", "lc245_2"). NULL = lc254, the
     * authoritative LostCity_Server build. Mock/loopback tests pass
     * lc245_2 explicitly. */
    char const* rev_name;
    /** Game-socket port. 0 = 43594 (legacy default). Set from the manifest
     * `[net:boot] port` or --port; --connect "host:port" also fills it. */
    int connect_port;
    /** Login-block RSA key pair (hex). NULL = built-in lc245_2/254 default.
     * Env TORIRS_RSA_EXP/MOD > these > built-in. */
    char const* rsa_exp;
    char const* rsa_mod;
    /** Login-block jag-archive CRCs. Only consulted when jag_crc_set and the
     * env TORIRS_JAG_CRC is absent (env wins). */
    int32_t jag_crc[9];
    int jag_crc_set;
    /** Protocol client version sent in the login block. 0 = rev-table default. */
    int client_version;
    /** enum AppUiLogic. 0 (DEFAULT) = derive from cache_kind. */
    int ui_logic;
    /** Client-behaviour era name from `[features:boot] era` — "lostcity",
     * "osrs" or "server_routed" (src/features/features.h). NULL/"" = derive
     * from the cache identity; TORIRS_FEATURES_ERA still overrides. */
    char const* features_era;
    /** `[features:boot] ground_click_nearest` — enum ToriRS_NearestModel, only
     * read when the _set flag is 1 (0 is a real model, RING3_STEPS). Overrides
     * the era table's ground_click_nearest_model; TORIRS_GROUND_CLICK_NEAREST
     * overrides both. */
    int features_ground_click_nearest;
    int features_ground_click_nearest_set;
    /** `[features:boot] ground_click_unbounded` / `ground_click_offmap` — the
     * two permissive ground-click extensions (features.h). Both are 0 in every
     * era table because the client is deob-exact by default; these keys, and
     * TORIRS_GROUND_CLICK_UNBOUNDED / TORIRS_GROUND_CLICK_OFFMAP, are the only
     * way to turn one on. -1 = not stated. */
    int features_ground_click_unbounded;
    int features_ground_click_offmap;
    /** `[features:boot] painter_draw_distance`, in the official OSRS 25..90
     * tile interval. Absent keeps Client-TS's fixed 25-tile radius;
     * TORIRS_DRAW_DISTANCE overrides it at runtime. */
    int features_painter_draw_distance;
    int features_painter_draw_distance_set;
    /** `[render:light]` overrides. Each *_set flag is 1 when the manifest key
     * was present; App_Init merges set fields over era/compiled defaults. */
    int light_actor_ambient;
    int light_actor_attenuation;
    int light_actor_x;
    int light_actor_y;
    int light_actor_z;
    int light_actor_ambient_set;
    int light_actor_attenuation_set;
    int light_actor_set;
    int light_scene_ambient;
    int light_scene_attenuation;
    int light_scene_x;
    int light_scene_y;
    int light_scene_z;
    int light_scene_ambient_set;
    int light_scene_attenuation_set;
    int light_scene_set;
    int light_npc_type_ambient_contrast;
    int light_npc_type_ambient_contrast_set;
    int light_player_head_ambient;
    int light_player_head_ambient_set;
    /** `[ui:boot] windowmode` — enum CS2VM_WindowMode, 0 = unset (keep the
     * host's own default). Fixed letterboxes a 765x503 canvas into whatever
     * window it is given; resizable lays the gameframe out at the window size.
     * --windowmode overrides. */
    int window_mode;
    /** `[ui:boot] window` — initial canvas AND window size, 0 = unset (the
     * 765x503 fixed frame). Only meaningful in resizable mode, where it is the
     * size the gameframe is laid out at before the user touches anything.
     * --window overrides, and TORIRS_ROOT_SIZE overrides both. */
    int window_w;
    int window_h;
};

/** App boot lifecycle: BOOTING until the root-interface build task (and its
 * dependent loads) complete through the per-frame task pump; the render path
 * draws a loading bar until READY. */
enum AppState
{
    APP_STATE_BOOTING = 0,
    APP_STATE_READY,
};

/* One deferred element<->sequence binding (animation still loading). */
struct AppSeqBindPending
{
    int element_id;
    int seq_id;
    /** World/client cycle on which LOC_ANIM requested the sequence. Async
     * loading must not reset a DynamicObject's clock when the bind lands. */
    int start_cycle;
};

/** One visible map surface region, with its distance from the view centre. */
struct App_WorldMapVisit
{
    int region_x;
    int region_y;
    int distance;
};

enum ToriRS_WorldRenderMode
{
    TORIRS_WORLD_PAINTER = 0,
    TORIRS_WORLD_DEPTH = 1,
};

/** Frames the developer overlay's frame-time readout averages over. */
#define APP_DEBUG_FRAME_SAMPLES 10

/**
 * RUNCLIENTSCRIPT payloads one server tick may push before the fence.
 *
 * Measured rather than guessed: the busiest tick in this tree is a panel open
 * (`~pricechecker_open` pushes two, a bank open pushes six), and login's burst
 * is the outlier at just under twenty. 64 leaves that room; past it the script
 * runs immediately, so the cap costs ordering and never a script.
 */
#define APP_PENDING_CLIENTSCRIPT_MAX 64

/**
 * Logic cycles a held clientscript may wait for a fence that never comes.
 *
 * A server tick is 600ms against a 20ms logic cycle, so 30 cycles is one whole
 * tick — long enough that a healthy connection never reaches it, short enough
 * that a tick truncated by a disconnect costs one tick of delay rather than the
 * script.
 */
#define APP_CLIENTSCRIPT_FENCE_MAX_CYCLES 30

/**
 * Logic cycles a settings change waits before it is written to disk.
 *
 * A slider drag reports a new volume every 20ms cycle, so writing on each one
 * would be fifty file rewrites a second for one gesture. Half a second of quiet
 * collapses a drag into a single write and is still far shorter than the time
 * between changing a setting and quitting; App_Shutdown flushes the tail
 * either way.
 */
#define APP_PREFS_SAVE_SETTLE_TICKS 25

struct App
{
    struct AppConfig cfg;

    /* Phase 1: task runtime + disk (created first, freed last). Exactly one of
     * the two disks is live, per cfg.cache_kind. */
    struct TaskRunner runner; /* owns queue + io + px */
    /** Serial game-action pipeline: per-packet exec tasks and interface slot
     * mounts. Own queue + io slots, SHARED px with `runner`. Head-only
     * execution makes it a strict FIFO — packet application order is
     * preserved across IO yields, and a mount enqueued by a packet runs
     * before the next packet is even popped (the pump only pops a new packet
     * when this queue is idle). */
    struct TaskRunner exec_runner;
    struct RSCache_Dat2Disk* dat2_disk;
    struct RSCache_Dat1Disk* dat1_disk;

    /* Phase 2: asset pipeline. The build cache matching the live disk backs
     * `provider`; everything downstream sees only the provider. */
    struct Dat2BuildCache* dat2_bc;
    struct Dat1BuildCache* dat1_bc;
    struct CacheProvider* provider;
    /** Lit model-instance LRU (spot/npc/loc/obj/player). Cleared at map build. */
    struct TorirsModelInstCache model_inst_cache;
    /**
     * Entity-info decode scratch: one 2048-entry op array per decoder, reused
     * across packets rather than allocated and freed for each. PLAYER_INFO and
     * NPC_INFO both arrive every server tick and each was calloc'ing ~80KB.
     * Borrowed by the running exec task and handed back on its free; NULL until
     * first use. Owned here so teardown reclaims them -- see
     * task_exec_entity_info.c for the borrow/release pair.
     */
    struct PktNpcInfoOp* npc_info_ops_scratch;
    struct PktPlayerInfoOp* player_info_ops_scratch;
    int npc_info_ops_scratch_busy;
    int player_info_ops_scratch_busy;

    /* Phase 3: scene + bridge. */
    struct ToriDraw_Scene* scene;
    struct UITreeSceneBridge bridge;

    /* Phase 4b: world sim + builder (needs provider + scene + varps; the
     * World references assets and scene elements by integer id only). */
    struct World* world;
    struct WorldBuilder* world_builder;
    struct PaintersBuffer* painter_buffer;
    /** Selected only after the platform renderer has initialized successfully. */
    enum ToriRS_WorldRenderMode world_render_mode;
    /** Viewport size remembered for TORIRS_PAINTER_CULL=baked debounce (0 = none). */
    int painter_cullmap_bake_w;
    int painter_cullmap_bake_h;
    struct ToriDraw_Camera world_camera;
    struct ToriDraw_Position world_camera_pos;
    /* Reference orbit camera (Client-TS followCamera): velocity-driven
     * yaw/pitch, a 1/16-eased anchor that trails the player, and a terrain
     * pitch clamp (cameraPitchClamp, 24.8 fixed) that keeps the eye above
     * nearby ground. cam_key_* latch arrow-held state for the follow step,
     * which runs before key sampling in the frame.
     *
     * orbit_x/orbit_z are FLOAT because the reference's are (rev-239
     * client.field917/field879, eased in client.method1605). An integer
     * `anchor += (target - anchor) / 16` never converges: once the gap drops
     * to 15 the truncated step is 0, so the anchor parks a permanent ~15
     * units short on each axis and the camera orbits a point beside the
     * player instead of the player. */
    int orbit_yaw;
    int orbit_pitch;
    int orbit_yaw_vel;
    int orbit_pitch_vel;
    float orbit_x;
    float orbit_z;
    int camera_pitch_clamp;
    int cam_key_left;
    int cam_key_right;
    int cam_key_up;
    int cam_key_down;
    /* Middle-button rotate (revconfig mmb_rotate= on the WORLD element): the
     * press latches inside the viewport rect and keeps the pointer until
     * release, so a drag that wanders over the sidebar keeps rotating.
     * cam_mmb_x/y is the pointer position the last delta was measured from. */
    int cam_mmb_active;
    int cam_mmb_x;
    int cam_mmb_y;
    /* Keys a revconfig hotkey binding acted on this frame, indexed by OSRS key
     * code. Debug world hotkeys share the digit row with the rev-254 tab
     * bindings, so they check this and stand down rather than firing both. */
    uint8_t hotkey_consumed[TORIRS_OSRSKEY_COUNT];
    /* Wheel zoom (revconfig wheel_zoom=), as a percentage of the follow cam's
     * natural orbit distance. 100 = the reference distance; smaller is closer.
     * The free camera dollies instead and ignores this. */
    int world_zoom_pct;
    int world_active; /* 1 once Task_WorldLoad completed */
    /** U toggles: 1 = the follow camera stands down and W/A/S/D + R/F fly
     *  world_camera_pos freely; relocking eases back onto the player (the
     *  follow's own >500-unit teleport snap handles the return). */
    int camera_unlocked;
    /* Latches the lazy load so a map that fails is not re-queued every frame. */
    int world_load_attempted;

    /* Baked world map the minimap widget blits (rebaked on every world load).
     * scene_id is -1 until the first bake; w/h are the sprite's pixel size,
     * which minimap_compute_camera_src_anchor needs to place the camera. */
    int world_map_scene_id;
    int world_map_w;
    int world_map_h;
    /** Terrain level the baked minimap sprite currently shows (reference
     * minimapLevel); a mismatch with the local player's level rebakes it. */
    int world_map_level;

    /* World picking: the full pickset refreshes as part of every rendered
     * frame (App_Render hittests visible models at world_mouse_x/y); click
     * handlers consume the last rendered set. world_emit_desc caches the
     * WORLD node's emit desc — the gate rect and the exact viewport the
     * render pass draws with. */
    struct World_PickSet world_pickset;
    struct UITreeEmitDesc world_emit_desc;
    int world_view_valid;
    /* Minimap widget: cached emit desc (on-screen box + the rotation/anchor
     * the blit drew with) for click-to-walk, and the destination flag tile
     * (scene coords, -1 = none; reference minimapFlagX/Z). */
    struct UITreeEmitDesc minimap_emit_desc;
    int minimap_view_valid;
    int minimap_flag_x;
    int minimap_flag_z;
    /* Per-frame minimap overlay dots, filled by the GET_MINIMAP_DOTS host
     * request during the emit walk and consumed by the same frame's draw. */
    struct UITreeMinimapDot minimap_dots[256];
    int minimap_dot_count;
    /* Per-frame entity overlay primitives (health bars + hitsplats), filled
     * by the GET_ENTITY_OVERLAYS host request and consumed by the same
     * frame's draw. Reference drawEntities budget: each entity contributes at
     * most 2 bar rects + 4 hitsplats x 3 primitives. */
    struct UITreeEntityOverlay entity_overlays[512];
    int entity_overlay_count;
    /* Per-frame world map blits, filled by the GET_WORLDMAP_TILES host request
     * and consumed by the same frame's draw: the visible regions first, then
     * every map element icon over them. A full-screen surface spans ~30 regions
     * and a few hundred icons at the densest zoom. */
    struct UITreeWorldMapTile worldmap_tiles[512];
    int worldmap_tile_count;
    /** Overview pane blit (clientCode 1401): one scaled compositetexture. */
    struct UITreeWorldMapTile worldmap_overview_tile;
    /** Scene id of the uploaded overview texture; 0 until first needed, -1 if
     *  the last upload failed. Replaced when the current area changes. */
    int worldmap_overview_scene_id;
    /** Area id whose compositetexture is currently in the overview scene slot;
     *  -1 when none. */
    int worldmap_overview_area_id;
    /** Baked map-surface regions (src/game/rs_worldmap_render.h). */
    struct RS_WorldMapRender* worldmap_render;
    /* Scene id of the synthesised flash marker drawn behind a flashing icon;
     * 0 until first needed, -1 if it could not be built. See
     * app_worldmap_flash_marker_scene on why it is synthesised and not a
     * cache sprite. */
    int worldmap_flash_scene_id;
    /* Visible regions for this frame, ordered nearest-the-view-centre first —
     * the order decides who gets the frame's bake and load allowance. The
     * lowest zoom over the whole map surface stays well inside this. */
    struct App_WorldMapVisit worldmap_visits[512];
    int worldmap_visit_count;
    /* World map surface box, recorded by the emit walk (the widget is sized by
     * the world map's scripts), and the drag-to-pan grab point. */
    int worldmap_box_x;
    int worldmap_box_y;
    int worldmap_box_w;
    int worldmap_box_h;
    int worldmap_debug_frame;
    int worldmap_drag_active;
    int worldmap_drag_x;
    int worldmap_drag_y;
    /* View position when the drag started: the pan is anchored to it, not
     * accumulated per frame. */
    int worldmap_drag_display_x;
    int worldmap_drag_display_y;
    /* A press that releases without panning is a click on the map, not a drag,
     * so the release has to know whether the view ever moved. */
    int worldmap_drag_moved;
    /** Scene id of the hitmarks sprite pack, resolved once at boot. */
    int hitmarks_scene_id;

    /* Persistent IF_SETTEXT store (reference keeps text on the shared
     * IfType.list config): the server sends journal/bonus texts BEFORE the
     * owning interface mounts, so they must survive and re-apply whenever
     * tree topology changes (tree->generation). */
    struct AppIfText
    {
        int com_id;
        char* text;
    }* if_texts;
    int if_text_count;
    int if_text_cap;
    uint32_t if_text_applied_gen;

    /* Persistent IF_SETHIDE store, same reasoning as if_texts (the reference
     * keeps `hide` on the shared IfType.list, so a hide sent before the owning
     * interface mounts still takes effect once it does). The chat option
     * dialogs ship two sword-decoration layers — a narrow centred pair and a
     * wide corner pair — and the server picks one with IF_SETHIDE right after
     * IF_OPENCHAT; dropping it left the cache default (narrow) on screen. */
    struct AppIfHide
    {
        int com_id;
        int hide;
    }* if_hides;
    int if_hide_count;
    int if_hide_cap;
    uint32_t if_hide_applied_gen;

    /* Persistent IF_SETEVENTS store. At rev 230 nothing is clickable by
     * default — the server declares which slots of which component accept
     * input, and it does so before the interface finishes mounting, so the
     * masks have to survive until there is a tree to apply them to. Without
     * this a dialogue renders correctly and swallows every click. */
    struct AppIfEvents
    {
        int com_id;
        int from;
        int to;
        int events;
    }* if_events;
    int if_event_count;
    int if_event_cap;

    /* Persistent interface-model store (reference keeps
     * model1Type/model1Id on IfType.list and re-resolves getModel every draw):
     * the head packet arrives before the chat interface mounts, so the request
     * must survive and re-apply on tree-topology changes. The async load task
     * makes the composited head scene model available; the per-frame poll binds
     * it onto the MODEL node once mounted (and after each remount). */
    struct AppIfHead
    {
        int com_id;
        int kind;             /* AppIfHeadKind: npc, player, obj, or raw model */
        int npc_id;           /* source id: npc, obj, or cache model */
        int zoom;             /* IF_SETOBJECT wire zoom (modelZoom = zoom2d*100/zoom) */
        int anim_id;          /* IF_SETANIM seq for this head, or -1 (reference modelAnim) */
        uint32_t applied_gen; /* tree generation this was last applied at; 0 = pending */
    }* if_heads;
    int if_head_count;
    int if_head_cap;

    /* Revision-239 server-driven player-composition widgets. Each component
     * owns a clone of the local PlayerComposition: slots is the effective
     * kit/equipment layer, identkit is the body underneath worn objects, and
     * colours/gender are independently mutable through the four
     * IF_SETPLAYERMODEL_* packets. A unique scene id prevents one widget's
     * incremental setters from changing any sibling widget. */
    struct AppIfPlayerModel
    {
        int com_id;
        int scene_id;
        int slots[12];
        int identkit[12];
        int colors[5];
        int gender;
        int anim_id;            /* IF_SETANIM modelAnim, or -1 */
        uint32_t version;       /* composition revision requested by packets */
        uint32_t built_version; /* revision currently uploaded to scene_id */
        uint32_t applied_gen;   /* tree generation scene_id was bound into */
    }* if_player_models;
    int if_player_model_count;
    int if_player_model_cap;

    /* Live local-player model widgets (clientCode 328 — the equipment-stats
     * figure). The composite is rebuilt from the local player's PLAYER_INFO
     * appearance whenever any of it moves, which is the whole point: equip a
     * helmet and the figure is wearing it. The last-built appearance is kept
     * here so an unchanged one is not re-merged every frame; the angles and the
     * animation frame are re-derived unconditionally (app_player_model_poll). */
    struct
    {
        int slots[12];
        int colors[5];
        int gender;
        int built; /* 0 until the first successful composite */
    } player_model;
    int world_mouse_in_viewport;
    int world_mouse_x; /* last input mouse, canvas coords */
    int world_mouse_y;
    int world_hover_tile_x; /* scene tile, -1 = none */
    int world_hover_tile_z;
    int world_hover_tile_level;

    /* Projectile hotkey latch: first press = src tile, second = dst + fire. */
    int proj_src_tile_x; /* -1 = unarmed */
    int proj_src_tile_z;
    int proj_src_tile_level;

    /* RevConfig tree builder: alive for the process, not just the build task —
     * it owns the sprite/font name registries bake resolved through. Unused
     * (builder_active == 0) when the tree came from a cache interface open. */
    struct UITreeBuilder builder;
    int builder_active;

    /* Phase 4: game state. */
    struct UITree* tree;
    struct InvManager invs;
    /* One inv obj-icon reconcile task in flight at a time (per-tick scan
     * re-enqueues while any item slot still lacks a rasterized icon). */
    int inv_icon_reconcile_inflight;
    struct VarPManager varps;
    /* Hitsplat types: type -> sprite id, from config group 32. See
     * src/game/rs_hitsplat.h for why a named sprite archive is not enough. */
    struct RS_Hitsplats hitsplats;
    /** Ambient soundscapes (config group 15). Empty before OldSchool 231, and
     *  the audio layer reads empty as "AMBIENTSOUND_START ids are effect ids". */
    struct RS_Soundscapes soundscapes;
    struct VarCManager varcs;
    struct LootStore loot;
    struct RS_PlayerStats stats;
    struct RS_CS2Host host;
    struct RS_CS1Host cs1_host;
    /** Runtime interface slots (tabs, modals, chat) — reference Client-TS
     *  mainModalId/sideOverlayId/sideTab fields. */
    struct RS_UISlots slots;
    /** Friends/ignores store feeding the clientCode row pass. */
    struct RS_Social social;
    /** Character-design state behind the tutorial design screen's client
     *  codes (parts/colours/gender + the preview's rebuild flag). */
    struct RS_IdkDesign idk_design;
    /** Chat message ring + input state, and its per-frame flattened draw
     *  model (the host hands chat_view to the emit walk). */
    struct RS_Chat chat;
    struct UIChatView chat_view;
    /** Frames the left button has been held over the chat scrollbar (reference
     *  scrollCycle); drives arrow-scroll acceleration and gates grip drag. */
    int chat_scroll_cycle;
    /** Chat input focus. When set, typed keys feed the chat input line and the
     *  manifest-configured debug shortcuts are suppressed so they cannot fire
     *  while composing a message. Clicking the chat region focuses; clicking
     *  elsewhere or pressing Escape unfocuses. */
    int chat_input_active;
    /** Minimenu chat-line seam (points at app_chat_line_at). */
    struct RS_MinimenuChatSource chat_source;
    /** Server-notify callbacks for IF1 button clicks (NULL until net). */
    struct RS_IF1ButtonSink button_sink;
    /** Client ticks since boot (reference loopCycle; drives design preview). */
    uint64_t logic_cycle;
    /** Networking subsystem; NULL until --connect enables it. Owned heap
     *  pointer so app.h need not include the net headers. */
    struct ToriRS_Network* net;
    int net_enabled;

    /*
     * Connection loss and re-establishment (reference `lostCon`, Client-TS
     * Client.ts:2734; deob gameState 40).
     *
     * A session dies in three ways and they need the same handling: the
     * transport reports the socket gone, the server stops speaking for long
     * enough that the reference client gives up (15s), or *this process*
     * stopped running for long enough that the backlog waiting for it is no
     * longer worth draining — a browser tab that was hidden or frozen, a
     * suspended laptop. The third is the one a native client never sees and
     * the one that made a returning tab replay minutes of packets at once.
     *
     * In every case the answer is the reference's: drop the socket, say so on
     * screen, and hand the session back with GAMERECONNECT rather than
     * fast-forwarding through a stale stream.
     */
    /** Wall clock at the last completed App_RunOnce; 0 before the first. */
    uint64_t last_frame_ms;
    /** Wall clock when a server packet last arrived. */
    uint64_t net_last_recv_ms;
    /** Wall clock when we last put bytes on the wire. Drives the NO_TIMEOUT
     * keepalive, which the reference sends only after a full second of
     * outbound silence -- any real packet resets the wait. */
    uint64_t net_last_send_ms;
    /** Wall clock when the first packet of the current session arrived; the
     * origin the TORIRS_NET_DROP_MS test hook measures from. */
    uint64_t net_first_recv_ms;
    /** Non-zero while the connection is gone and being re-established. */
    int net_lost;
    /** Re-establish attempts made since the connection was lost. */
    int net_reconnect_attempts;
    /** Wall clock at which the next attempt may be made. */
    uint64_t net_reconnect_at_ms;
    /** Set once the attempts are exhausted: lost, and not coming back. */
    int net_reconnect_failed;
    /** One-shot: the next REBUILD must run even if it names the zone the
     * client is already standing in. Raised when a session is re-established,
     * because that rebuild is the server's whole world state arriving again
     * and its acknowledgement is what releases the rest of the burst. */
    int net_force_rebuild;
    /** Client-behaviour era table (src/features/features.h). Never NULL after
     *  App_Init — unlike `net`, it is resolved on every boot because an
     *  offline click still has to pick an approach model. Points at
     *  `features_storage`, not at the era singleton. */
    struct ToriRS_FeatureTable const* features;
    /** The app's own copy of the era table, so a `[features:boot]` per-item
     *  override lands somewhere writable. App_Init copies the resolved era in,
     *  applies the overrides and points `features` here. Read through
     *  `features`; this member exists to own the storage. */
    struct ToriRS_FeatureTable features_storage;
    /** Effective lighting behaviour after era + `[render:light]` merge.
     *  Call sites read these rather than features->npc_light_* directly so a
     *  manifest override wins without mutating the const era table. */
    int npc_light_uses_type_ambient_contrast;
    int player_head_light_ambient;

    /* Phase 5: frame state. */
    struct UITreeHost ui_host;
    struct UITreeEmitBuffer emit;
    struct UIInteraction interact;
    struct SeqLoadTracker seq_loads;
    struct InterfaceOpenStats open_stats;
    struct UICross cross;
    /** Mouseover text under the pointer, rebuilt every frame (reference: CS2
     * script 4726 rebuilds it every client cycle). */
    struct UIHoverText hover_text;
    /* Developer overlay (src/ui/README_DEBUG_OVERLAY.md): one minimenu-styled
     * panel carrying the frame time, averaged over the last
     * APP_DEBUG_FRAME_SAMPLES frames. It is hidden until the toggle key, and a
     * hidden panel builds no primitives at all — the emit pass then costs one
     * host call and paints nothing, which is why it can stay declared in a
     * manifest permanently. The ring below keeps filling while it is hidden,
     * so the average is already settled the frame it is shown. */
    struct ToriDbgUI dbg_ui;
    /** Panel / frame-time row handles, -1 until App_Init built them. */
    int dbg_panel;
    int dbg_frame_row;
    int dbg_visible;
    /** Frame durations in microseconds, newest written at dbg_frame_head. */
    uint32_t dbg_frame_us[APP_DEBUG_FRAME_SAMPLES];
    int dbg_frame_head;
    /** Samples written so far, capped at APP_DEBUG_FRAME_SAMPLES. */
    int dbg_frame_count;
    /** Loc editor: a TORIDBG_PANEL_MENU in the same dbg_ui instance (so it
     * shares Build/Prims/emit plumbing with the frame-time panel for free).
     * Opened at the loc under the cursor; "Move"/"Rotate" rows re-place it
     * client-side only via App_WorldLocChange, so the readout gives exact
     * scene coords to hand-copy into a script without a server round trip. */
    int locedit_panel;
    int locedit_visible;
    int locedit_row_target; /* "loc <id> shape <n>" or "no loc selected" */
    int locedit_row_pos;    /* "x=.. z=.. level=.." */
    int locedit_row_size;   /* "size AxB angle=N" */
    int locedit_row_extra;  /* loc name, or "interactive=0/1" when unnamed */
    int locedit_item_xplus;
    int locedit_item_xminus;
    int locedit_item_zplus;
    int locedit_item_zminus;
    int locedit_item_rotate;
    int locedit_item_reselect;
    int locedit_item_deselect;
    int locedit_item_close;
    /** Selected loc, or loc_id -1 for "nothing selected". Set only by an
     * explicit Reselect/Deselect click -- opening or closing the panel never
     * changes it, so a target stays active across a toggle, a camera move, or
     * a string of nudges until the user picks a different one. scene_x/z/level
     * are the loc's CURRENT placement, kept in sync with every move/rotate so
     * the next one starts from the right tile. */
    int locedit_loc_id;
    int locedit_shape;
    int locedit_angle;
    int locedit_size_x;
    int locedit_size_z;
    int locedit_interactive;
    char locedit_name[64];
    int locedit_scene_x;
    int locedit_scene_z;
    int locedit_level;
    /** The last world tile the cursor hovered while NOT over the panel itself
     * -- Reselect targets this, not the live world_hover_tile_x/z, because by
     * the time a menu click on "Reselect" lands the cursor has necessarily
     * moved onto the panel, which invalidates the live hover. -1 = none yet. */
    int locedit_hover_x;
    int locedit_hover_z;
    uint64_t last_logic_ms;
    /** Ctrl held as of the last input pump (reference keyHeld[5]). Latched per
     *  frame because the minimenu action path has no LibToriRS_Input in hand,
     *  and the reference reads it inside tryMove — i.e. for ground, minimap AND
     *  interaction clicks alike, all of which run from there. */
    int ctrl_held;
    int hover_com_id;
    int clicked_com_id;
    int need_redraw;

    /* Async lifecycle state (no blocking IO outside the platform pump). */
    int app_state;     /* enum AppState */
    int boot_progress; /* 0..100, drives the loading bar while BOOTING */
    /* Boot pump accounting (TORIRS_BOOT_STATS): how many frames the boot took,
     * how many scheduler steps ran in total, and how many of those frames hit
     * the per-frame step budget — the last one is what says whether the boot is
     * bound by work or by the frame loop handing it too few slices. */
    int boot_frames;
    long boot_steps;
    int boot_frames_budget_capped;
    uint64_t boot_start_ms;
    /* Post-boot frames that used their whole step budget with work still
     * pending — the async pipeline being drip-fed a slice at a time. */
    int busy_frames;
    long busy_steps;
    int boot_interface_id;
    /**
     * The once-per-session half of Task_AppBoot has already run.
     *
     * Task_AppBoot runs again on every root remount (the Display panel's
     * Fixed/Classic/Modern switch), and its preamble is not idempotent:
     * `VarPManager_SetVarpTypes` reallocates the varp VALUE arrays, so a second
     * pass calloc-zeroes every varp the server has sent this session. Cache type
     * tables and device settings cannot change inside a session; session varp
     * state can, and only the server can put it back. */
    int boot_config_ready;
    /** Set when async work mutated the tree; App_RunOnce consumes it with a
     * relayout + CS1 re-eval request + redraw. */
    int pending_tree_refresh;
    int cs1_eval_inflight;
    /** Tree-affecting async work (CS2 hooks, transmits) is in flight; when the
     * runner queue next goes idle the tree gets a refresh pass. */
    int runner_had_work;
    /** The serial packet/interface transaction yielded on real external IO.
     * Its partially-applied tree is not eligible for frame publication. */
    int exec_runner_had_work;
    int world_load_inflight;
    /** Send MAP_BUILD_COMPLETE when the in-flight world load finishes (set by
     * the REBUILD_NORMAL packet task, not by hotkey/lazy loads). */
    int world_load_server_driven;
    /** Texture ids requested but not yet published into the scene. */
    int tex_pending[512];
    int tex_pending_count;
    /** Element/seq bindings deferred until the sequence load lands. */
    struct AppSeqBindPending seq_bind_pending[64];
    int seq_bind_pending_count;
    /** Entity-sync bookkeeping (server slots -> world entities). */
    struct RS_EntitySync esync;
    /** Dedupe for entity movement-seq load requests. */
    struct SeqLoadTracker entity_seq_loads;
    /** Entity attached-graphic (SPOTANIM mask) combine state, keyed by the
     * entity's body scene element. While the graphic is active the element's
     * model is the reference getTempModel Model.combine([body, spot]) — `body`
     * holds the owned pristine snapshot restored on detach, `spot` the owned
     * spot base model, `combined` the identity of the merged model currently on
     * the element (owned by the element; compared, never dereferenced). */
    struct AppEntitySpotanim
    {
        int body_element_id; /* -1 = free slot */
        /*
         * WORLD_ENTITY_ID of the entity this snapshot was taken from.
         *
         * The element id ALONE is not an identity: scene element ids are
         * recycled, so an entry keyed only by `body_element_id` outlives its
         * owner and then aliases whoever is handed that id next. Detaching such
         * an entry pushes `body` -- the previous owner's model -- onto the new
         * occupant's element and hands over ownership, which is how calling a
         * familiar in the QBD arena made the Queen's model follow the player.
         * The liveness sweep cannot see it either: a recycled id IS live.
         */
        int owner_entity_id;
        int spotanim_id;
        int load_enqueued; /* asset-load task fired for spotanim_id */
        int applied_frame; /* spot seq frame baked into `combined`; -1 none */
        struct ToriDraw_Model* body;
        struct ToriDraw_Model* spot;
        struct ToriDraw_Model* combined;
    } entity_spotanims[64];

    /* ---- server-driven state (Part 5 packet coverage) ---- */
    /** Audio packet sink (no playback backend). */
    struct RS_Audio audio;
    /** What the host reported about its audio device before this frame. The
     *  music player reads it to decide how much to synthesise; zeroed means
     *  "no device", and nothing is rendered. */
    struct ToriRS_AudioFeedback audio_feedback;
    /** Outbound audio requests for the host to hand a backend (App_DrainAudio). */
    struct ToriRS_AudioQueue audio_out;
    /**
     * Device settings that outlive the process: the audio panel's volumes and
     * the rest of the CS2 option store (game/rs_prefs.h). Loaded during boot,
     * mirrored back from the host as the player changes things.
     */
    struct RS_Prefs prefs;
    /** Where prefs are written; NULL turns persistence off (TORIRS_PREFS=""). */
    char const* prefs_path;
    /** logic_cycle at which prefs last moved, or 0 when they are on disk. A
     *  slider drag changes them every tick, so the write waits for the drag to
     *  settle rather than rewriting the file 50 times a second. */
    uint64_t prefs_dirty_cycle;
    /**
     * Camera scripting (CAM_* packets).
     *
     * The camera keeps easing toward its target every frame for as long as the
     * script is up, so the packets set a destination rather than a position;
     * only a rate2 of 100 or more snaps straight there. Heights are measured
     * up from the ground under the tile, not in world space.
     *
     * Shake is per axis and all five can run at once — the encounter scripts
     * fire one call per axis and expect them to compound, so a single slot
     * would keep only whichever arrived last.
     */
    struct
    {
        int scripted; /* 1 while a CAM_MOVETO/LOOKAT script overrides free-fly */
        int move_lx, move_lz, move_height, move_rate, move_rate2;
        int look_lx, look_lz, look_height, look_rate, look_rate2;
        int shake[5];
        int shake_jitter[5];    /* random spread either side of the sine */
        int shake_amplitude[5]; /* sine amplitude */
        int shake_speed[5];     /* sine rate, hundredths */
        int shake_cycle[5];
    } cam_script;
    /** HINT_ARROW state (drawing is a flagged follow-on). type 0 = none. */
    struct
    {
        int type;
        int target; /* npc/player slot, or tile x */
        int tile_z;
        int height;
    } hint_arrow;
    /** SET_PLAYER_OP rows for the player context menu (slot 1..5 -> [0..4]). */
    char player_ops[5][40];
    int player_ops_primary[5];
    /**
     * Controls-settings "Attack" options (enum RS_AttackOption; reference
     * client.playerAttackOption / npcAttackOption). Derived state, not varp
     * storage: varp clientcode 18 / 22 assigns them and nothing else does, so
     * a VARP_RESET leaves them alone exactly as the reference does — it resets
     * them only on the full game-state reset, never from its zeroed table.
     * Both start at RS_ATTACK_OPTION_DEFAULT (Hidden).
     */
    int player_attack_option;
    int npc_attack_option;
    int multiway; /* SET_MULTIWAY */
    /** LAST_LOGIN_INFO for the welcome screen clientcode rows. */
    struct
    {
        int last_ip;
        int days_since_login;
        int days_since_recovery;
        int unread_messages;
    } welcome;
    int reboot_ticks; /* UPDATE_REBOOT_TIMER countdown; 0 = none */
    /** MESSAGE_PRIVATE dedupe (reference messageIds ring). */
    int pm_message_ids[100];
    int pm_message_head;
    int tracking_enabled; /* ENABLE/FINISH_TRACKING */
    int net_cheat_sent;   /* TORIRS_NET_CHEAT one-shot latch */
    /** Zone base for follows-mode zone packets (scene-local tiles). */
    int zone_base_x;
    int zone_base_z;
    /** Plane named by the latest UPDATE_ZONE_* header. Revision-239 zone
     * updates address this plane even when the local player is elsewhere. */
    int zone_level;
    /* Revision 239 SET_NPC_UPDATE_ORIGIN. NPC low-resolution deltas are
     * relative to this scene-local tile, not implicitly to the local player. */
    int npc_update_origin_x;
    int npc_update_origin_z;
    int npc_update_origin_valid;
    /**
     * RUNCLIENTSCRIPT payloads held until the server tick that pushed them has
     * been applied in full.
     *
     * A pushed script repaints from client state that LATER packets of the same
     * tick are about to change, and this pipeline applies one packet at a time
     * with a frame rendered between them (see `app_logic_tick`). Running the
     * script where it arrives therefore paints a half-applied tick and shows it:
     * the price checker's `ge_pricechecker_prices` lands before the
     * UPDATE_INV_PARTIAL that removes the item, so the removed item stays drawn
     * with a price of 0 for as long as the repaint plus the remaining packets
     * take — about ten frames, and plainly visible.
     *
     * The reference has the same arrival order and no such artifact, because it
     * decodes a whole tick's packets in one cycle and only then draws.
     * SERVER_TICK_END is that fence on the wire, so the scripts are drained
     * there; `app_flush_pending_clientscripts` is also called when the pipeline
     * runs dry, which is what carries revisions that send no fence (lc254).
     *
     * The payload is a flat POD, so entries are held by value. Overflow runs
     * the script immediately rather than dropping it — degrading to the old
     * ordering is a cosmetic bug, losing a script is not.
     */
    struct PktRunClientScript pending_clientscripts[APP_PENDING_CLIENTSCRIPT_MAX];
    int pending_clientscript_count;
    /**
     * Has this connection ever sent SERVER_TICK_END?
     *
     * The pipeline pops a packet only when it runs dry, and a tick's packets do
     * not all arrive in one read — so "nothing left to apply" happens *inside* a
     * tick as often as at its end, and treating it as the fence flushes early
     * and reproduces exactly the artifact the fence exists to remove. Where a
     * real fence exists it is therefore the only one used; the dry-pipeline
     * fallback is for revisions that send none (lc254).
     */
    int server_tick_fence_seen;
    /** A fenced server tick has started applying but SERVER_TICK_END has not
     * yet executed.  The live tree may contain only part of that tick, so the
     * renderer retains the preceding committed frame while this is set. */
    int server_tick_open;
    int server_tick_open_cycle;
    /** Logic cycle the oldest held script has been waiting since, so a fence
     *  that never arrives (a tick cut short by a disconnect) cannot strand it. */
    int pending_clientscript_cycle;
    /** Set by App_RunOnce once the stable-tree gate has been crossed and the
     *  current host input frame has reached interaction. */
    int input_frame_consumed;
    /**
     * Zone sub-packets that arrived while the world was still async-loading.
     *
     * The reference cannot receive one mid-build — its scene build runs
     * synchronously inside the packet loop, so every zone update processes
     * after the build it follows. Our REBUILD is an async task, and dropping
     * what arrives in that window diverges: the Inferno's flank walls and
     * rubble land two ticks after REBUILD_REGION and vanished whenever the
     * load was still in flight (docs/ORANGE_WEDGE.md §17). Each entry keeps
     * the zone base and header plane AS OF ARRIVAL. Revision 239's header can
     * target a plane other than the player, so resolving it at replay time
     * would overwrite locs in the wrong plane. Replayed in arrival order once
     * load_complete flips.
     */
    struct AppPendingZonePkt
    {
        struct PktZoneSubPacket pkt;
        int base_x; /* app->zone_base_* as of arrival */
        int base_z;
        int level; /* zone header plane as of arrival */
    } pending_zone[256];
    int pending_zone_count;
    /** Last REBUILD_NORMAL centre zone (deob field1192/field474 /
     * Client-TS mapBuildCenterZoneX/Z). -1 until the first rebuild. Same-zone
     * packets early-out when the world is already active. */
    int rebuild_zone_x;
    int rebuild_zone_z;

    /* ---- interaction state (Part 6) ---- */
    /** "Use <item> with ..." selection (reference objSelected). */
    struct
    {
        int active;
        int obj_id;
        int slot;
        int component_id;
        char name[40];
    } objsel;
    /** "Cast <spell> on ..." selection (reference targetMode/targetComId/
     * targetMask/targetOp): armed by clicking a BUTTON_TARGET spell, consumed
     * by the next click on a valid target kind. */
    struct
    {
        int active;
        int component_id;
        int mask;
        char op[64];
    } targetsel;
    /** Frames since the last input command (IDLE_TIMER). */
    long idle_frames;
    int idle_timer_sent;

    /** Inventory slot press/drag (reference objDrag* state machine): a left
     * press on a filled slot ARMS this — the generic node drag is suppressed
     * while armed, the armed slot renders trans-128 at the mouse delta, and
     * release routes to swap + INV_BUTTOND (real drag: moved >5px AND held
     * >= 5 cycles) or to the default menu row (short click).
     * drag_com_id -1 = not armed. CS1 release: optimistic swap + classic
     * INV_BUTTOND. CS2 release: onDragComplete + dual-endpoint IfButtonD,
     * no local item mutation (rev-230 deob). */
    int inv_drag_com_id;
    int inv_drag_can_drag; /* armed cell's IF_SETEVENTS drag-depth != 0 */
    int inv_drag_from_slot;
    int inv_drag_source_id; /* inv container source id */
    int inv_drag_cycles;
    int inv_drag_grab_x; /* mouse at arm time (reference objGrabX/Y) */
    int inv_drag_grab_y;
    int inv_drag_threshold; /* moved past dead zone since arm (objGrabThreshold) */
    int inv_drag_dead_zone; /* px; from widget, else 5 */
    int inv_drag_dead_time; /* cycles; from widget, else 5 */
    int inv_drag_dx;        /* emit offset for the armed slot (deadzoned) */
    int inv_drag_dy;

    /** Re-entrancy guard for optimistic modal close (rev-230 field267): while
     *  locally unmounting type-0/3 subs, nested if_close must not re-enter. */
    int closing_modals;

    /** The last unscaled window size the shell reported (TORIRS_CMD_WINDOW_RESIZE),
     *  before interface scaling divides it into the canvas. Kept because the
     *  canvas is a lossy function of it: at 200% a 1600x900 window and a
     *  1601x901 one both become 800x450, so the canvas cannot be scaled back up
     *  when the player changes the scale again. 0 until the first resize
     *  arrives. */
    int window_w;
    int window_h;
};

/** Smallest client canvas. The reference's resizable mode will not go below the
 *  fixed frame either, and the reason is structural rather than cosmetic: every
 *  rev-230 gameframe child is authored as an inset off 765x503 (toplevel_resize
 *  computes `max(0, width - inset)` throughout), so a smaller canvas produces
 *  zero-sized viewports, not a smaller frame. */
#define APP_CANVAS_MIN_W 765
#define APP_CANVAS_MIN_H 503

/**
 * Set the client canvas size — THE setter, and the only place the three copies
 * of it agree.
 *
 * Those three are `UITree_LayoutRootWidth/Height` (what the whole UI tree lays
 * out against, read through UITREE_LAYOUT_ROOT_W/H by ~25 call sites),
 * `RS_CS2Host.viewport_w/h` (what CS2VM2_ThreadSetCanvas hands the VM, and
 * therefore what GETCANVASSIZE and VIEWPORT_GETEFFECTIVESIZE return), and the
 * SDL backbuffer (owned by the platform, resized by its caller). They used to be
 * written independently and had already drifted: at a 1024x768 root, the
 * gameframe's own layout script read 1024x768 from if_getwidth and 765x503 from
 * viewport_geteffectivesize, and quietly laid a 765x503 viewport island out in
 * the middle of the frame. Nothing errored. Write the canvas through here.
 *
 * Clamps to APP_CANVAS_MIN_*. Relayouts, then dispatches registered onResize
 * listeners only for components whose resolved width or height changed (the
 * reference trigger=true rule), and relayouts after those scripts. A component
 * that merely moves with a recentered mount does not receive onResize. Returns
 * 1 if the canvas changed, 0 if it was already current.
 */
int
App_SetCanvasSize(
    struct App* app,
    int width,
    int height);

/**
 * Width of the right-docked chrome strip (popout launcher / open panel) after
 * the live layout pass. Measured from the UITree: the widest full-height,
 * fixed-width, parent-height, right-anchored component whose right edge is the
 * canvas right edge. 0 when absent. The layout-mode signature keeps fill-width
 * interface roots from feeding the canvas width back into itself.
 *
 * Script 5355 carves this strip out of the canvas. In fixed mode the classic
 * frame is authored for APP_CANVAS_MIN_W, so the shell must grow the canvas by
 * this amount (see App_SyncFixedChromeInset) or the strip covers the stone edge.
 */
int
App_MeasureRightChromeStripWidth(struct App const* app);

/**
 * Fixed-mode canvas width that keeps the classic frame at APP_CANVAS_MIN_W and
 * parks the chrome strip outside it: MIN_W + measured strip. Resizable callers
 * should not use this — they carve from whatever window size they already have.
 */
int
App_FixedCanvasWidth(struct App const* app);

/**
 * When window_mode is fixed, grow/shrink the canvas to App_FixedCanvasWidth so
 * the gameframe lays out at APP_CANVAS_MIN_W with the popout strip outside.
 * Returns 1 if the canvas size changed. The shell must also snap the SDL window
 * to the new size (App has no platform).
 */
int
App_SyncFixedChromeInset(struct App* app);

/**
 * Apply a pending "Interface scaling" change (device option 27), if a
 * clientscript made one since the last call. Returns 1 if the canvas changed.
 *
 * The scale is realised as a *smaller canvas*, not as a second coordinate
 * space: the whole client — UI tree, world viewport, backbuffer — lays out and
 * draws at window/scale, and the shell's existing letterbox blows that up to
 * the window. 200% therefore means "half as many pixels across, each twice the
 * size", which is what makes every interface element twice as big without a
 * single widget knowing about it. The cost is the honest one and the same one
 * the mobile client pays: the 3D viewport renders at the reduced resolution
 * too.
 *
 * Fixed mode is deliberately unaffected — its canvas is pinned to the classic
 * frame and already letterboxed to fill the window, so there is nothing left
 * for a scale to do. App_SetCanvasSize's floor enforces that on its own.
 */
int
App_SyncUiScale(struct App* app);

/**
 * Take a pending SETWINDOWMODE, if a clientscript issued one since the last
 * call. Writes the new mode (enum CS2VM_WindowMode) to *out_mode and returns 1;
 * returns 0 when nothing changed.
 *
 * The App deliberately does not act on it: fixed vs resizable is a statement
 * about the *window*, and the App has no platform. The shell drains this and
 * decides — which for this client is "stop tracking the window and go back to
 * the fixed canvas", or "start tracking it". Same split as
 * RS_CS2Host.close_modal_requested.
 */
int
App_TakeWindowModeChange(
    struct App* app,
    int* out_mode);

/**
 * Drain a Display-panel layout choice (0 Fixed / 1 Classic / 2 Modern) raised
 * when settings_client_mode calls setwindowmode. Returns 1 when a choice is
 * pending. The shell sends WINDOW_STATUS.
 */
int
App_TakeClientLayoutChange(
    struct App* app,
    int* out_mode);

/**
 * The window mode the client is in right now (enum CS2VM_WindowMode), i.e. what
 * GETWINDOWMODE answers.
 *
 * The shell needs this at boot. The host's mode is live state from the moment
 * RS_CS2Host_Init runs, but the platform's follow gate starts clear, so without
 * a boot-time read the two disagree for the whole session: every script is told
 * "resizable" while the window letterboxes a fixed canvas — which is precisely
 * the "resizable mode scales instead of resizing" bug.
 */
int
App_WindowMode(
    struct App const* app);

/**
 * State the boot window mode (enum CS2VM_WindowMode), before anything reads it.
 *
 * Config, not a script action: it writes both the live and the default mode and
 * does NOT raise window_mode_dirty, so it cannot be mistaken for a SETWINDOWMODE
 * the user performed. Out-of-range values are ignored (the host keeps its own
 * default). The shell applies the platform side itself.
 */
void
App_SetBootWindowMode(
    struct App* app,
    int mode);

/** Construct all subsystems in dependency order. Asserts on failure (parity
 * with the previous bootstrap). */
void
App_Init(
    struct App* app,
    struct AppConfig const* cfg);

/** Tear down in strict reverse of App_Init. */
void
App_Shutdown(struct App* app);

/** Forget everything that belonged to the session that just ended: tracked
 * entities and the per-account UI state the reference resets alongside them.
 * Shared by the server's LOGOUT and by the connection-lost path, which differ
 * only in what happens next. */
void
App_NetSessionReset(struct App* app);

/** Select world submission after renderer initialization. A software fallback
 * must always restore TORIRS_WORLD_PAINTER. */
void
App_SetWorldRenderMode(
    struct App* app,
    enum ToriRS_WorldRenderMode mode);

/** Resolved interface-logic VM (enum AppUiLogic, never DEFAULT): the manifest's
 * explicit choice, or derived from cache_kind (dat1 -> CS1, dat2 -> CS2). The
 * old-gen-only ClientCode pass and the CS2 host wiring key off this. */
int
App_UiLogic(struct App const* app);

/** Begin opening an interface as the tree root (TS WidgetManager
 * .setRootInterface). Fully async: enqueues the boot task and returns —
 * App_RunOnce pumps it and flips app_state to APP_STATE_READY when the tree
 * (and initial assets) are built. App_Render draws a loading bar meanwhile. */
void
App_OpenRootInterface(
    struct App* app,
    int interface_id);

/** IF_OPENSUB (rev-230 openSubInterface): mount a cache interface group under a
 *  component slot of the open root. target_uid = packed (parent<<16|child) of
 *  the mount slot; type 0=modal, 1=overlay, 3=tab/sidemodal. Async — enqueued on
 *  the serial exec pipeline so it settles before the next packet is applied. */
void
App_OpenSubInterface(
    struct App* app,
    int target_uid,
    int interface_id,
    int type);

/** IF_CLOSESUB: unmount whatever is mounted at a component slot. */
void
App_CloseSubInterface(
    struct App* app,
    int target_uid);

/** IF_MOVESUB: move the mounted sub at source_uid onto dest_uid. */
void
App_MoveSubInterface(
    struct App* app,
    int source_uid,
    int dest_uid);

/** RUNCLIENTSCRIPT: run a server-named clientscript with its arguments. Enqueued
 *  on the same serial pipeline as everything else, so a script that seeds state
 *  for an interface (the world map's `worldmap_transmitdata`) settles before the
 *  IF_OPENSUB behind it. */
void
App_RunClientScript(
    struct App* app,
    struct PktRunClientScript const* request);

/** Dispatch every RUNCLIENTSCRIPT held since the last fence, in arrival order.
 *  Called at SERVER_TICK_END and whenever the packet pipeline runs dry — see
 *  `pending_clientscripts` for why they are held at all. */
void
App_FlushPendingClientScripts(struct App* app);

/**
 * IDK_SAVEDESIGN: send the accepted character design (reference
 * CC_ACCEPT_DESIGN). No-op when networking is not in the game state.
 */
void
App_SendIdkDesign(
    struct App* app,
    int gender,
    int const kits[RS_IDK_DESIGN_PARTS],
    int const colours[RS_IDK_DESIGN_COLOURS]);

/** IF_SETTEXT: persist (reference IfType.list semantics) + apply if mounted. */
void
App_IfTextSet(
    struct App* app,
    int com_id,
    char const* text);

/** IF_SETHIDE: persist (reference IfType.list semantics) + apply if mounted. */
void
App_IfHideSet(
    struct App* app,
    int com_id,
    int hide);

/** Drop every IF_SETEVENTS arming. Called on IF_OPENTOP, because that is when
 *  the real client drops it — see the call site in rs_gameproto_exec.c. */
void
App_IfEventsClear(struct App* app);

/**
 * IF_SETEVENTS: mark slots `from`..`to` of a component as accepting input.
 *
 * Persisting WITHIN a root, like App_IfHideSet, because the server enables
 * events before the interface holding the component has mounted. Not across
 * one: `App_IfEventsClear` drops the table on IF_OPENTOP.
 */
void
App_IfEventsSet(
    struct App* app,
    int com_id,
    int from,
    int to,
    int events);

/** Server-declared events mask for a component, or 0 when it was never
 *  enabled. 0 means "not interactive" — rev 230 has no clickable-by-default. */
int
App_IfEventsGet(
    struct App const* app,
    int com_id);

/** Server-declared events mask for one sub-id of a component. This is the
 *  lookup inventory cells and other ranged IF_SETEVENTS consumers need: the
 *  wire names a static parent plus a dynamic-child/grid slot, so querying the
 *  parent alone deliberately does not match a 0..N range. */
int
App_IfEventsGetAt(
    struct App const* app,
    int com_id,
    int sub_id);

/** Effective rev239 WidgetFlags for a live node. A matching IF_SETEVENTS
 *  override wins (including an explicit zero); otherwise the cache-authored
 *  click mask on the widget is used. Dynamic children inherit ranged server
 *  overrides from their static parent. */
unsigned
App_IfEventsGetEffective(
    struct App const* app,
    int com_id);

/** IF_SETNPCHEAD: load the npctype + its head models, composite the chathead,
 *  and bind it to the MODEL widget (reference IfType.getModel type 2). Async —
 *  the head appears once the assets resolve. */
void
App_SetInterfaceNpcHead(
    struct App* app,
    int component_id,
    int npc_id);

/** IF_SETPLAYERHEAD: composite the local player's chathead onto the MODEL
 *  widget (reference IfType.getModel type 3). Async. */
void
App_SetInterfacePlayerHead(
    struct App* app,
    int component_id);

/** Revision-239's independently mutable interface PlayerComposition setters. */
void
App_SetInterfacePlayerModelSelf(
    struct App* app,
    int component_id,
    int copy_objs);

void
App_SetInterfacePlayerModelBaseColour(
    struct App* app,
    int component_id,
    int index,
    int colour);

void
App_SetInterfacePlayerModelBodyType(
    struct App* app,
    int component_id,
    int body_type);

void
App_SetInterfacePlayerModelObj(
    struct App* app,
    int component_id,
    int obj_id);

/** IF_SETMODEL (reference IfType model1Type 1): load a raw cache model and
 *  bind its uploaded scene model to the widget. Persisted across remounts. */
void
App_SetInterfaceModel(
    struct App* app,
    int component_id,
    int model_id);

/** IF_SETOBJECT (reference IfType model1Type 4): bind an obj's lit inventory
 *  model to a MODEL widget with the objtype's 2d angles and modelZoom =
 *  zoom2d*100/zoom — e.g. the combat-tab wielded weapon. Persisted by com_id
 *  like the chatheads (survives the sidebar tab mounting after the packet). */
void
App_SetInterfaceObjModel(
    struct App* app,
    int component_id,
    int obj_id,
    int zoom);

/** IF_SETANIM: set a MODEL widget's animation seq; persisted alongside a
 *  matching chathead so it survives the interface (re)mounting. */
void
App_SetInterfaceModelAnim(
    struct App* app,
    int component_id,
    int anim_id);

/** Pump the boot to completion (headless harnesses and tests only — the
 * interactive loop must NOT call this; it renders the loading state
 * instead). IO still flows exclusively through the platform pump. */
void
App_BootWait(struct App* app);

/* Entity-sync spawn/apply helpers (assets must already be cached — the
 * PLAYER_INFO / NPC_INFO exec tasks await the loads first). Coordinates are
 * scene-local tiles. Return the World pool index, or -1. */
int
App_WorldSpawnSyncedPlayer(
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

int
App_WorldSpawnSyncedNpc(
    struct App* app,
    int npc_id,
    int scene_x,
    int scene_z,
    int level);

/** Follow NpcType.multiNpc (opcode 106) to the variant that's actually live
 * under the local player's current varp/varbit state, same rule as a loc
 * transform table (VarPManager_ResolveTransform). A `multinpc` shell record
 * carries no model of its own, so every wire npc type -- NPC_INFO's initial
 * type and every CHANGE_TYPE -- must be resolved through this before it is
 * used for anything (spawn, retype, preload). Returns `npc_id` unchanged when
 * it doesn't name a shell, or on a lookup miss. Depth-capped at 4, matching
 * tools/gen_multinpc_catalog.py's chain-walk. */
int
App_NpctypeResolveMultiId(
    struct App* app,
    int npc_id);

struct UITreeMinimapDot;

/** Build this frame's minimap dot overlay (the UITREE_HOST_GET_MINIMAP_DOTS
 * body). Returns the dot count and points `*out_dots` at the app-owned array,
 * valid until the next call. Exposed for the dot-gating test. */
int
App_MinimapBuildDots(
    struct App* app,
    struct UITreeMinimapDot const** out_dots);

struct PktPlayerAppearance;

/** Rebuild + swap the player's composited model and apply the decoded
 * appearance (idle anims, name, combat level) to the World entity. */
void
App_WorldApplyPlayerAppearance(
    struct App* app,
    int world_idx,
    int element_id,
    struct PktPlayerAppearance const* appearance);

/** NPC transmog (CHANGE_TYPE): rebuild the model + apply the new config's
 * size/anims/menu data (assets already cached). */
void
App_WorldApplyNpcType(
    struct App* app,
    int world_idx,
    int element_id,
    int npc_type);

/* Ground item stacks (zone OBJ_* packets; objtype/model already cached). */
int
App_WorldObjStackAdd(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count);

void
App_WorldObjStackDel(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id);

/**
 * REBUILD_NORMAL relocation, run right after the server-driven world load
 * lands: shift every kept entity by the scene-base delta (tiles), reposition
 * ground-item elements against the new heightmap, drop out-of-scene stacks,
 * clear projectiles/spotanims (reference mapBuild), shift/clear the camera
 * and cutscene script, clear the minimenu, invalidate the minimap bake, and
 * move the minimap destination flag. Entity element ids are untouched — they
 * live in the scene's DYNAMIC pool, which the rebuild's static clear skips.
 */
void
App_WorldRebuildShift(
    struct App* app,
    int base_dx,
    int base_dz);

/**
 * REBUILD_NORMAL (Client-TS / deob method3310): early-out when the centre
 * zone is unchanged and a world is active; otherwise queue a classic 104x104
 * zone-centred load. The packet task awaits the load, then shifts entities
 * and finishes. Returns 1 when a load was started, 0 on same-zone skip
 * (reference does not ack a skipped rebuild — Client.ts:2289 acks from
 * mapBuild only).
 *
 * `force` suppresses that early-out, and REBUILD_REGION always passes it. The
 * skip is sound for a cache-built scene, where the same centre zone means the
 * same 104x104 tiles; it is wrong for an instanced one, where the zones can be
 * repointed underneath an unmoved player — which is exactly what happens when a
 * room is added to a house the player is standing in.
 */
int
App_WorldRebuildBegin(
    struct App* app,
    int zone_x,
    int zone_z,
    int force);

/** Drain WorldEventKind_EntityRemoved into ToriDraw_SceneElementRemove.
 *  Required before a scene rebuild begins (ResetSceneAlloc asserts the queue
 *  is empty) and after bulk despawns. */
void
App_WorldDrainEntityRemoved(struct App* app);

/** Post-load wiring (height fn, texture sync, minimap bake, and the
 * server ack for a REBUILD_NORMAL-driven load). Runs at the tail of the world
 * load: the fire-and-forget path wires it as Task_WorldLoad's on_done; the
 * server-driven path, which awaits the load, calls it directly after the await.
 * Camera is only placed for non-server-driven loads — a rebuild shifts the
 * existing camera instead (deob field3239 -= dx<<7).
 */
void
App_WorldLoadFinish(struct App* app);

/** MAP_BUILD_COMPLETE ack. Sent after a REBUILD_NORMAL-driven load finishes
 * (Client.ts mapBuild / Client.ts:2289). Same-zone skips do not ack. */
void
App_SendMapBuildComplete(struct App* app);

/** LOC_ANIM: attach a sequence to the scenery element on a tile. `loc_shape`
 * (zone packet info >> 2) selects the loc's layer so a door animates the wall,
 * not a centrepiece/floor-decor sharing the tile. */
void
App_WorldSceneryAnim(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id);

/* Spawn a free-standing spotanim (graphical effect) at a tile — reference
 * MapSpotAnim, driven by the MAP_ANIM zone packet. Enqueues an async load of
 * the spotanim config + its model/seq before building the world entity. */
void
App_WorldSpotanimSpawn(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int spotanim_id,
    int height,
    int delay);

/* Jump the scripted camera straight to its move-to / look-at target rather
 * than easing there. CAM_MOVETO and CAM_LOOKAT call these when the rate2 they
 * carry is 100 or more, which is what makes a cutscene cut rather than glide. */
void
App_CinemaCameraSnapPosition(struct App* app);

void
App_CinemaCameraSnapAngle(struct App* app);

/* Spawn a projectile (reference ClientProj) from a spotanim config, driven by
 * the MAP_PROJANIM zone packet. Enqueues an async load of the spotanim config +
 * its model/seq before building the world entity. Coordinates are scene tiles;
 * `src_height`/`dst_height` are the raw wire bytes (×4 applied internally);
 * `start_delay`/`end_delay`/`peak`/`arc` are the wire trajectory params
 * (Client-TS t1/t2/angle/startpos). `target` is the wire target-entity id
 * (>0 npc, <0 player) — stored but live retargeting is a follow-on. */
void
App_WorldProjectileSpawn(
    struct App* app,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int level,
    int spotanim_id,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target);

/* Apply a zone LOC_ADD_CHANGE / LOC_DEL (reference locChangeCreate +
 * locChangeDoQueue): enqueues an async task on the serial exec FIFO that awaits
 * the loc config + its models (+ seq) before calling
 * WorldBuilder_ApplyLocChange — the change only lands once its assets are
 * resident (reference changeLocAvailable). loc_id < 0 = pure delete. */
void
App_WorldLocChange(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle);

/**
 * Zone LOC_MERGE / P_LOCMERGE: schedule a timed hide of the loc on this tile
 * (countdown LocChange) and mark the player so the loc model rides with them
 * for [start_cycle, end_cycle) client ticks from now.
 */
void
App_WorldLocMerge(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int start_cycle,
    int end_cycle,
    int player_pid);

/**
 * Drain the command bus, routing each command to its subsystem: the single
 * command loop of the unified input path. TORIRS_CMD_INPUT_* commands apply to
 * `input` (ToriRS_Input); TORIRS_CMD_NET_* commands go to the network
 * subsystem once one is attached (ignored until then); TORIRS_CMD_FRAME is a
 * record/replay delimiter and is skipped. Call between LibToriRS_Input_Begin
 * and _End, before App_RunOnce.
 */
void
App_DrainCommands(
    struct App* app,
    struct ToriRS_CmdBus* bus,
    struct LibToriRS_Input* input);

/**
 * Queue a sound effect for playback (reference SYNTH_SOUND).
 *
 * The one way in: the packet handler uses it, and so does the TORIRS_SIM_SOUND
 * harness. `delay` is in client ticks and the effect's own lead-in is added to
 * it, so callers pass what the server would have sent.
 */
void
App_PlaySound(
    struct App* app,
    int sound_id,
    int loops,
    int delay);

/**
 * SOUND_AREA: the same, at a place in the world.
 *
 * `radius` is where it becomes inaudible and `inner` where it stops being at
 * full volume, both in tiles. Outside `radius` the reference discards the entry
 * rather than playing it quietly, which is why this is not just a volume hint.
 */
void
App_PlaySoundAt(
    struct App* app,
    int sound_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z,
    int radius,
    int inner);

/**
 * Tell the game what the host's audio device is doing.
 *
 * Called once per frame *before* the tick, because the music player sizes its
 * synthesis from the stream headroom reported here. A host that never calls it
 * leaves the feedback zeroed, which reads as "no device" and costs nothing.
 */
void
App_SetAudioFeedback(
    struct App* app,
    const struct ToriRS_AudioFeedback* feedback);

/** Play a music track (MIDI_SONG / MIDI_SONG_V2). */
void
App_PlaySong(
    struct App* app,
    int song_id,
    bool loop,
    int fade_out_ms,
    int fade_in_ms);

/** Play a jingle, resuming the song afterwards (MIDI_JINGLE). */
void
App_PlaySongWithSecondary(
    struct App* app,
    int primary_id,
    int secondary_id,
    int fade_out_ms,
    int fade_in_ms);

void
App_SwapSong(
    struct App* app,
    int fade_out_ms,
    int fade_in_ms);

void
App_PlayJingle(
    struct App* app,
    int jingle_id,
    int length_ms);

/** Stop the current track (MIDI_SONG_STOP). */
void
App_StopSong(
    struct App* app,
    int fade_out_ms);

/**
 * Set the region's background ambience (AMBIENTSOUND_START / _STOP).
 *
 * One looping, unpositioned sound at a time; -1 stops it. Separate from the
 * loc-driven area sounds, which have positions and are found in the scene
 * rather than announced.
 */
void
App_SetAmbientSound(
    struct App* app,
    int sound_id,
    int fade_ms);

/**
 * Take the audio requests the game produced since the last call.
 *
 * The outbound half of the audio interface: the game queues what should be heard
 * (see game/rs_audio.h), the host drains it here once per frame and submits the
 * batch to whichever backend it built. A host that never calls this simply gets
 * no sound — nothing else changes, which is what keeps headless runs and tests
 * free of an audio device.
 *
 * The PCM each command points at stays valid until the *next* call, which is one
 * frame — long enough for a backend to copy or submit it. Returns how many
 * commands were written.
 */
int
App_DrainAudio(
    struct App* app,
    struct ToriRS_AudioCommand* out,
    int max);

/**
 * Report how long the frame the host just finished took, in microseconds.
 *
 * The developer overlay's frame-time readout is the mean of the last
 * APP_DEBUG_FRAME_SAMPLES of these. The App owns no clock — the shell measures
 * the interval it wants reported and hands it over, the same way the caret
 * blink is app-driven in ui/. A host that never calls this simply gets a
 * readout of "--"; nothing else changes.
 *
 * Measure the frame's *work*, not its wall clock: a capped loop sleeps out the
 * residual of its 20 ms budget, and timing across that sleep reports the cap
 * back rather than the cost of the frame.
 */
void
App_NoteFrameTime(
    struct App* app,
    uint64_t frame_us);

/**
 * Send a `::` command as if it had been typed into the chatbox.
 *
 * The debug procs a content lane defines — `[debugproc,rs2012qbd]` and friends
 * — are the only way into an encounter that no walk or click can reach, and a
 * headless harness has no chatbox. `text` is the command WITHOUT the leading
 * `::`, matching the wire format.
 *
 * Returns false until the connection reaches TORIRS_NET_GAME: the command is a
 * server-side script call, so before login there is nothing to send it to. The
 * world renders well before that point, so a caller that fires once on a frame
 * number it guessed will send nothing at all — retry on false.
 */
bool
App_SendCommand(
    struct App* app,
    char const* text);

/**
 * One loop-body iteration: pump tasks, run pending 20ms logic ticks
 * (client clock, widget timers, animations), then the per-frame interaction
 * pass and emit rebuild. Returns non-zero when the frame needs re-rendering.
 */
int
App_RunOnce(
    struct App* app,
    uint64_t now_ms,
    struct LibToriRS_Input* input);

/** True only when the live UI tree is eligible to produce a new frame.  While
 * false, hosts must present/copy the last committed framebuffer rather than
 * calling App_Render against a partially-applied CS2/server transaction. */
int
App_FrameSettled(struct App const* app);

/** Whether the most recent App_RunOnce reached interaction. A shell retains
 * one-shot input while false so an async CS2/tick wait cannot eat a mouse-up or
 * key edge. */
int
App_InputFrameConsumed(struct App const* app);

/**
 * Relayout + CS1 re-evaluate + mark for redraw after an out-of-band tree
 * mutation (slot mounts, packet-driven component changes).
 */
void
App_RefreshAfterTreeMutation(struct App* app);

/** Rasterize the current emit buffer into pixels (width x height ARGB). */
void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height);

/**
 * Build a ToriRS_Frame for the current emit/world state (no rasterization).
 * Returns false when the app is still booting (caller should draw a boot bar).
 */
bool
App_BuildFrame(
    struct App* app,
    struct ToriRS_Frame* frame,
    int width,
    int height);

/** Classify render-time pick hits into the world pickset / hover tile. */
void
App_PickFinish(
    struct App* app,
    struct ToriRS_PickHits const* hits);

/** True while APP_STATE_BOOTING; optionally returns boot_progress 0..100. */
bool
App_IsBooting(
    struct App* app,
    int* out_progress);

/** Write the current emit buffer to a BMP. Returns 0 on success. */
int
App_WriteBmp(
    struct App* app,
    char const* path,
    int width,
    int height);

/** Index of the tree's 3D viewport component, or -1 when the open interface has
 * none. The world is a UI element like any other: no viewport in the tree means
 * no map load, no sim, no 3D pass. Two sources feed it — a RevConfig INI
 * `type=world` node, or a cache component with clientCode
 * UITREE_CLIENT_CODE_CONTENT_WORLD (baked in uitree_build.c). */
int32_t
App_WorldNodeIndex(struct App const* app);

/**
 * Record a kill-loot drop into the client store and run clientscript 7159.
 *
 * Sequence from the decompiled 7159: the engine fills the store THEN the
 * script reads it back, so AddKillLoot must precede the script push.
 * `obj_id` is the cache obj id; cost is looked up from the objtype.
 */
void
App_LootNotifyKill(
    struct App* app,
    char const* source_name,
    int obj_id,
    int qty);

/**
 * Send one ordinary world-object operation through the live game's outbound
 * protocol path. This is a headless acceptance hook: callers supply the
 * cache-defined object id and absolute tile, so no content ids live in C.
 */
void
App_SimulateLocOp(
    struct App* app,
    int op_num,
    int abs_x,
    int abs_z,
    int loc_id);

/**
 * The npc counterpart of App_SimulateLocOp: send one ordinary OPNPC1..5 for the
 * first live npc of the given cache type. Returns the server slot the operation
 * was addressed to, or -1 when no synced npc of that type is in the scene.
 *
 * Targeting by type is what makes this usable from a test. A world click needs
 * the npc's pixels, which move with the camera and the tile the server picked;
 * the packet needs its server slot, which is assigned at spawn. The type is the
 * only one of the three a test can write down.
 */
int
App_SimulateNpcOp(
    struct App* app,
    int op_num,
    int npc_id);

#endif
