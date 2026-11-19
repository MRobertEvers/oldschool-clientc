#ifndef SRC_APP_H
#define SRC_APP_H

#include "asyncio.h"
#include "engine/cache_mounts.h"
#include "engine/cache_provider.h"
#include "engine/entity_spotanim.h"
#include "engine/uitree_anim.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_builder/uitree_builder.h"
#include "engine/uitree_scene_bridge.h"
#include "engine/torirs_model_inst_cache.h"
#include "engine/world_seq_source_toridraw.h"
#include "features/features.h"
#include "features/feature_facts.h"
#include "game/rs_audio.h"
#include "render/world_camera_orbit.h"
#include "game/rs_chat.h"
#include "game/rs_clientscript_queue.h"
#include "game/rs_server_tick.h"
#include "game/rs_zone_inbox.h"
#include "game/rs_rebuild_region.h"
#include "game/rs_npc_update_origin.h"
#include "game/rs_hint_arrow.h"
#include "game/rs_player_ops.h"
#include "game/rs_interaction_mode.h"
#include "game/rs_session_info.h"
#include "game/rs_boot_state.h"
#include "game/rs_client_phase.h"
#include "game/rs_sailing_hud.h"
#include "game/rs_tile_highlight_watch.h"
#include "game/rs_mouseover_report.h"
#include "game/rs_client_trigger.h"
#include "perf/boot_pump_stats.h"
#include "perf/frame_clock.h"
#include "net/net_keepalive.h"
#include "net/net_link_watch.h"
#include "net/net_once.h"
#include "net/session_end.h"
#include "ui/chat_line.h"
#include "ui/inv_drag.h"
#include "ui/minimap_view.h"
#include "ui/tree_refresh.h"
#include "ui/target_select.h"
#include "game/rs_ground_items_dirty.h"
#include "engine/title_flames.h"
#include "game/rs_login_replies.h"
#include "game/rs_title_session.h"
#include "render/cam_script.h"
#include "render/overlay_stage.h"
#include "render/panel_overlay_stage.h"
#include "render/painter_cull_bake.h"
#include "render/redraw_request.h"
#include "render/render_mode.h"
#include "plugin/plugin_frame_binding.h"
#include "plugin/plugin_highlights.h"
#include "plugin/plugin_boot_config.h"
#include "plugin/plugin_geometry.h"
#include "plugin/plugin_panel.h"
#include "ui/chrome_compositor.h"
#include "plugin/plugin_screenshots.h"
#include "render/render_skip.h"
#include "render/skybox_slot.h"
#include "render/tile_mark_stage.h"
#include "render/world_views.h"
#include "replay/replay_chase.h"
#include "replay/replay_config.h"
#include "net/net_client_config.h"
#include "editor/map_editor_config.h"
#include "game/offline_viewer_config.h"
#include "game/rs_worldmap_view.h"
#include "game/rs_preload.h"
#include "game/rs_title.h"
#include "game/rs_accounts.h"
#include "game/rs_title_accounts.h"
#include "game/rs_title_music.h"
#include "platform/platform_credential_form.h"
#include "game/task_accounts_io.h"
#include "net/rev/revpacket.h"
#include "game/rs_cs1_host.h"
#include "game/rs_cs2_host.h"
#include "game/entity_info_scratch.h"
#include "game/rs_entity_sync.h"
#include "game/rs_idk_design.h"
#include "game/rs_if1_buttons.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_player_stats.h"
#include "game/rs_prefs.h"
#include "game/rs_save_debounce.h"
#include "platform/client_scale.h"
#include "ui/canvas_scale.h"
#include "platform/device_caps.h"
#include "platform/text_input_gate.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "game/rs_worldmap_drag.h"
#include "game/rs_worldmap_surface.h"
#include "input/torirs_input.h"
#include "input/torirs_frame_modifiers.h"
#include "input/torirs_input_claims.h"
#include "input/torirs_world_tap.h"
#include "inv/inv_manager.h"
#include "platform/platform_io.h"
#include "render/torirs_damage_region.h"
#include "render/torirs_frame.h"
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_drive.h"
#include "plugin/torirs_plugin_mesh.h"
#include "ui/uitree_frame.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "ui/uitree_if_models.h"
#include "ui/uitree_role.h"
#include "ui/uitree_scroll.h"
#include "revconfig/revconfig_profile.h"
#include "revconfig/revconfig_refs.h"
#include "task_runner.h"
#include "engine/world_builder/rebuild_frame_stats.h"
#include "engine/world_builder/rebuild_pace.h"
#include "engine/world_builder/world_rebuild_job.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_load_state.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "ui/uitree.h"
#include "ui/uitree_cross.h"
#include "ui/uitree_ink.h"
#include "ui/torirs_chrome_exec.h"
#include "ui/torirs_chrome_shell.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_kept_mounts.h"
#include "ui/uitree_host.h"
#include "ui/uitree_hovertext.h"
#include "ui/uitree_interact.h"
#include "varc/varc_manager.h"
#include "game/rs_loot_store.h"
#include "game/rs_healthbar.h"
#include "game/rs_hitsplat.h"
#include "game/rs_soundscape.h"
#include "varp/varp_manager.h"
#include "world/wev.h"
#include "world/world_pickset.h"
#include "world/wev_aboard_trace.h"
#include "world/loc_lane.h"
#include "world/entity_objstack.h"
#include "world/entity_projectile.h"
#include "sideapp/side_app_binding.h"
#include "world/worldview.h"
#include "render/wev_flat.h"

struct ToriPlatform_Renderer_Soft3D;
struct ToriRS_Frame;
struct ToriRS_PickHits;
struct PktRunClientScript;

#include <assert.h>
#include <limits.h>
#include <stdint.h>

struct World;
struct Assets;
struct RS_Pending;
struct WorldBuilder;
struct PaintersBuffer;
struct RSCache_Dat1Disk;
struct Dat1BuildCache;
struct Dat2BuildCache;
struct ToriRS_CmdBus;
struct ToriRS_CmdHeader;
struct ToriRS_CmdSafeArea;
struct ToriRS_Network;
struct RsprotInfoOp;
struct DevOverlay;
struct Editor;
struct MapEditorApp;
/** Placeholder requests waiting for a tick; owned by app/app_placeholder.c. */
struct WidgetIconPark;

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

struct AppConfig
{
    char const* cache_dir;
    char const* config_dir;
    char const* script_dir;
    /* [io:boot] -- the server asked for any stored file this disk lacks: the
     * plugin manifest, the plugin scripts, and each shipped plugin asset as a
     * plugin asks for it. Empty host leaves TORIRS_IO_SERVER (or nothing) in
     * charge. */
    char io_host[256];
    int io_port;
    /**
     * enum WorldBuilderLayout (engine/world_builder/world_builder.h): which
     * world builder fills the scene -- `[world:boot] layout=` or
     * `--world-layout`. 0 (static, the game's) is what a zeroed config gets;
     * the map-editor manifests state edit.
     */
    int world_layout;
    enum AppCacheKind cache_kind;
    /** Cache identity from [cache:boot]. All four stated; used by
     *  RSCache_ProfileForIdentity. cache_identity_set is 0 until ApplyToConfig. */
    int cache_game;
    int cache_epoch;
    int cache_revision;
    uint32_t cache_quirks;
    int cache_identity_set;
    /**
     * `[cache:boot] source=ondemand` — dat1 archives come off the LostCity
     * server named by `[net:boot]` instead of out of cache_dir, over the
     * 2004-era on-demand protocol (src/platform/platform_x_io_ondemand.h).
     *
     * There is then no local cache at all: cache_dir is a label, and nothing
     * opens it. That is the point — a server repacks its cache whenever its
     * content changes, and a copy on this machine is stale from the moment it
     * is made, which surfaces as a login refused for "client out of date"
     * rather than as anything about the cache.
     */
    int cache_on_demand;
    /**
     * The same server's HTTP port, which is where the nine jag archives and
     * the login checksums live (`[net:boot] ws_port`; 0 = 80).
     *
     * It is spelled ws_port in the manifest because that is where a browser
     * reaches the world, and for LostCity the two are one port. Only the
     * on-demand cache source reads it natively.
     */
    int web_port;
    /** Map square to spawn on when nothing else selects one. Both -1 = use the client
     *  default (50,50). Set from the manifest `[cache:boot] spawn`; TORIRS_WORLD_MAP still
     *  overrides it, and a server REBUILD_NORMAL overrides both.
     *
     *  This exists because 50,50 is not universally loadable: a keyed cache carries XTEA
     *  keys only for the squares it was dumped with, and an unkeyed square on a keyed
     *  cache yields terrain (which is not encrypted) with **zero locs**. */
    int spawn_x;
    int spawn_z;
    /** Resolved `[debug:hotkeys]` key -> `[action:<name>]` bindings, by action
     *  name, and every declared action. Empty by default. Each binding's `args`
     *  retains the action's optional `a=` payload. The side-app registry copies
     *  it (sideapp/side_app.h). */
    struct SideAppBindingTable side_app_bindings;
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
    /** `[features:boot] mover` — enum ToriRS_MoverModel, only read when the
     * _set flag is 1 (0 is a real model, CYCLE_INTEGER). Overrides the era
     * table; TORIRS_MOVER_MODEL overrides both. */
    int features_mover_model;
    int features_mover_model_set;
    /** `[features:boot] world_rebuild` — enum ToriRS_WorldRebuildModel, only
     * read when the _set flag is 1 (0 is a real model, BLOCKING). Overrides
     * the era table; TORIRS_WORLD_REBUILD overrides both. */
    int features_world_rebuild_model;
    int features_world_rebuild_model_set;
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
    /** Manifest-pinned chrome zoom in quarter units (bootmanifest.h), -1
     *  `dynamic`; 0 follows the display density. The env
     *  TORIRS_CHROME_SCALE outranks both. */
    int chrome_scale;
    /** `[ui:boot] chrome_checkbox` -- enum ToriRSChromeCheckStyle: which of the
     *  interfaces' two booleans the chrome's checkboxes wear. 0 (the default)
     *  is the settings page's tick/cross. TORIRS_CHROME_CHECKBOX outranks it. */
    int chrome_checkbox;
    /** `[ui:boot] hidpi` — 1 renders into a device-pixel drawable, -1 declines
     *  it, 0 is unset and leaves the platform default standing (on, except on
     *  the web lane). TORIRS_HIDPI outranks all three. */
    int hidpi;
    /** `[ui:boot] plugins` -- -1 declines the plugin layer, 1 asks for it, 0 is
     *  unset and loads it. TORIRS_PLUGINS outranks both. */
    int plugins;
    /** `[ui:boot] clienttype` / `on_mobile` -- what the CLIENTTYPE and
     *  ON_MOBILE clientscript opcodes answer. 0 = unset for clienttype; on_mobile
     *  is tri-state (1 yes, -1 no, 0 unset). Unset falls through to the
     *  revconfig profile's `[features]`, then to the platform default (mobile
     *  on Android, the desktop client elsewhere). TORIRS_CLIENTTYPE /
     *  TORIRS_ON_MOBILE outrank all of them. */
    int clienttype;
    int on_mobile;
    /** `[ui:boot] window` — initial canvas AND window size, 0 = unset (the
     * 765x503 fixed frame). Only meaningful in resizable mode, where it is the
     * size the gameframe is laid out at before the user touches anything.
     * --window overrides, and TORIRS_ROOT_SIZE overrides both. */
    int window_w;
    int window_h;
    /* ---- One member per app: the launch options only that app reads. Each
     * type is plain data in its app's folder and names no App. ---- */
    /** The game client's network options (`[net:boot]`, --connect ...). */
    struct NetClientConfig net;
    /** The map editor's options (`[editor:boot]`, --maped ...). */
    struct MapEditorConfig map_editor;
    /** The offline interface viewer's options (the boot interface, the
     *  `[ui:gameframe]` / `[ui:varc]` stand-ins, the headless preview). */
    struct OfflineViewerConfig viewer;
    /** The replay viewer's options (`[cache:boot] dir_for_revision`). */
    struct ReplayConfig replay;
};


/* enum AppState and enum AppScreen: @see game/rs_client_phase.h. */

struct App
{
    struct AppConfig cfg;

    /*
     * Every cache id this client knows by name rather than by literal: the
     * settings panel's apply scripts, the XP counter's interface, the human
     * ready animation, the fonts the overlays draw in. Parsed from the same
     * RevConfig sources the UI tree is built from, but kept for the whole
     * session because these are read long after Task_UITreeBuild has been
     * freed. RevConfigRefs_Get returns -1 for anything this revision does not
     * declare, and -1 means the feature is off here — never "use a default".
     */
    struct RevConfigRefs revconfig_refs;

    /*
     * The other half of the same profile: what this revision's client DOES,
     * rather than which id it does it to. `[features]` states the era table
     * (pathing, mover, painter radius); `[camera]` states what the world camera
     * lets the player do — the 2004 frame has no zoom and no middle-button
     * rotate, and saying so is what stops this client adding gestures the
     * revision never had.
     *
     * Held for the session for the same reason the refs are: the camera policy
     * is read on every wheel notch, long after Task_UITreeBuild is gone.
     */
    struct RevConfigProfile revconfig_profile;

    /*
     * The third half: which NODE a semantic name means.
     *
     * `[iface:…]` answers "which id is the XP panel" and stops there, because
     * an id is all the client needed for the things it names. A plugin needs
     * more -- it wants the report button's rectangle, and whether the logout
     * screen is up -- and those are components, not interfaces, and on a CS2
     * lane one of them may be a node a script built with no cache record at
     * all. So a role is a name bound to a matcher chain, resolved against the
     * live tree on ask. @see ui/uitree_role.h.
     *
     * Session-lifetime, and deliberately not on the tree: the table is a fact
     * about the REVISION, and it has to survive the gameframe rebuild that
     * throws the tree's nodes away.
     */
    struct UITreeRoleTable ui_roles;

    /* Phase 1: task runtime + disk (created first, freed last). Exactly one of
     * the two disks is live, per cfg.cache_kind.
     *
     * The three task queues, named for what each promises a caller:
     *
     *   packet_runner      it happens in the order the server sent it
     *   frame_runner       it runs now, as far as it will go, every frame
     *   background_runner  it gets the time a frame has left, for as many
     *                      frames as it takes
     */
    /**
     * The frame queue: parallel, pumped to a standstill every App_RunOnce with
     * no time limit (App_PumpAsync), and settled whole when a client script is
     * on it (app_settle_cs2_frame) -- a frame is withheld until its
     * render-blocking tasks are done. Asset loads, client scripts, sign-in,
     * file saves. Owns queue + io + px.
     *
     * Its had_work latch (TaskRunner_HadWork): tree-affecting async work (CS2
     * hooks, transmits) is in flight; when the queue next goes idle the tree
     * gets a refresh pass.
     */
    struct TaskRunner frame_runner;
    /** The packet queue: per-packet exec tasks and interface slot
     * mounts. Own queue + io slots, SHARED px with `frame_runner`. Head-only
     * execution makes it a strict FIFO — packet application order is
     * preserved across IO yields, and a mount enqueued by a packet runs
     * before the next packet is even popped (the pump only pops a new packet
     * when this queue is idle).
     *
     * Its had_work latch (TaskRunner_HadWork): the serial packet/interface
     * transaction yielded on real external IO. Its partially-applied tree is
     * not eligible for frame publication. */
    struct TaskRunner packet_runner;
    /**
     * The background queue: work nothing in the frame waits on. Parallel, own
     * queue + io, SHARED px.
     * Given time rather than drained -- a floor slice every App_RunOnce, and
     * whatever slack the platform loop has before its frame deadline
     * (App_RunBackground) -- so neither app_settle_cs2_frame nor the packet
     * pump ever steps it. A job on it that must be done by a deadline is
     * finished by its owner (the world job, app_world_job.c). Nothing settles
     * a transaction on it, so its had_work latch is never set.
     */
    struct TaskRunner background_runner;
    /** The pending index: every live parallel task on frame_runner, found by
     *  what it is for and cancelled by key or scope; it holds no data of its
     *  own (game/rs_pending.h; app/app_parallel_task.h). */
    struct RS_Pending* pending;

    /* Phase 2: asset pipeline. The disk (or the on-demand stream) and the
     * build cache that backs `provider`; everything downstream sees only the
     * provider. @see engine/cache_mounts.h. */
    struct CacheMounts cache_mounts;
    struct CacheProvider* provider;
    /** Lit model-instance LRU (spot/npc/loc/obj/player). Cleared at map build. */
    struct TorirsModelInstCache model_inst_cache;
    /**
     * Entity-info decode scratch (game/entity_info_scratch.h): one op list and
     * its byte arena per decoder, reused across packets rather than allocated
     * and freed for each. PLAYER_INFO and NPC_INFO both arrive every server
     * tick. Borrowed by the running exec task and handed back on its free;
     * owned here so teardown reclaims them (Task_EntityInfoScratchFree).
     */
    struct EntityInfoScratch entity_info_scratch;

    /* Phase 3: scene + bridge. */
    /** Runs scripted projectiles' curve clientscripts (app_world_curve);
     *  acquired on the first one, NULL until then. */
    struct CS2VM2* curve_vm;
    struct ToriDraw_Scene* scene;
    struct UITreeSceneBridge bridge;

    /*
     * The software renderers, made once and re-pointed at a buffer per frame.
     * They carry the outline/shadow cache and the blit scratch across frames,
     * which is the whole reason they are not made at each render.
     *
     * Two of them because the targets are independent: `soft` paints the game
     * canvas from App_Render, while the chrome compositor's one rasterises retained
     * CUSTOM widget bitmaps for WEB/BROWSER without disturbing the game
     * renderer (@see ui/chrome_compositor.h).
     */
    struct ToriPlatform_Renderer_Soft3D* soft;

    /* Phase 4b: world sim + builder (needs provider + scene + varps; the
     * World references assets and scene elements by integer id only). */
    struct World* world;
    /** World_SeqSource binding: which scene's animation registry answers
     *  "how long is this seq's frame", and which provider resolves a spotanim
     *  to its seq. Rebound whenever a world is handed the source. */
    struct WorldSeqSourceToriDraw seq_source;
    struct WorldBuilder* world_builder;
    /**
     * Multi-world view registry (OSRS world entities / sailing — worldview.h).
     * Slot 0 is the root view and BORROWS `world`/`world_builder` above, so
     * the hundreds of existing app->world call sites stay valid; views spawned
     * by WORLDENTITY_INFO (later phase) own their pairs and free them on
     * despawn.
     */
    struct WorldviewRegistry worldviews;
    /**
     * Live world entities (sailing boats) + each view's server-ordered entity
     * list, fed by WORLDENTITY_INFO and advanced by Wevs_Frame every frame
     * (wev.h). An entity's id doubles as its view id in `worldviews`.
     */
    struct Wevs wevs;
    /** How a flattened hull is drawn: the live deob path or a prebuilt
     *  outline, chosen at init by TORIRS_WEV_FLAT (render/wev_flat.h). */
    struct WevFlat wev_flat;
    /** The helm's revision ids and the native bearing-marker arrows
     * (game/rs_sailing_hud.h). Scene-owned elements are claimed across root
     * rebuilds; a chosen heading remains highlighted for 30 cycles. */
    struct RS_SailingHud sailing_hud;
    /** WorldEntityConfig table (config archive 72), loaded once at boot by
     * CreateTask_Dat2WevConfigLoad. Empty on a pre-sailing cache. */
    struct WevConfigTable wev_configs;
    /** `TORIRS_WEV_DEBUG=1` latch: the last aboard view (Wevs_AboardView) a
     *  trace line named. See world/wev_aboard_trace.h. */
    struct WevAboardTrace aboard_trace;
    struct PaintersBuffer* painter_buffer;
    /** The renderer's facts and the server's ZBUF choice (render/render_mode.h).
     *  @see App_SetWorldRenderMode, App_SetRendererAnimatesTextures,
     *  App_SetSoftwareRsZBuffer */
    struct RenderMode render_mode;
    /** The viewport the TORIRS_PAINTER_CULL=baked cull map was last baked for
     *  (render/painter_cull_bake.h). */
    struct PainterCullBake painter_cull_bake;
    /**
     * The world views, the frame cursor, the runner/watcher split and the
     * WORLD node's emit desc (render/world_views.h). `world_views.frame` is
     * the view the frame being built reads and writes, chosen where a stage
     * begins: the presented draw and the emit walk set the presented view,
     * the offscreen runner frame and the camera follow step set theirs, and
     * each puts views[0] back -- every other moment (logic, the plugin pump,
     * the driver's verbs) sees views[0].
     */
    struct WorldViews world_views;
    /** What the device is, stated at boot: a finger camera, the touch
     *  policy (resolved in App_Init) and an on-screen keyboard.
     *  @see platform/device_caps.h. */
    struct DeviceCaps device_caps;
    /** Whether there is a world, whether a load was tried, whether one runs
     *  with the viewport blanked, and whether the server asked for it.
     *  @see engine/world_builder/world_load_state.h. */
    struct WorldLoadState world_load;

    /**
     * The map editor, or NULL -- which is every boot that did not ask for one,
     * i.e. every normal client run, and an editor boot whose ToriRSMapEd
     * could not be reached.
     *
     * Owned. Made in App_Init's editor block beside the session it holds
     * (struct Editor: present only when `[editor:boot] content_dir=` named a
     * content tree or server=tcp dialled a daemon), freed in App_Shutdown.
     * While it exists the squares the world builder meshes come from that
     * tree's `.jm2`/`.jl2` text rather than from the baked cache, and edits
     * are saved back as text. It never speaks to a game server: an editor boot
     * states no `[net:boot]`, so the whole net stack is simply not
     * constructed. It holds the command panel (built in `dev_overlay`'s
     * chrome) and the Place tool's hover ghost. Shared code asks the map
     * editor queries (app/app_mapeditor_app.c), never this pointer.
     * @see editor/map_editor_app.h.
     */
    struct MapEditorApp* map_editor;

    /**
     * Damage rectangle: the union of every region whose pixels can change
     * while the emit list stays byte-identical, in canvas coordinates.
     * `damage.valid` is 0 when the whole canvas must be treated as damaged.
     *
     * The retain gate proves the *command list* is unchanged. It does not
     * prove the *pixels* are, and the difference is exactly three things:
     *
     *  - the WORLD viewport, where models animate and the camera drifts under
     *    a list that never mentions either;
     *  - the MINIMAP, rebuilt from a rotating source every frame;
     *  - every desc holding a host-owned pointer (minimap dots, entity
     *    overlays, worldmap tiles, debug prims), whose desc bytes are stable
     *    while the buffer behind them is refilled -- the same descs
     *    `UITreeEmitBuffer::volatile_refs` counts, for the same reason.
     *
     * Everything else in a retained frame is already on screen, so it does not
     * need presenting. (It is still cleared and redrawn: the software renderer
     * used to clip its clear and its draws to this box too, and no longer
     * does.) Membership is decided
     * by testing the pointers rather than by listing the kinds that set them,
     * so a kind added later cannot quietly opt itself out. @see
     * app_compute_damage.
     *
     * The region carries the same damage twice, as a bounding box and as up
     * to four rectangles, because the two answers cost differently: the two
     * live regions are the world viewport and the minimap, which sit at
     * opposite ends of the same rows, so their bounding box swallows the
     * sidebar strip between them and is 240,195 px against the 193,901 px the
     * two rects actually cover. For reference the Java client, which keeps a
     * separate PixMap per region, clears 170,048 px and presents 196,880 px
     * per frame (Client.gameDraw / Client.java:5122).
     */
    struct ToriRS_DamageRegion damage;

    /** The plugin bridge's memos of the gameframe: slot nodes, interned
     *  frame-role and facet ids, tab stones and the binder's last pass, each
     *  with the counters it is reused under (plugin/plugin_frame_binding.h). */
    struct PluginFrameBinding plugin_frame_binding;
    /* Minimap widget: where the last emit walk drew the map, the angle it
     * blitted with, the destination flag tile (reference minimapFlagX/Z), the
     * baked map sprite it blits and the MINIMAP_TOGGLE state (see enum
     * AppMinimapState, game/rs_minimap_state.h). ui/minimap_view.h. */
    struct MinimapView minimap;
    /* Per-frame minimap overlay dots, filled by the GET_MINIMAP_DOTS host
     * request during the emit walk and consumed by the same frame's draw. */
    struct MinimapDots minimap_dots;
    /*
     * Per-frame overlay primitives for the two GAME surfaces: the world list
     * the health bars, hitsplats and editor marks land in, and the canvas list
     * a plugin's own drawing goes to above the interfaces.
     *
     * Two lists rather than one with a flag, because the two are cut to
     * different boxes and the clip travels on the DESC rather than on the item
     * -- one desc carries one clip, so two clips need two descs. Which list a
     * push lands in, and why a panel's drawing lands in neither, is
     * render/overlay_stage.h.
     */
    struct OverlayStage overlays;
    /**
     * Retained custom-page drawing, isolated from all three game lists.
     *
     * Redrawing one dirty custom well replaces only that serial's run;
     * unchanged wells survive without invoking their plugin again. The stage is
     * committed only after the generation/serial still name the selected page.
     * @see render/panel_overlay_stage.h.
     */
    struct PanelOverlayStage panel_wells;
    /**
     * Tile markers that belong INSIDE the 3D pass, and the tiles they mark.
     * app_overlay_push routes primitives here while the stage is open; the
     * placement against the painter buffer runs once a frame
     * (app_world_tile_marks_place).
     */
    struct TileMarkStage tile_marks;
    /** The OUTBOUND command bus: requests the App makes of the platform
     *  (TORIRS_CMD_PLATFORM_*). NULL in a harness with no platform, which then
     *  asks for nothing. @see App_SetPlatformBus. */
    struct ToriRS_CmdBus* platform_bus;
    /* The world map surface: which regions are on screen and in what order
     * (game/rs_worldmap_view.h owns that rule), this frame's blits, the baked
     * regions behind them, the overview pane, and the drag-to-pan box the emit
     * walk records because the widget is sized by the map's own scripts.
     * game/rs_worldmap_surface.h. */
    struct RS_WorldMapSurface worldmap;

    /* Persistent IF_SETTEXT store (reference keeps text on the shared
     * IfType.list config): the server sends journal/bonus texts BEFORE the
     * owning interface mounts, so they must survive and re-apply whenever
     * tree topology changes (tree->generation). */
    struct UIIfTextStore if_texts;

    /* Persistent IF_SETHIDE store, same reasoning as if_texts (the reference
     * keeps `hide` on the shared IfType.list, so a hide sent before the owning
     * interface mounts still takes effect once it does). The chat option
     * dialogs ship two sword-decoration layers — a narrow centred pair and a
     * wide corner pair — and the server picks one with IF_SETHIDE right after
     * IF_OPENCHAT; dropping it left the cache default (narrow) on screen. */
    struct UIIfIntStore if_hides;

    /* Persistent IF_SETCOLOUR store, for the reason the two above exist: a
     * colour written before its interface has mounted has no node to land on,
     * and IF_SETCOLOUR used to be the one setter in this family that applied
     * straight to the tree and was lost when it missed. The Theatre's title
     * card is what found it — server mounts the overlay and writes the red in
     * the same tick, and the red never arrived, so the card came up in the
     * cache's authored black. */
    struct UIIfIntStore if_colours;

    /* Persistent IF_SETEVENTS store. At rev 230 nothing is clickable by
     * default — the server declares which slots of which component accept
     * input, and it does so before the interface finishes mounting, so the
     * masks have to survive until there is a tree to apply them to. Without
     * this a dialogue renders correctly and swallows every click. */
    struct UIIfEventTable if_events;

    /* Persistent interface-model stores (reference keeps
     * model1Type/model1Id on IfType.list and re-resolves getModel every draw):
     * the head packet arrives before the chat interface mounts, so the request
     * must survive and re-apply on tree-topology changes. The async load task
     * makes the composited head scene model available; the per-frame poll binds
     * it onto the MODEL node once mounted (and after each remount). Also the
     * IF_SETPLAYERMODEL_* compositions, and the appearance the local player's
     * clientCode-328 figure was last built from (app_player_model_poll).
     * @see ui/uitree_if_models.h */
    struct UIIfModels if_models;

    /* Placeholder requests a CLIENTSCRIPT asked for, parked until a logic
     * tick releases them. A script runs inside the settle, and the settle
     * drains the asset runner, so a placeholder queued from one would be
     * picked up by the very frame it was meant to leave -- app_placeholder.c
     * has the measurement. One pointer: the park and its key are
     * inv/widget_icon_park.h, its burst bound is that unit's. NULL until the
     * first request. */
    struct WidgetIconPark* placeholder_park;

    /* Projectile hotkey latch: first press = src tile, second = dst + fire. */
    /*
     * The loc lane: zone loc changes apply in the order they were enqueued,
     * but load off the packet queue. A task takes a ticket when it is queued
     * (LocLane_Take), loads whatever it needs on the parallel asset
     * runner alongside the others, and only then waits for the lane's applied
     * count to reach the ticket before it -- so a LOC_DEL lands after the add it
     * deletes, and nine rock changes at the Inferno's door cost one model
     * wave instead of nine, none of it parking the packets behind them. A
     * change not yet applied is the deob's pending spawn, which a LOC_ANIM
     * may hand its sequence to (App_WorldSceneryAnim).
     * See app_spawn_loc_lane_queue (app_world_edit.c) and world/loc_lane.h.
     */
    struct LocLane loc_lane;
    /** The side-apps: the debug tools, their key bindings and their state
     *  (sideapp/side_app.h). Registered by App_Init through app/app_tools.c,
     *  the one place that lists them; freed with the dev overlay. */
    struct SideApps* side_apps;

    /* RevConfig tree builder: alive for the process, not just the build task —
     * it owns the sprite/font name registries bake resolved through. Unused
     * (UITreeBuilder_Active false) when the tree came from a cache interface
     * open. */
    struct UITreeBuilder builder;

    /* Phase 4: game state. */
    struct UITree* tree;
    struct InvManager invs;
    struct VarPManager varps;
    /* Hitsplat types: type -> sprite id, from config group 32. See
     * src/game/rs_hitsplat.h for why a named sprite archive is not enough. */
    struct RS_Hitsplats hitsplats;
    /* Healthbar types (config group 33): the overhead bar's sprites, its fill
     * denominator and its fade. See src/game/rs_healthbar.h for why the fill
     * value the server sends is not the bar's pixel width. */
    struct RS_Healthbars healthbars;
    /** Ambient soundscapes (config group 15). Empty before OldSchool 231, and
     *  the audio layer reads empty as "AMBIENTSOUND_START ids are effect ids". */
    struct RS_Soundscapes soundscapes;
    struct VarCManager varcs;
    struct LootStore loot;
    struct RS_PlayerStats stats;
    struct RS_CS2Host host;
    struct RS_CS1Host cs1_host;
    /** Runtime interface slots (tabs, modals, chat) — reference Client-TS
     *  mainModalId/sideOverlayId/sideTab fields — and the component the last
     *  modal mounted on, with the optimistic modal close's re-entrancy guard. */
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
    /** Pre-game screen state: which title screen is up, what is typed into
     *  its fields, and the reply lines the login response filled in.
     *  @see AppScreen for how it relates to the session as a whole. */
    struct RS_Title title;
    /** Getting from that screen to the world, once: the credentials, the
     *  one automatic submit, the frame between a submit and its dial, and
     *  the per-field scratch the host requests hand out. @see
     *  game/rs_title_session.h for why each of those is a rule. */
    struct RS_TitleSession title_session;
    /** The account flow, on a world whose manifest says `[net:boot]
     *  auth=oauth`: the device's accounts and the site session, each read
     *  once, the one sign-in in flight, and the title's Play Now cards. @see
     *  game/rs_title_accounts.h. */
    struct RS_TitleAccounts accounts;
    /** The title screen's own track and whether this client started it.
     *  @see game/rs_title_music.h. */
    struct RS_TitleMusic title_music;

    /** What each login rejection means, in this revision's words. Loaded from
     *  the profile beside RevConfigRefs and alive for the whole session. */
    struct RS_LoginReplyTable login_replies;
    /* What this revision loads before the title screen: the profile's own
     * [preload:] list, in order. @see RS_PreloadTable. */
    struct RS_PreloadTable preload;
    /**
     * The title screen's two braziers, or NULL when none is burning.
     *
     * A pointer because the simulation carries four 128 KB buffers and the
     * session only wants them while the title screen is up; it is freed the
     * moment the gameframe roots.
     */
    struct TitleFlames* flames;
    /** The typed chat line: whether the client-painted one has the keyboard,
     *  and whether the web's cache one is open and what its prompt covers.
     *  @see app_chat_focus_tick, app_chat_lock_keys. */
    struct UIChatLine chat_line;
    /** Minimenu chat-line seam (points at app_chat_line_at). */
    struct RS_MinimenuChatSource chat_source;
    /** Server-notify callbacks for IF1 button clicks (NULL until net). */
    struct RS_IF1ButtonSink button_sink;
    /** Networking subsystem; NULL until --connect enables it, and whether
     *  this run talks to a server is the handle's (ToriRS_Network_Enabled,
     *  App_NetEnabled). Owned heap pointer so app.h need not include the net
     *  headers. */
    struct ToriRS_Network* net;

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
    /** When we last put bytes on the wire (net/net_keepalive.h). Drives the
     * NO_TIMEOUT keepalive, which the reference sends only after a full second
     * of outbound silence -- any real packet resets the wait. */
    struct NetKeepalive net_keepalive;
    /**
     * When the session is dead, when to dial again, and when to stop.
     *
     * The three ways a session can end are not interchangeable and none of
     * them is observable after the fact -- a session that ends and comes back
     * looks exactly like one that was never lost. See net/net_link_watch.h.
     */
    struct NetLinkWatch net_link;
    /** A replay seek's chase (TORIRS_CONTROL_REPLAY_COORD, replay/replay_chase.h):
     *  the world packets are withheld, so the local player's tile is the
     *  recording's, stated per tick by rs_gameproto_exec.c, and `coord` reads
     *  it (app_tick.c) until the seek's world keyframe clears it
     *  (replay/replay_world_apply.c); and the cycles each chase tick charges
     *  the client clock. */
    struct ReplayChase replay_chase;
    /**
     * The logout the player asked for and the world hop the server ordered
     * (net/session_end.h). The request is raised by the CS1 logout clientCode
     * and by the CS2 host's LOGOUT opcode and drained by the logic tick
     * (app_logout_tick); the armed wait is what app_net_tear_down_session
     * reads to tell the server's answer apart from a connection that was
     * lost. A LOGOUT_TRANSFER's hop is taken by app_net_link_watch at the head
     * of the next frame (app_net.c). @see App_Logout.
     */
    struct SessionEnd session_end;
    /** Client-behaviour era table (src/features/features.h): the app's own
     *  copy, so a `[features:boot]` per-item override lands somewhere
     *  writable and the era singleton stays pristine. App_Init copies the
     *  resolved era in and applies the overrides; it is resolved on every
     *  boot, unlike `net`, because an offline click still has to pick an
     *  approach model. Zeroed, and so not ToriRS_Features_Resolved, until
     *  then. */
    struct ToriRS_FeatureTable features;
    /** Effective lighting behaviour after era + `[render:light]` merge, and
     *  who draws the mouseover line (features/feature_facts.h). Call sites
     *  read the lighting here rather than features->npc_light_* directly so a
     *  manifest override wins without mutating the era table. */
    struct ToriRS_FeatureFacts feature_facts;

    /* Phase 5: frame state. */
    struct UITreeHost ui_host;
    struct UITreeEmitBuffer emit;
    /** Previous publication identity for the retained-emit gate, and whether
     * this frame retained the list (UITree_EmitRetainGateRetained). Per-App (not
     * function statics) so a second client or a reinitialized App cannot inherit
     * another tree's quiet verdict. */
    struct UITreeEmitRetainGate emit_gate;
    struct UIInteraction interact;
    struct InterfaceOpenStats open_stats;
    struct UICross cross;
    /* The touch marker: armed with the cross (app_cross_show), drawn instead
     * of it on a lane whose profile says `cross@mobile` -> inkwell. @see
     * ui/uitree_ink.h. */
    struct UIInk ink;
    /** Mouseover text under the pointer, rebuilt every frame (reference: CS2
     * script 4726 rebuilds it every client cycle). */
    struct UIHoverText hover_text;
    /* Developer overlay (src/ui/README_DEBUG_OVERLAY.md): the developer
     * chrome instance and the All Settings pickers. The frame-time panel's,
     * the loc editor's and the map editor's panels are built in its chrome
     * too. Owned: made in App_Init, freed in App_Shutdown. See
     * ui/dev_overlay.h. */
    struct DevOverlay* dev_overlay;
    /**
     * Scripting/plugin host (src/plugin/). Always built in App_New: the nxt-*
     * builtins live in it whatever the plugin switch says. Seam calls still
     * test it for NULL, which a test App without a host relies on.
     */
    struct ToriRS_PluginHost* plugins;
    /**
     * What this boot handed the plugin host: the USER plugins switch, where
     * plugin settings are written, and this boot's feature values before any
     * plugin set one -- snapshotted lazily on the first plugin call, which is
     * always after App_Init (plugin/plugin_boot_config.h,
     * app_plugin_feature_capture).
     */
    struct PluginBootConfig plugin_boot;
    /**
     * quest-driver event ring (src/plugin/torirs_plugin_drive.h).
     *
     * Present in every build, stamped unconditionally from the sites marked
     * `DRIVE_STAMP:` across the tree, and read by cursor by the test-only
     * driver. Declared here rather than allocated on demand for the reason
     * every stamp site depends on: a stamp must be a struct store with no
     * branch on whether a test happens to be running, or it is a stamp that
     * rots the first time nobody is looking.
     */
    struct App_DriveRing drive_events;
    /**
     * The plugin window: its chrome instance, presentation, page rows, choice
     * pool and build stamps. Allocated by App_Init, freed by App_Shutdown,
     * never NULL in between. @see plugin/plugin_panel.h.
     */
    struct PluginPanel* plugin_win;
    /**
     * The merged chrome display list, the plugin page's surface picture and
     * the renderer that draws plugin chrome. Allocated by App_Init, freed by
     * App_Shutdown, never NULL in between. @see ui/chrome_compositor.h.
     */
    struct ChromeCompositor* chrome_compositor;
    /**
     * The cache's highlight groups, resolved against this frame's world.
     *
     * Rebuilt at the start of each api->highlight_next walk rather than every
     * frame: the groups are almost always empty (nothing has been switched on)
     * and the resolution is a walk of the npc, loc and ground-item pools, so
     * it is not work to do for nobody. One walk per frame is the shape the
     * api documents.
     */
    /**
     * What the three tile-highlight triggers were last fired for.
     *
     * Clientscripts 5197 / 5203 / 5209 each clear their highlight group and
     * re-add one tile, so they are fired on the EDGE of their subject changing
     * rather than every frame -- three dispatches and six highlight ops a
     * frame to restate what has not moved is not free, and both the pointer
     * and the player sit still for most of them.
     *
     * The current-tile one watches the local player's ROUTE and not `coord`,
     * because the tile 5203 marks is the route's newest entry while walking:
     * the server-side tile, which is ahead of the rendered one. See
     * app_cs2_local_route_signature for what is folded into it.
     *
     * Seeded to a value none of the three can take, so the first frame after
     * login fires all three: a group that starts empty has to be filled once,
     * and the "switched off" branch has to run once too.
     * (game/rs_tile_highlight_watch.h)
     */
    struct RS_TileHighlightWatch tile_highlight_watch;

    /**
     * Tiles whose ground-item pile changed since the last logic tick, as
     * packed absolute coords.
     *
     * The ground-items overlay is rebuilt per TILE, by a cache script nothing
     * in the cache calls (`[script:ground_items_overlay]`) -- the same
     * arrangement as the three tile refreshers above, because a pile changing
     * is something only the client knows. The list is drained once per tick
     * rather than fired from inside the zone executor: one OBJ_ADD burst can
     * touch the same tile several times, and a script per packet would rebuild
     * the same overlay three times over.
     *
     * `ground_items_refresh_all` is the overflow and the whole-scene case
     * together -- a rebuild shift, a settings change, more tiles in one tick
     * than the list holds. It walks every stack in the pool, which is bounded
     * by the scene and is why the list can stay small.
     */
    struct RS_GroundItemsDirty ground_items_dirty;
    /** The two carrier varps behind the ground-items settings, resolved from
     *  the revconfig varbits once the varbit table is loaded, the values last
     *  seen in them, and the aux lists' revisions last seen
     *  (game/rs_ground_items_dirty.h). */
    struct RS_GroundItemsSettings ground_items_settings;
    /** The mouseover subject the debug report line was last printed for,
     *  folded to one int (game/rs_mouseover_report.h). */
    struct RS_MouseoverReport mouseover_report;
    /** Raised when a scripted overlay's layer has gone missing from the tree,
     *  taken by the next overlay pass, which re-fires every ADD trigger
     *  (game/rs_client_trigger.h). */
    struct RS_ClientTriggerRefire client_trigger_refire;

    /** The highlight groups resolved to things on screen: the list a plugin
     *  walks and the loc highlights kept across frames
     *  (plugin/plugin_highlights.h). */
    struct PluginHighlights resolved_highlights;
    /** Plugin-owned world objects, authored meshes and shipped models, each
     *  indexed by the handle the plugin holds, and whether any geometry moved
     *  since the last settle (plugin/plugin_geometry.h). */
    struct PluginGeometry plugin_geometry;
    /**
     * Deferred frame captures (api->screenshot), taken at the top of the NEXT
     * App_RunOnce (plugin/plugin_screenshots.h).
     */
    struct PluginScreenshots plugin_screenshots;
    /** When the plugin config store last moved and is not yet on disk
     *  (game/rs_save_debounce.h). Same settle delay as prefs_save, for the
     *  same reason: a script editing state every tick must not be fifty
     *  writes. */
    struct RS_SaveDebounce plugin_prefs_save;
    /* The fixed-step accumulator that turns wall time into logic cycles and
     * the cycles a frame-locked frame owes the next; the client's serials
     * (logic cycles run, App_RunOnce calls, the time of the last frame); the
     * fps window (frames drawn in the last whole second, which
     * SEND_PING_REPLY reports) and frames drawn since boot on every renderer
     * (App_NoteFrameDrawn) -- drawn, not iterated: the loop runs at the
     * pacer's rate whether or not a frame is drawn, so counting iterations
     * measures the pacer, not the screen; the idle timer; and the last few
     * frames' work (App_NoteFrameTime), which the frame-time panel averages
     * and App_LastFrameUs reads. See perf/frame_clock.h. */
    struct FrameClock frame_clock;
    /**
     * Render skip: the frame loop still runs input, net, the logic tick and
     * the plugins, but App_Render and the present are skipped unless a frame
     * has to be drawn. See App_RenderSkipFrame for what forces one and why.
     * Switched by TORIRS_RENDER_SKIP=1 (App_Init), `::renderskip` typed in
     * the chatbox, and api.drive.render_skip (t.render.skip).
     * See render/render_skip.h.
     */
    struct RenderSkip render_skip;
    /** The modifier keys as of the last input pump (Ctrl, reference
     *  keyHeld[5]), latched in app_frame.c for the move paths that run
     *  without the input, and this frame's input parked for the plugin api's
     *  key_held and the menu's Shift.
     *
     *  The input frame is a parameter to App_RunOnce rather than App state
     *  everywhere else, but a plugin handler fires from inside the overlay
     *  build and the menu build -- call sites several frames of stack below
     *  the one holding it -- and threading it through every one of them to
     *  answer "is shift down" would be a wider change than parking the
     *  pointer for the duration of the frame that owns it.
     *  @see input/torirs_frame_modifiers.h. */
    struct ToriRS_FrameModifiers frame_modifiers;
    /**
     * A finger's tap on the world that arrived before any frame had picked
     * at its pixel, held until one has (app_frame.c).
     *
     * A world click runs the default row of the menu built from the pickset
     * the LAST rendered frame hittested. A mouse hovers for frames before it
     * clicks, so that set is the pointer's. A finger has no hover: the set is
     * reset when it lifts, and a tap whose down and up land in one frame
     * arrives with an empty set -- "Walk here" to nowhere, no packet, no
     * cross. The iPad lost the first tap after every login that way
     * (2026-11-16, black box `world_click` pick n=0 stamp=none): frames are
     * slow while a swapped-in world bakes, so a quick tap fit in one.
     *
     * While one is held the pointer is kept present at its point so the next
     * frame picks there; the click runs once the set is stamped at that
     * point, or after TORIRS_WORLD_TAP_WAIT_FRAMES on whatever the set holds
     * then. See input/torirs_world_tap.h.
     */
    struct ToriRS_WorldTap world_tap;
    /** The next frame has to draw. @see render/redraw_request.h. */
    struct RedrawRequest redraw_request;

    /* Async lifecycle state (no blocking IO outside the platform pump). */
    /**
     * Whether the tree being baked has finished, which screen it is, and
     * whether a logout's bake keeps the last frame on screen instead of
     * painting: App_RunOnce returns no commit while that is held, and the host
     * presents its retained frame (the software surface re-uploads its
     * pixels; the GPU lanes leave their front buffer alone) until the title
     * tree can draw the login screen in one step. Raised by App_Logout,
     * cleared by the next bake that starts and by the bake that settles.
     * @see game/rs_client_phase.h, App_BootHoldsLastFrame.
     */
    struct RS_ClientPhase phase;
    /*
     * Boot pump accounting (TORIRS_BOOT_STATS) and whether the frame queue
     * still had work when the last App_RunOnce returned.
     *
     * The flag is read by the frame loop, which skips its pacing sleep while
     * it is set. The frame cap exists to pace the SCREEN; sleeping through
     * outstanding IO paces the download instead, and on a cold boot this
     * client streams its whole world through that pipeline -- 516 containers
     * for the rev-289 world -- so the cap was deciding how fast the game could
     * load. @see perf/boot_pump_stats.h.
     */
    struct BootPumpStats pump_stats;
    /** The boot bar, the root interface the bake mounts, the boot task's
     *  once-per-session preamble latch and the headless preview's outcome.
     *  @see game/rs_boot_state.h. */
    struct RS_BootState boot;
    /**
     * The outgoing root's mounts, kept across a re-root (App_OpenRootInterface).
     *
     * The deob keeps its InterfaceParent table when IF_OPENTOP changes the top:
     * the subs stay open, keyed by their old slots, and the IF_MOVESUBs that
     * follow relink each one into the new top (class415.method9481). The tree
     * here is rebuilt instead, so these records are what App_MoveSubInterface
     * finds a moved sub by. A record a re-root does not move is kept, as the
     * deob keeps it: it is in a slot only the old top has, and it reopens when
     * that top is the root again (app_detach_tree_mounts). The table's rules
     * are ui/uitree_kept_mounts.h.
     */
    struct UITreeKeptMounts kept_mounts;
    /** Raised when async work mutated the tree; App_RunOnce takes it with a
     * relayout + CS1 re-eval request + redraw. */
    struct UITreeRefresh tree_refresh;
    /** The boot report's first packet and the TORIRS_NET_CHEAT boot cheat:
     *  each acted on once per run (net/net_once.h). */
    struct NetOnce net_once;
    /** Every asset wanted and not here, with at most one load in flight for
     *  it (engine/asset_requests.h); the loaders are app/app_assets.c. */
    struct Assets* assets;
    /** Entity-sync bookkeeping (server slots -> world entities). */
    struct RS_EntitySync esync;
    /** Entity attached-graphic (SPOTANIM mask) combine state, keyed by the
     * entity's body scene element and its owner (engine/entity_spotanim.h);
     * app_entity_sync.c builds and swaps the models. */
    struct EntitySpotanims entity_spotanims;

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
    /** When prefs last moved and are not yet on disk (game/rs_save_debounce.h).
     *  A slider drag changes them every tick, so the write waits for the drag
     *  to settle rather than rewriting the file 50 times a second. */
    struct RS_SaveDebounce prefs_save;
    /**
     * Camera scripting (CAM_* packets, CAM_UNLOCK included): the targets,
     * eases, shakes, free camera and the read side's event ring
     * (render/cam_script.h). The packet handlers in rs_gameproto_exec.c set
     * it, app_camera.c steps the views from it, and the quest driver reads
     * its events (torirs_plugin_drive_pointer.c).
     */
    struct CamScript cam_script;
    /**
     * HINT_ARROW state -- the server pointing at something. type 0 = none.
     *
     * Drawn by `app_overlay_build_hint_arrow`, which is also the whole of what
     * this client can do for All Settings rows 272 (worldmap marker) and 273
     * (world arrows): neither has a reader in the cache or in the NXT engine,
     * and the only marker family the reference carries is the hint arrow's own
     * (`GraphicsDefaults::GetSpriteHintMapMarkersID` and its two siblings). So
     * the server decides whether to point, and this decides how to draw it.
     */
    struct RS_HintArrow hint_arrow;
    /**
     * SET_PLAYER_OP rows and the two Controls-settings "Attack" options
     * (game/rs_player_ops.h). The options are derived state, not varp storage:
     * varp clientcode 18 / 22 assigns them and nothing else does, so a
     * VARP_RESET leaves them alone exactly as the reference does -- it resets
     * them only on the full game-state reset (app_attack_options_reset),
     * never from its zeroed table.
     */
    struct RS_PlayerOps player_ops;
    /* SET_INTERACTION_MODE / RESET_INTERACTION_MODE (deob class61): what a
     * click may do in each world view (game/rs_interaction_mode.h). A logout
     * puts them back to 1/1 (class61.method1386): App_InteractionModesReset. */
    struct RS_InteractionMode interaction_mode;
    /* What the server says once or rarely in a session: LAST_LOGIN_INFO, the
     * reboot countdown, the site strings, tracking, SET_MULTIWAY.
     * game/rs_session_info.h. */
    struct RS_SessionInfo session_info;
    /** CAM_SKYBOX: wanted, asked for and built (render/skybox_slot.h). */
    struct SkyboxSlot skybox;
    /* Revision 239 SET_NPC_UPDATE_ORIGIN. NPC low-resolution deltas are
     * relative to this scene-local tile, not implicitly to the local player.
     * See game/rs_npc_update_origin.h. */
    struct RS_NpcUpdateOrigin npc_update_origin;
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
     * there; `App_FlushPendingClientScripts` is also called when the pipeline
     * runs dry, which is what carries revisions that send no fence (lc254).
     *
     * The payload is a flat POD, so entries are held by value. Overflow runs
     * the script immediately rather than dropping it — degrading to the old
     * ordering is a cosmetic bug, losing a script is not.
     *
     * The hold must never reorder a script against an interface MOUNT change.
     * The reference runs a RUNCLIENTSCRIPT inline, in wire order (rev-184
     * deob: the packet handler builds the ScriptEvent and calls runScript
     * before it reads the next packet), so a script the server sent before an
     * IF_CLOSESUB runs while its interface is still up. Held past the close
     * here, its first `cc_find` on the closed group auto-loaded that group
     * back (rs_cs2_yield_if_group_missing) and the interface the server closed
     * stayed in the tree -- `t.ui.await_close` timed out on Scrambled!'s last
     * jigsaw piece (b72). So `app_pump_net_packets` releases the held scripts
     * onto the packet pipeline AHEAD of any packet
     * `gameproto_packet_changes_interface_mounts` names: they run before the
     * mount change, against every packet of the tick before them. A
     * component property write (`gameproto_packet_writes_interface_component`:
     * IF_SETTEXT, IF_SETHIDE, ...) releases them too, so a script and a packet
     * writing the same component keep wire order (Jagex's journal reset before
     * its rows, RSProx index 4400 [2130]). Inventory and varp writes do not
     * release them -- that is the price checker case above, which the hold
     * exists for.
     *
     * The queue itself is game/rs_clientscript_queue.h, including the backstop
     * cycle a fence that never arrives is measured against.
     */
    struct RS_ClientScriptQueue pending_clientscripts;
    /**
     * The server tick fence: has this connection sent SERVER_TICK_END, is a
     * tick half applied (the renderer keeps the preceding committed frame
     * while it is), was it opened on varps alone (App_WorldJobWanted), and
     * how many ticks have executed, in wire order -- at SERVER_TICK_END, or at
     * PLAYER_INFO on a revision that sends no fence. A loc change stamped with
     * an earlier tick than the packet reading it is one the rev-239 client has
     * already built (it builds a pending spawn in the client cycle that read
     * it, right after the packet loop), however long this client's lane has
     * held it. See game/rs_server_tick.h.
     */
    struct RS_ServerTick server_tick;
    /**
     * Who has taken a part of this frame's input (input/torirs_input_claims.h):
     *
     *   keys     a revconfig hotkey binding, or a map editor chord, acted on
     *            the OSRS key. Debug world hotkeys share the digit row with
     *            the rev-254 tab bindings, so they check this and stand down
     *            rather than firing both.
     *   frame    taken by App_RunOnce once the stable-tree gate has been
     *            crossed and the current host input frame has reached
     *            interaction, and by every chrome or camera gesture that acts
     *            on it; App_InputFrameConsumed reads it.
     *   pointer  this frame's pointer is over chrome drawn in this canvas (a
     *            developer panel, or the plugin window under the buffer
     *            executor), so the game sees none of it. Latched once per
     *            frame after the chrome ticks; see app_chrome_wants_pointer,
     *            which is also what computes it.
     */
    struct ToriRS_InputClaims input_claims;
    /**
     * The zone base for follows-mode zone packets, and the zone sub-packets
     * that arrived while the world was still async-loading.
     *
     * The reference cannot receive one mid-build — its scene build runs
     * synchronously inside the packet loop, so every zone update processes
     * after the build it follows. Our REBUILD is an async task, and dropping
     * what arrives in that window diverges: the Inferno's flank walls and
     * rubble land two ticks after REBUILD_REGION and vanished whenever the
     * load was still in flight (docs/ORANGE_WEDGE.md §17). Each entry keeps
     * the zone base and header plane AS OF ARRIVAL. Revision 239's header can
     * target a plane other than the player, so resolving it at replay time
     * would overwrite locs in the wrong plane. It also keeps the packet-apply
     * cursor as of arrival: a queued packet can be replayed ticks later, after
     * SERVER_TICK_END has reset the live cursor, so resolving it at replay
     * time would land a boat's zones in the root. Replayed in arrival order
     * once load_complete flips. See game/rs_zone_inbox.h.
     */
    struct RS_ZoneInbox zone_inbox;
    /** Last REBUILD_NORMAL centre zone (deob field1192/field474 /
     * Client-TS mapBuildCenterZoneX/Z), -1 until the first rebuild; same-zone
     * packets early-out when the world is already active. And the root
     * scene's dynamic-rebuild template grid, KEPT: the world load consumes it
     * and the packet's copy is freed, but it is the only thing a client is
     * told about an instance's layout (the CoX helper's room list is read from
     * it). See game/rs_rebuild_region.h. */
    struct RS_RebuildRegion rebuild_region;

    /**
     * The paced catch-up after a background rebuild's commit
     * (engine/world_builder/rebuild_pace.h), on the background queue's clock.
     *
     * The commit frame does the commit and nothing else: no packet is popped
     * on it. For the window after it, the packets held behind the REBUILD
     * and the scripts they dispatch are applied a budget of work a frame,
     * stopping between packets and between tasks. A frame that stops part
     * way through a server tick is latched like any frame whose tick has not
     * finished arriving (App_FrameSettled), so nothing half applied is shown.
     * @see App_PaceArm, App_PacedDeadline.
     */
    struct RebuildPace pace;
    /**
     * The background world rebuild (engine/world_builder/world_rebuild_job.h):
     * how a server REBUILD is done, and the job in progress, if any.
     *
     * The mode is the world's `[features:boot] world_rebuild` (background
     * unless it says blocking), TORIRS_WORLD_REBUILD over that, and the
     * deadline TORIRS_WORLD_JOB_DEADLINE_MS (default 600), resolved when the
     * features are bound (App_WorldRebuildModeInit).
     *
     * Blocking, the default: the packet task awaits the whole load inline and
     * the scene is rebuilt in place inside that packet -- the least memory,
     * and the one frame it costs is the gather and the build together.
     *
     * Background (TORIRS_WORLD_REBUILD=background): the gather runs on
     * background_runner while the old scene stays drawn and the packets behind
     * the REBUILD are held at the pump; the build and the tail run when it
     * lands (app_world_job.c). While a job is pending the packet pump pops
     * nothing: everything the server sent after the REBUILD addresses the
     * scene it is building. Nothing else is held -- frames, input, interfaces
     * and outgoing packets carry on against the old scene.
     * @see App_WorldJobPending.
     */
    struct WorldRebuildJob world_job;
    /**
     * TORIRS_REBUILD_TIMING: the main-thread work of every frame from the one
     * a REBUILD started in to the one it finished in, either mode -- the
     * number the background mode exists to move (App_NoteFrameTime).
     * engine/world_builder/rebuild_frame_stats.h.
     */
    struct RebuildFrameStats rebuild_frames;

    /* ---- interaction state (Part 6) ---- */
    /** "Use <item> with ..." and "Cast <spell> on ..." selections: armed by
     * a Use row or a BUTTON_TARGET spell, consumed by the next click on a
     * valid target kind. */
    struct UITargetSelect target_select;

    /** Inventory slot press/drag (reference objDrag* state machine): a left
     * press on a filled slot ARMS this — the generic node drag is suppressed
     * while armed, the armed slot renders trans-128 at the mouse delta, and
     * release routes to swap + INV_BUTTOND (real drag: past the dead zone AND
     * held past the dead time) or to the default menu row (short click).
     * CS1 release: optimistic swap + classic INV_BUTTOND. CS2 release:
     * onDragComplete + dual-endpoint IfButtonD, no local item mutation
     * (rev-230 deob). The gates themselves are ui/inv_drag.h; what stays here
     * is the tree/container reach the module deliberately does not have. */
    struct UIInvDrag inv_drag;

    /** Client scaling state and whether the lane's window mode has been
     *  restated (App_SyncNativeWindowMode). @see ui/canvas_scale.h. */
    struct UICanvasScale client_scale;

    /** Whether the on-screen keyboard should be up and whether the platform
     *  must be told: the scripts' and plugins' requests, what was last pushed,
     *  and the keyboard inset (what the layout subtracts for every row whose
     *  profile declared `safe_area=os:bottom` -- the login box, on the
     *  profiles that say so). Drained by the shell, which is the only thing
     *  here with a platform to raise one on. @see App_TakeTextInputChange and
     *  platform/text_input_gate.h. */
    struct TextInputGate text_input_gate;
};


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
/**
 * Raise the cache's NPC_ADD client trigger for a freshly synced npc.
 *
 * Called from the entity-sync path rather than from the spawn helper, because
 * the npc's `server_slot` -- the uid every context op and every scripted
 * overlay keys on -- is written after the helper returns. See
 * game/rs_client_trigger.h.
 */
void
App_ClientTriggerNpcAdd(struct App* app, int npc_pool_index);

int
App_SetCanvasSize(
    struct App* app,
    int width,
    int height);

/**
 * Set the chrome scale in quarters of a device pixel per ToriRSChrome pixel,
 * 4..12 (6 is 1.5x), fractions included.
 *
 * One call, because the things it touches must never disagree: the
 * overlay's LAYOUT (both chrome models, ToriRSChrome_SetScaleQ, which measure
 * against the quarter metrics), the scene FONTS and SKIN the renderer draws
 * those rows with (UITreeSceneBridge_SetChromeScaleQ registers the bake's
 * atlases and images at q), the tree components that name the fonts, and the
 * step the parchment tiles by. Set some and not the others and the chrome
 * lays out at one size and paints at another -- which reads as a broken font
 * rather than as a missed call.
 *
 * The caller is whoever knows the display: main.c's main_chrome_scale_q,
 * from the display's density, a pin or the editor's View row.
 * @return 1 if the scale changed.
 */
int
App_SetChromeScaleQ(struct App* app, int q);

/** App_SetChromeScaleQ(app, 4 * scale), for an integer scale 1..3. */
int
App_SetChromeScale(struct App* app, int scale);

/**
 * Text rendering, App_SetChromeScaleQ's sibling: 0 = bitmap at whole scales
 * (the baked tiers at 1x, 2x, 3x; the vector face between), 1 = smooth always
 * (the vector face at every scale). Sets both chrome models' mode
 * (ToriRSChromeBake_SetTextSmooth) and the bridge's
 * (UITreeSceneBridge_SetChromeTextSmooth), then re-resolves the overlay's
 * font ids, so the native renderer draws the atlas the mode names. Nothing
 * moves when the mode did not. @return 1 if it changed.
 */
int
App_SetChromeTextSmooth(struct App* app, int smooth);

/**
 * Choose which of the interfaces' two booleans the chrome's checkboxes wear:
 * enum ToriRSChromeCheckStyle.
 *
 * One call for the same reason SetChromeScale is one: the developer overlay
 * and the plugin window are two instances of one model, and a client whose two
 * panels disagree about what a checkbox looks like is a client with a bug
 * nobody can point at. Whatever executor the plugin window is bound to hears
 * about it through the seam (TORIRS_CHROME_CMD_CHECK_STYLE) on the next sync,
 * so a native window changes with the in-canvas one.
 *
 * @return 1 if the style changed.
 */
int
App_SetChromeCheckStyle(struct App* app, int style);

/** enum ToriRSChromeCheckStyle, as App_SetChromeCheckStyle left it. */
int
App_ChromeCheckStyle(struct App const* app);

/**
 * Give the plugin window a presentation to use.
 *
 * The shell chooses, because choosing needs the platform handle and the App is
 * deliberately platform-free -- App_Render is handed a pixel buffer rather than
 * a window for the same reason. What arrives here is a vtable, NOT a started
 * executor: it is brought up the first time the plugin window is opened, so a
 * session that never opens it never opens a second OS window either.
 *
 * Optional. Without it the plugin window uses the internal in-canvas
 * presenter, the fallback on lanes without a web executor.
 */
/**
 * Hand the App the bus it sends platform requests on. main.c owns the bus and
 * drains it every frame into PlatformWindow_* calls; the App only pushes.
 */
void
App_SetPlatformBus(struct App* app, struct ToriRS_CmdBus* bus);

void
App_SetPluginChromeExec(
    struct App* app, struct ToriRSChromeExec const* exec, int kind, int explicit_choice);

/** Device pixels per chrome pixel. */
int
App_ChromeScale(struct App const* app);

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
 * want App_CanvasFloorWidth instead — they carve from whatever window size
 * they already have, down to that floor and no further.
 */
int
App_FixedCanvasWidth(struct App const* app);

/**
 * The window size fixed mode snaps to: the fixed canvas scaled by "Interface
 * scaling" (device option 27), rounded down to a whole multiple in integer
 * stretch mode. The canvas cannot move -- App_SetCanvasSize floors it at the
 * classic frame -- so the window grows and the present stretches the same
 * pixels into it.
 */
int
App_FixedWindowWidth(struct App const* app);

int
App_FixedWindowHeight(struct App const* app);

/**
 * The narrowest canvas the frame now on screen can be laid out in: the FRAME's
 * floor (a plugin layout's when one holds the frame, the revconfig gameframe's
 * APP_CANVAS_MIN_W otherwise) PLUS the right-docked chrome strip.
 *
 * The strip is not part of any frame's area — both resizable toplevels lay
 * their own children out beside interface 728's 42-column rail, and a layout
 * plugin is handed the canvas less that same rail as its build area — so a
 * canvas of exactly the frame's floor leaves the frame 42 columns short of it,
 * which is what puts a 519-wide chatbox under a 249-wide sidebar at the
 * 765x503 preset.
 */
int
App_CanvasFloorWidth(struct App const* app);

/**
 * When window_mode is fixed, grow/shrink the canvas to App_FixedCanvasWidth so
 * the gameframe lays out at APP_CANVAS_MIN_W with the popout strip outside.
 * Returns 1 if the canvas size changed. The shell must also snap the SDL window
 * to the new size (App has no platform).
 */
int
App_SyncFixedChromeInset(struct App* app);

/**
 * The smallest game area, in drawable pixels, a resizable window should have:
 * the frame and the strip at the chosen interface scale. Returns 0 when not
 * resizable. The shell grows a window below it and makes it the window's
 * minimum, both capped at the display (App has no platform).
 *
 * Where the display cannot hold it, App_ApplyWindowLayout lowers the scale
 * rather than hand the frame a buffer smaller than itself.
 */
int
App_ResizableWindowFloor(
    struct App const* app,
    int* out_w,
    int* out_h);

/**
 * Apply a pending client-scaling change -- interface scale (27), stretch mode
 * (30), pixel limit (31) or its policy (32) -- to a resizable canvas. Returns 1
 * if the canvas changed.
 *
 * The interface scale is realised as a *smaller canvas*, not as a second
 * coordinate space: the UI tree lays out at window/scale and the present blows
 * that up to the window. 200% therefore means "half as many layout pixels
 * across, each twice the size", which makes every interface element twice as
 * big without a single widget knowing about it. A CPU renderer also rasterises
 * at that size; a GPU one rasterises at the render size ClientScale_Present
 * chose, up to the pixel limit.
 *
 * Resizable only. Fixed mode's canvas is pinned to the classic frame, so the
 * shell applies the scale to the window it snaps to instead
 * (App_FixedWindowWidth/Height) and consumes the flag there.
 */
int
App_SyncClientScale(struct App* app);

/**
 * The resizable canvas for a game area of `window_w` x `window_h` output
 * pixels under the current settings, applied through App_SetCanvasSize (and so
 * its floor). Records the window and the layout. Returns 1 if the canvas
 * changed.
 */
int
App_ApplyWindowLayout(
    struct App* app,
    int window_w,
    int window_h);

/**
 * The device's safe area changed (TORIRS_CMD_SAFE_AREA): record it for the
 * SAFEAREA_GET* opcodes and give every onResize listener the layout pass a
 * resize would, since the mobile toplevel's dodger (script 4731) and the
 * gameframe's own resize script are what read it. Returns 1 if it changed.
 */
int
App_SetSafeArea(
    struct App* app,
    struct ToriRS_CmdSafeArea const* area);

/**
 * The session this client is playing, as the next run of it would ask for it
 * back: the name it was dialled with and its resume token
 * (app_session_resume_token), which a boot passes as `--user` / `--resume` to
 * reconnect with GAMERECONNECT instead of logging in. 1 only while in the game
 * on a revision with seed reconnect; 0 otherwise (and both outputs "").
 * For a host that starts the client over in place (Android's renderer
 * restart), as a reloaded page does (src/web/torirs_host.js).
 */
int
App_SessionResume(
    struct App const* app,
    char* out_user,
    int user_size,
    char* out_token,
    int token_size);

/** The smallest canvas height the frame now on screen can be laid out in: the
 *  classic frame's, or a plugin layout's own. @see App_CanvasFloorWidth. */
int
App_CanvasFloorHeight(struct App const* app);

/** The HighDPI mode in force: the store's choice, with "automatic" resolved
 *  to the lane's own (App_SetHighDpiAuto). */
enum ClientScaleHighDpi
App_HighDpiMode(struct App const* app);

/** The display density the platform last reported, as a percent; 100 before
 *  any report. */
int
App_DisplayDensityPercent(struct App const* app);

/**
 * The shell's report of the display the window is on: drawable pixels per
 * window point, as a percent (200 on a Retina display). Re-lays out a
 * resizable canvas when it changes, which is how a window dragged between a
 * 1x and a 2x monitor keeps its HighDPI mode's size.
 */
void
App_SetDisplayDensity(
    struct App* app,
    int density_percent);

/**
 * What HighDPI "automatic" (device option 34 = 0) resolves to on this lane.
 * The shell states it at boot from the manifest's `[ui:boot] hidpi=`: a lane
 * authored at window points (hidpi=0) keeps rendering at points, every other
 * lane at device pixels. A stored choice other than automatic overrides it.
 */
void
App_SetHighDpiAuto(
    struct App* app,
    enum ClientScaleHighDpi mode);

/** The client-scaling settings the store holds now, with the output filter's
 *  "same as the interface filter" and HighDPI's "automatic" already resolved. */
void
App_ClientScaleSettings(
    struct App const* app,
    struct ClientScaleSettings* out);

/** Record what the shell presented with, for the settings page's readout. */
void
App_SetClientScalePresent(
    struct App* app,
    struct ClientScalePresent const* present);

struct UICanvasScale const*
App_ClientScale(struct App const* app);

/**
 * Restate the lane's own window mode: FIXED on a CS1 lane, whose baked frame
 * has no resizable layout, and the lane default elsewhere.
 *
 * Raised through the SAME pending-window-mode flag a clientscript's
 * setwindowmode raises, deliberately: fixed versus resizable is a statement
 * about the WINDOW, and there is already one drain in the shell that makes it.
 */
void
App_SyncNativeWindowMode(struct App* app);

/**
 * Once per frame: bind the cache gameframe's roles and publish widget changes
 * to the plugins, and restate the lane's window mode once the host has
 * started. A no-op when there is no plugin host.
 */
void
App_PluginLayoutTick(struct App* app);

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
/**
 * Drain a plugin's request to show or hide the on-screen keyboard.
 *
 * The same split as App_TakeWindowModeChange, and for the same reason: raising
 * a keyboard is a statement about the PLATFORM, the App has none to make it
 * with, and there is already one drain in the shell that turns statements like
 * this into SDL calls. @return 1 when a request was pending.
 */
int
App_TakeTextInputChange(struct App* app, int* out_on);

/**
 * The login form as a password manager needs it (platform_credential_form.h):
 * 1 and `out` filled when it changed since the last take. Polled per frame,
 * like App_TakeTextInputChange, because many paths move the form.
 */
int
App_TakeCredentialForm(
    struct App* app,
    struct PlatformCredentialForm* out);

/** The page's input for `field` (0 username, 1 password) now says `text`:
 *  typed there, or filled by the browser. Ignored off the credential form. */
void
App_CredentialInput(
    struct App* app,
    int field,
    char const* text);

/** The page's input for `field` took the focus: the caret follows it. */
void
App_CredentialFocus(
    struct App* app,
    int field);

/** The page's form was submitted (Enter in either input): the Login button. */
void
App_CredentialSubmit(struct App* app);

/**
 * A url a clientscript asked the client to open (openurl, 3113), copied into
 * `out` and cleared. The same split as App_TakeTextInputChange: opening a
 * browser is the platform's to do. @return 1 when a url was pending.
 */
int
App_TakeOpenUrl(struct App* app, char* out, int cap);

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

/**
 * Does a side-app answer every action the launch configuration names?
 *
 * Prints `bootmanifest: [action:<source>] has unknown target '<name>'` for
 * each declared action no side-app registered (and a line for a binding no
 * declaration names), and answers 0; the host then refuses to boot, as the
 * manifest load refused it before the names moved to the side-apps. After
 * App_Init, which registers them.
 */
int
App_ToolsBindingsResolved(struct App const* app);

/**
 * Make the one world view App_Init starts with (WorldViews_Init), its menu
 * pointer at the menu `interact` keeps. Touches nothing else, so App_Init
 * calls it right after the memset and a test that builds a struct App by hand
 * calls it the same way before reaching anything that reads a view.
 */
static inline void
App_WorldViewsInit(struct App* app)
{
    assert(app);
    WorldViews_Init(&app->world_views, &app->interact.minimenu);
}

/** The view a script's verbs act through (views[0], always). */
static inline struct App_WorldView*
App_RunnerView(struct App* app)
{
    assert(app);
    return WorldViews_Runner(&app->world_views);
}

/** The view the presented frame draws: the PlayerClient view while a script
 *  is attached, otherwise the only one. */
static inline struct App_WorldView*
App_PresentedView(struct App* app)
{
    assert(app);
    return WorldViews_Presented(&app->world_views);
}

/** The runner camera split (struct App_ViewSplit), app_render.c.
 *
 *  Attach makes views[1] a copy of views[0] (no jump), gives it its own menu,
 *  and hands the driver its own input bus. It answers 1 when two views are
 *  live afterwards, 0 when the client presents nothing or its lane cannot
 *  carry the second view; `out_reason` (when non-NULL) then says which. A
 *  second attach is a no-op answering 1. */
int
App_ViewAttachPlayerClient(
    struct App* app,
    char const** out_reason);

/** Detach: views[0] takes the watcher's camera, pointer and menu (what is on
 *  screen stays on screen; nothing the runner did is copied), views[1] and the
 *  runner's buffers are freed. A detach with nothing attached is a no-op. */
void
App_ViewDetachPlayerClient(struct App* app);

/** FRAME LOCK: every frame pays exactly k logic cycles
 *  (TORIRS_LOGIC_CYCLES_PER_FRAME, default 1) instead of what the wall clock
 *  owes, and a watched client's world clock steps 20 ms a frame (main.c
 *  on_demand_world_clock). A bounded run (TORIRS_MAX_FRAMES) is locked from
 *  boot; a client that hosts a party (api.drive.party_host) engages it there,
 *  because a party plays in lock step; an unlocked client (App_SpeedUnlock)
 *  is locked from boot. At the 50 fps pace that is real speed.
 *  Never released, not even when the party is: the world clock has stepped
 *  20 ms a frame since, and the wall clock may be behind it. */
int
App_FrameLocked(void);

void
App_FrameLockEngage(void);

/** UNLOCKED CLOCK (`--unlocked`, TORIRS_UNLOCKED=1, `[net:boot]
 *  clock=unlocked`): the client runs as fast as the machine allows, and so
 *  does the world it plays in. Implies the FRAME LOCK above -- every frame
 *  pays exactly k logic cycles, whatever the wall clock says -- and main.c
 *  drops the frame-cap wait. The one thing that does not move is the ratio:
 *  the embedded server's 600 ms tick is clocked off App_LogicCyclesRun
 *  (NetTransport_CycleClock), so a server tick is always 30 client cycles
 *  however fast either one runs. Only the in-process server's clock and an
 *  RSProx replay's are this client's to drive, so main.c refuses it on any
 *  other transport. Set at boot; only a replay releases it (App_SpeedRelock),
 *  and may set it again later. */
void
App_SpeedUnlock(void);

/** Release the unlocked clock: back to the wall clock and the frame cap. For
 *  an RSProx replay only (main.c main_rsprox_replay_frame), whose transport is
 *  the one clock that shares the client's: the embedded server's tick, a
 *  party's lock step and a bounded run's frame clock stay locked once locked. */
void
App_SpeedRelock(void);

int
App_SpeedUnlocked(void);

/** Every logic cycle the frame loop actually ran, counted after the settle
 *  fence (a cycle charged and owed to the next frame is counted when it
 *  runs). The unlocked clock's world time is this x 20 ms. */
void
App_LogicCyclesNote(int cycles);

uint64_t
App_LogicCyclesRun(void);


/** Run `cycles` logic cycles a frame while the frame lock is on, in place of
 *  TORIRS_LOGIC_CYCLES_PER_FRAME (0 puts the env's value back). For a replay
 *  seek (main.c main_rsprox_replay_frame), which runs the client as fast as
 *  it goes: the replay's clock is the client's cycles, so any k keeps a tick
 *  at 30 cycles. The frame clock a frame hands its timers keeps the env's
 *  stretch, and nothing in an embedded-server build calls this. */
void
App_SetLogicCyclesPerFrame(int cycles);

int
App_LogicCyclesPerFrameSet(void);

/** Swap which view the input stage acts through: 1 = the PlayerClient view
 *  (its menu is moved into interact.minimenu, the storage the UI step
 *  drives), 0 = back to the runner's. Only while attached. */
void
App_ViewSplitBindPlayerClient(
    struct App* app,
    int bind);

/** The Interact switch. */
void
App_ViewSetInteract(
    struct App* app,
    int interact);

/** Read the watcher actions after `after_serial` into `out` (oldest first);
 *  answers how many were written. */
int
App_ViewWatcherActions(
    struct App const* app,
    uint32_t after_serial,
    struct App_ViewSplitWatcherAction* out,
    int capacity);

/** The host: route one physical input command while attached (main.c's drain
 *  hands every input command of its bus here when the split is attached).
 *  Answers 1 when the command was an input command (consumed or forwarded),
 *  0 for any other command (the caller dispatches it). */
int
App_ViewSplitPhysicalCommand(
    struct App* app,
    struct ToriRS_CmdHeader const* header,
    uint8_t const* payload,
    struct LibToriRS_Input* game_input);

/** The host: drain the runner's bus into the game input (after the physical
 *  bus), noting the runner's pointer and gesture. */
void
App_ViewSplitDrainRunner(
    struct App* app,
    struct LibToriRS_Input* game_input);

/** The host, once per loop iteration while attached, before the presented
 *  draw: `committed` is App_RunOnce's answer. Draws the owed offscreen
 *  runner frame (BEFORE the presented one) and ages the runner's pickset. */
void
App_ViewSplitBeforePresent(
    struct App* app,
    int committed);

/** Draw the offscreen runner frame now into the runner's buffer (or
 *  `pixels` when non-NULL), pick armed at the runner's pointer unless
 *  `pick` is 0. The CONTRACT: no frame end, no load-event drain, no
 *  animation accumulation beyond the pose cache, its own painter buffer, no
 *  rand(), not counted as a drawn frame. */
void
App_ViewSplitDrawRunner(
    struct App* app,
    int* pixels,
    int pick);

/** Tear down in strict reverse of App_Init. */
void
App_Shutdown(struct App* app);

/** Whether this run talks to a server at all: a network handle exists and was
 *  enabled by App_Init (ToriRS_Network_Enabled). */
int
App_NetEnabled(struct App const* app);

/** Forget everything that belonged to the session that just ended: tracked
 * entities and the per-account UI state the reference resets alongside them.
 * Shared by the server's LOGOUT and by the connection-lost path, which differ
 * only in what happens next. */
void
App_NetSessionReset(struct App* app);

/** SET/RESET_INTERACTION_MODE state back to the reference's boot value (1/1,
 *  no view set): App_Init and every session end. */
void
App_InteractionModesReset(struct App* app);

/** SEND_PING_REPLY with the ping's two values and this client's frame rate. */
void
App_SendPingReply(
    struct App* app,
    int value1,
    int value2);

/**
 * End the session on purpose and go back to the login screen.
 *
 * The reference's `logout` (Client-TS Client.ts:2699): close the socket, forget
 * the world, and put the title screen back up with the credential fields
 * cleared. Distinct from the connection-lost path in every way that matters --
 * nothing is being re-established, so the reconnect watch is disarmed rather
 * than armed.
 *
 * This is the ENDING, not the request. The logout button does not reach it:
 * the button sends its IF_BUTTON and arms the wait in App::session_end, and what
 * calls this is the server's answer -- the LOGOUT packet, or the socket it
 * closes instead. The two ways in are that answer and a click with no session
 * to answer it (an offline profile, or a connection already gone). A wait
 * with no answer is not one: it lasts until the server ends the session.
 *
 * A profile that declares no [layout:title] has no login screen to return to;
 * the session still ends, and the client stays on the gameframe it booted into.
 */
void
App_Logout(struct App* app);

/** Select world submission after renderer initialization. A software fallback
 * must always restore TORIRS_WORLD_PAINTER. */
void
App_SetWorldRenderMode(
    struct App* app,
    enum ToriRS_WorldRenderMode mode);

/**
 * Tell the app the renderer animates water/lava textures in its own shader
 * (GLES2, GL3, D3D9), so ToriDraw_TextureMapAnimate need not scroll the
 * texels on the CPU each cycle. Default false: the software lane needs it.
 */
void
App_SetRendererAnimatesTextures(
    struct App* app,
    bool animates);

/**
 * The "Software (OSRS z-buffer)" renderer: Soft3D draws the world through the
 * rev-241 client's own depth buffer (ToriPlatform_Renderer_Soft3D_SetRsZBuffer).
 * The world stays TORIRS_WORLD_PAINTER. Default false; every renderer start
 * sets it.
 */
void
App_SetSoftwareRsZBuffer(
    struct App* app,
    bool rszbuffer);

/** Resolved interface-logic VM (enum AppUiLogic, never DEFAULT): the manifest's
 * explicit choice, or derived from cache_kind (dat1 -> CS1, dat2 -> CS2). The
 * old-gen-only ClientCode pass and the CS2 host wiring key off this. */
int
App_UiLogic(struct App const* app);

/** Begin opening an interface as the tree root (TS WidgetManager
 * .setRootInterface). Fully async: enqueues the boot task and returns —
 * App_RunOnce pumps it and flips the phase to APP_STATE_READY when the tree
 * (and initial assets) are built. App_Render draws a loading bar meanwhile. */
void
App_OpenRootInterface(
    struct App* app,
    int interface_id);

/**
 * Does this profile declare a title screen?
 *
 * Undeclared means absent, the standing revconfig contract: a profile with no
 * [layout:title] boots straight into the game, which is what every offline,
 * bench and map-editor manifest here wants.
 */
int
App_HasTitleScreen(struct App const* app);

/**
 * Bake the title screen as the tree root and put the session on APP_SCREEN_TITLE.
 *
 * Same machinery as App_OpenRootInterface -- clear, rebuild, re-stamp roles --
 * with the title [layout:] group selected and no root interface to mount,
 * because no revision ships its login screen as interface data.
 */
void
App_OpenTitleScreen(struct App* app);

/**
 * The networked boot with a title screen: LOADING FIRST, login after.
 *
 * Bakes the gameframe first thing, under the startup loading bar -- that bake
 * is where the interface packs, media and fonts are actually fetched and
 * decoded, so it IS the loading the bar narrates -- and swaps to the title
 * screen the moment it settles (App_RunOnce / App_BootWait), before a single
 * gameframe frame can render. The post-login rebake then finds every cache
 * this bake warmed and crosses in a moment, showing only "entering world"
 * instead of replaying the loading captions after the login screen.
 */
void
App_BootGameframeThenTitle(struct App* app);

/**
 * Is the current loading phase the post-login kind -- black screen, the
 * sentence "Loading - please wait.", and NO bar? True during any quiet bake
 * (post-login rebake, in-game remount) while it is still baking. The boot's
 * own loading screen, which ran before the title, keeps the bar.
 */
int
App_BootTextOnly(struct App const* app);

/** IF_OPENSUB (rev-230 openSubInterface): mount a cache interface group under a
 *  component slot of the open root. target_uid = packed (parent<<16|child) of
 *  the mount slot; type 0=modal, 1=overlay, 3=tab/sidemodal. Async — enqueued on
 *  the serial packet queue so it settles before the next packet is applied. */
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

/** The two halves of a re-root's mount handling, apart from the tree rebuild
 *  between them (App_OpenRootInterface runs both). Detach: the tree's mounts
 *  join the kept records, and a kept record the last re-root did not move
 *  stays. Return: what is kept in `interface_id`'s own slots is reopened, with
 *  whatever was open inside it. */
void
App_RootMountsDetach(struct App* app);

void
App_RootMountsReturn(
    struct App* app,
    int interface_id);

/** The mounts kept for a top that is not the live root (see
 *  App.kept_mounts). IF_RESYNC states the whole table, so its handler
 *  clears these and adds back the mounts whose slot the live top cannot reach. */
void
App_KeptMountsClear(struct App* app);

void
App_KeptMountAdd(
    struct App* app,
    int target_uid,
    int interface_id,
    int type);

/** RUNCLIENTSCRIPT: run a server-named clientscript with its arguments. Held
 *  until the tick fence (`pending_clientscripts`), but never past an interface
 *  open or close behind it: the packet pump releases the held scripts onto the
 *  serial packet pipeline ahead of any packet that mounts or unmounts an
 *  interface, so a script that seeds state for an interface (the world map's
 *  `worldmap_transmitdata`) settles before the IF_OPENSUB behind it, and one
 *  sent before an IF_CLOSESUB runs while its interface is still mounted. */
void
App_RunClientScript(
    struct App* app,
    struct PktRunClientScript const* request);

/** Dispatch every RUNCLIENTSCRIPT held since the last fence, in arrival order,
 *  as tasks on `runner`. Called at SERVER_TICK_END and whenever the packet
 *  pipeline runs dry with the CS2 runner (`app->frame_runner`), and by the packet
 *  pump with the packet pipeline (`app->packet_runner`) ahead of an interface
 *  mount change -- see `pending_clientscripts` for why they are held at all. */
void
App_FlushPendingClientScripts(
    struct App* app,
    struct TaskRunner* runner);

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

/** The presentation halves of IF_SETTEXT / IF_SETCOLOUR / IF_SETHIDE /
 *  IF_SETEVENTS, whose store write the packet model has already made
 *  (game/rs_server_state.c): put the value on the node if it is mounted, log
 *  it under TORIRS_NET_DEBUG, and redraw. `colour` is RGB888. */
void
App_IfTextShow(
    struct App* app,
    int com_id,
    char const* text);
void
App_IfColourShow(
    struct App* app,
    int com_id,
    int colour);
void
App_IfHideShow(
    struct App* app,
    int com_id,
    int hide);
void
App_IfEventsShow(
    struct App* app,
    int com_id,
    int from,
    int to,
    int events);

/** Bind the packet model's records (game/rs_server_state.h) to this App's
 *  modules, for one packet: RS_GameProtoCtx.state. */
struct RS_ServerState;
void
App_ServerStateBind(
    struct App* app,
    struct RS_ServerState* out);

/** IF_SETTEXT: persist (reference IfType.list semantics) + apply if mounted. */
void
App_IfTextSet(
    struct App* app,
    int com_id,
    char const* text);

/** IF_SETCOLOUR: persist (same reasoning as text/hide) + apply if mounted. */
void
App_IfColourSet(
    struct App* app,
    int com_id,
    int colour);

/** IF_SETHIDE: persist (reference IfType.list semantics) + apply if mounted. */
void
App_IfHideSet(
    struct App* app,
    int com_id,
    int hide);

/** Forget every stored IF_SETTEXT, IF_SETHIDE and IF_SETCOLOUR on interface
 *  `group_id`'s components. Called when a close UNLOADS that group (the
 *  reference's IfType.list goes with it); a same-group remount keeps them. */
void
App_IfStoresDropGroup(
    struct App* app,
    int group_id);

/** Drop every IF_SETEVENTS arming. Called on IF_RESYNC, which replaces the
 *  whole table; IF_OPENTOP keeps it (see that case in rs_gameproto_exec.c). */
void
App_IfEventsClear(struct App* app);

/** Drop every IF_SETEVENTS arming on interface `group_id`'s components.
 *  Called when that group is closed out of a slot -- the real client drops
 *  them on every close, a same-group remount included (UIIfEventTable_DropGroup). */
void
App_IfEventsDropGroup(
    struct App* app,
    int group_id);

/**
 * IF_SETEVENTS: mark slots `from`..`to` of a component as accepting input.
 *
 * Persisting across mounts, like App_IfHideSet, because the server enables
 * events before the interface holding the component has mounted, and across
 * an IF_OPENTOP, which the reference never clears them on. Not across an
 * IF_RESYNC (`App_IfEventsClear`), nor a close of the component's own group
 * (`App_IfEventsDropGroup`).
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

/** Spawn a synced npc. `npc_id` is the type whose MODEL is drawn -- already
 * resolved through the multiNpc table -- and `base_npc_id` is the id the wire
 * sent. They differ only for a multinpc, and the split matters: the reference
 * builds the entity's size, turn speed, ready animation and walk animations
 * out of the WIRE's type and transforms only for the body (Client.ts NPC add).
 * Pass -1 for `base_npc_id` when there is no separate shell. */
int
App_WorldSpawnSyncedNpc(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int scene_x,
    int scene_z,
    int level);

/** Follow NpcType.multiNpc (opcode 106) to the variant that's actually live
 * under the local player's current varp/varbit state, same rule as a loc
 * transform table (VarPManager_ResolveTransform). A `multinpc` shell record
 * carries no model of its own. Returns -1 when the selected entry hides the
 * NPC, `npc_id` unchanged when it does not name a shell or on a lookup miss.
 * This synchronous helper only follows already-resident configs; packet and
 * live-remorph paths use CreateTask_NpcMultiLoad so a cold cache cannot strand
 * the entity on its wrapper. Interface chatheads retain the shell id but use
 * this same per-client resolution before binding the selected child's head.
 * Depth-capped at TORIRS_NPC_MULTI_MAX_DEPTH, matching the catalog walk. */
int
App_NpctypeResolveMultiId(
    struct App* app,
    int npc_id);

/** Load a multiNpc wrapper and the locally selected child chain, then all body
 * models and movement sequences needed to spawn/retype it. `*out_npc_id` is
 * the selected leaf or -1 for a hidden entry. The caller must keep the output
 * storage alive until the returned task completes. */
/**
 * The config half of CreateTask_NpcMultiLoad alone: load the wrapper's config
 * and walk its transform rungs to the child this client's varps select,
 * without fetching that child's body. What the packet handlers await now
 * that the body lands on its own (game/task_entity_assets.h).
 */
struct ToriRS_Task*
CreateTask_NpcMultiResolve(
    struct App* app,
    int base_npc_id,
    int* out_npc_id);

struct ToriRS_Task*
CreateTask_NpcMultiLoad(
    struct App* app,
    int base_npc_id,
    int* out_npc_id);

struct UITreeMinimapDot;

/** Build this frame's minimap dot overlay (the UITREE_HOST_GET_MINIMAP_DOTS
 * body). Returns the dot count and points `*out_dots` at the app-owned array,
 * valid until the next call. Exposed for the dot-gating test. */
int
App_MinimapBuildDots(
    struct App* app,
    struct UITreeMinimapDot const** out_dots);

struct PktPlayerAppearance;

/** Apply the decoded appearance (slots, colours, idle anims, name, combat
 * level, headicon) to the World entity. Data only: the body is derived from
 * it per frame once every part is resident (app_world_reconcile_player_body). */
void
App_WorldApplyPlayerAppearance(
    struct App* app,
    int world_idx,
    struct PktPlayerAppearance const* appearance);

/** NPC transmog (CHANGE_TYPE): rebuild the model + apply the new config's
 * size/anims/menu data (assets already cached). `npc_type` is the resolved
 * drawn type and `base_npc_type` the wire's id, split for the reason
 * App_WorldSpawnSyncedNpc gives; -1 when there is no separate shell. */
void
App_WorldApplyNpcType(
    struct App* app,
    int world_idx,
    int element_id,
    int npc_type,
    int base_npc_type);

struct WorldEntityFacet_IdleAnimations;

/** The stance set `npc_type` (drawn under the wire's `base_npc_type`) gives an
 * npc on a spawn or a retype. 0 when the type's config is not resident. */
int
App_WorldNpcTypeIdle(
    struct App* app,
    int npc_type,
    int base_npc_type,
    struct WorldEntityFacet_IdleAnimations* out);

/* The body NpcBodyLand waited for is resident: mount it and the type's ready
 * pose, and nothing else (the type's fields were applied with the op). */
void
App_WorldNpcBodyLanded(
    struct App* app,
    int world_idx,
    int element_id,
    int npc_type,
    int base_npc_type);

/* Ground item stacks (zone OBJ_* packets; objtype/model already cached). */
int
App_WorldObjStackAdd(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count);

/**
 * OBJ_ADD's ownership half, applied to the stack the add just returned.
 *
 * `public_ticks` and `despawn_ticks` are the packet's own remaining-tick
 * counts; this converts them to the client-clock deadlines the world stores,
 * so the caller never has to know how the two clocks relate. Zero or negative
 * means "no such deadline", which is what a revision without the fields sends.
 *
 * Separate from the add for the same reason World_ObjStackSetOwnership is:
 * only the zone packet has these, and a hotkey spawn must not have to invent
 * them.
 */
void
App_WorldObjStackSetOwnership(
    struct App* app,
    int idx,
    int public_ticks,
    int despawn_ticks,
    int owner,
    int never_becomes_public);

void
App_WorldObjStackDel(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id);

/** OBJ_COUNT: retarget a stack's count in place. Goes through App rather than
 *  straight to World so the plugins hear about it -- every edge of a ground
 *  item's life is announced, and one that slipped past would leave a plugin
 *  drawing against a count that no longer exists. */
void
App_WorldObjStackSetCount(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count);

/** Zone clear: remove every stack on one tile, announcing each. */
void
App_WorldObjStackClearTile(
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

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
 * The view the active-world packet cursor addresses (worldview.h). This is the
 * one seam between the packet-apply layer and the multi-world registry: zone
 * applicators and the rebuild path take their (world, builder) from here, so a
 * SET_ACTIVE_WORLD naming a boat view routes them without touching the call
 * sites again. Asserts the cursor names a live view — id 0 always is once the
 * root registers at world creation.
 */
struct Worldview*
App_ActiveWorldview(struct App* app);

/**
 * Spawn one world entity from the WORLDENTITY_INFO new-entity trailer
 * (SAILING_PLAN C1): builds the entity's own (World, WorldBuilder) pair over
 * the App's shared scene, registers it in a free view slot under the server's
 * id `wire_id` (WorldviewRegistry_SlotForWire finds it again) beneath the
 * active-world cursor's view, and creates the Wev at the trailer's absolute
 * transform (fine units / 0..2047). `config_id` must name a loaded WevConfig — a spawn
 * for a config the cache does not carry is a protocol violation and asserts.
 * Requires a fully-booted App (scene + provider live); a harness App without
 * them asserts rather than half-spawning.
 */
struct Wev*
App_WevSpawn(
    struct App* app,
    int wire_id,
    int config_id,
    int size_x_tiles,
    int size_z_tiles,
    int priority_group,
    int x,
    int z,
    int angle,
    unsigned op_mask);

/**
 * Despawn world entity `id`: removes the Wev from its parent's list and
 * releases its Worldview (freeing the owned world/builder pair). Any nested
 * entities must have been despawned first — the server sends leaves first.
 */
void
App_WevDespawn(
    struct App* app,
    int id);

/** The plane a world entity's deck is authored at inside its own world
 *  (WevConfig.plane): where a deck's npcs and riders stand. */
int
App_WevDeckLevel(
    struct App* app,
    int view_id);

/**
 * The live view whose STAGING rectangle contains the absolute tile, or 0.
 *
 * This is the deob's geometric membership rule from the wire's side
 * (docs/SAILING.md §5.1): a rider aboard a hull carries deck-instance
 * coordinates — tiles inside the view's base rectangle, hundreds of squares
 * off the real map — and the entity-info executor calls this to rebase them
 * view-locally instead of producing a root-scene-local tile no uint8_t route
 * queue can carry. On a match, `out_local_x`/`out_local_z` are the tile
 * relative to the view's base. Views whose base is still unknown (no
 * REBUILD_WORLDENTITY yet) never match; the per-tick routing pass re-tests, so
 * an absolute op that raced the deck rebuild heals a tick later.
 */
int
App_WevHomeViewForAbsTile(
    struct App* app,
    int abs_tile_x,
    int abs_tile_z,
    int* out_local_x,
    int* out_local_z);

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
 *
 * `background`: the rebuild will be a world job (App_WorldJobStart), which
 * keeps the old scene on screen while it gathers -- so the viewport is not
 * blanked (WorldLoadState_Inflight stays clear).
 */
int
App_WorldRebuildBegin(
    struct App* app,
    int zone_x,
    int zone_z,
    int force,
    int background);

/** Resolve the rebuild mode and the job deadline (App::world_job), once,
 *  at init. */
void
App_WorldRebuildModeInit(struct App* app);

/**
 * Should the REBUILD about to be applied be a background world job?
 *
 * Only in background mode, only for a ROOT rebuild onto a scene that is
 * already loaded (a first load has nothing to keep on screen), and only when
 * the frame latch can be suspended for the hold without publishing a
 * half-applied UI transaction: the REBUILD's server tick may be open on
 * plain varps (RS_ServerTick_OpenNonVar false) and no clientscript may be
 * held. The reasoning and the measurement are at the definition.
 */
int
App_WorldJobWanted(struct App const* app);

/**
 * Start the background rebuild of the root scene: queue its gather on
 * background_runner and hold the packet pump until it has been committed. Called by
 * the REBUILD packet task in place of awaiting the load, after the same
 * bookkeeping (App_WorldRebuildBegin, the square list, the xtea keys).
 * `zones` is the instance grid or NULL; both arrays are copied.
 */
void
App_WorldJobStart(
    struct App* app,
    int const* chunks_xz,
    int chunk_count,
    int zone_x,
    int zone_z,
    int32_t const* zones);

/** Is a background rebuild in progress (packets held, scene not yet built)? */
int
App_WorldJobPending(struct App const* app);

/**
 * The packet pump's question, asked before every pop while a job is pending:
 * commit it if its gather has landed (or finish it now, if its deadline has
 * passed). 1: the job is committed and popping may go on; 0: still pending,
 * hold.
 */
int
App_WorldJobPoll(struct App* app);

/**
 * Start the paced catch-up: called by the commit, on the frame it ran. No-op
 * when pacing is off (a budget of 0).
 */
void
App_PaceArm(struct App* app);

/**
 * The time this frame's paced packet and script work must stop at, or 0 when
 * nothing is paced (never armed, or the window after the commit is over).
 * The first ask of a frame fixes that frame's deadline one budget ahead, so a
 * frame always gets its budget however late in it the work starts.
 */
uint64_t
App_PacedDeadline(struct App* app);

/** Is the pump barred from popping another packet this frame? */
int
App_PaceHoldsPop(struct App* app);

/**
 * Paced work was stopped for this frame's deadline. Pacing then lasts a
 * little past the stop even if its window is over, so what the window left
 * queued does not all run on the frame the window ends (a new scene's music
 * samples were still loading 700 ms after its commit), up to a hard limit.
 */
void
App_PaceNoteStop(struct App* app);

/**
 * End a pending job without building anything: session teardown, logout,
 * shutdown, or an offline/editor load taking the scene over. The hold lifts,
 * nothing is acknowledged, and the loads already queued run out harmlessly.
 * No-op when nothing is pending.
 */
void
App_WorldJobCancel(
    struct App* app,
    char const* why);

/**
 * "The world is settled": no load in flight in either mode. What a test or a
 * driver waiting for the scene must ask -- WorldLoadState_Inflight alone only
 * says whether the viewport is blanked, and stays clear through a background
 * rebuild.
 */
int
App_WorldSettled(struct App const* app);

/**
 * Give the background queue time until `deadline_us` (PlatformWindow_TicksUs
 * clock). Called by App_RunOnce with a floor slice, and by a platform loop
 * with the slack before its frame deadline instead of sleeping it. Costs
 * nothing when the queue is empty.
 */
void
App_RunBackground(
    struct App* app,
    uint64_t deadline_us);

/** Drain WorldEventKind_EntityRemoved into ToriDraw_SceneElementRemove.
 *  Required before a scene rebuild begins (ResetSceneAlloc asserts the queue
 *  is empty) and after bulk despawns. The root wrapper no-ops before the
 *  first world exists; the For variant takes any view's World — a boat view
 *  drains its own queue through it before its deck rebuild (C2 drain rule). */
void
App_WorldDrainEntityRemoved(struct App* app);

void
App_WorldDrainEntityRemovedFor(
    struct App* app,
    struct World* world);

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

/** LOC_ANIM: attach a sequence to the loc standing on a tile's layer now, and
 * hand it to a change still pending there when the rev-239 client would (deob
 * Statics.method1508 / class69.field666). `loc_shape` (zone packet info >> 2)
 * selects the layer so a door animates the wall, not a centrepiece/floor-decor
 * sharing the tile. */
void
App_WorldSceneryAnim(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id);

/** SPOTANIM mask: start loading an entity-attached graphic the moment the
 * packet names it, rather than when its start delay has run out. A delayed
 * impact (a barrage's ice block, a bolt's splash) is timed to land with its
 * projectile; loading it only then meant a cold graphic drew late or, once the
 * world had played its frames out, not at all. No-op when already resident. */
void
App_EntitySpotanimPrefetch(
    struct App* app,
    int spotanim_id);

/* Spawn a free-standing spotanim (graphical effect) at a tile of the active
 * view — reference MapSpotAnim, driven by the MAP_ANIM zone packet. The world
 * entity exists at once (its delay and lifetime count from now); its body
 * (config, model, seq) lands later through a parallel task
 * (app/app_tasks_effects.c). Returns its pool index. */
int
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

/** CAM_MOVETO_CYCLES / CAM_ROTATETO_COORDINATE, at arrival (rs_gameproto_exec.c).
 *  `local_x`/`local_z` are scene-local tiles. */
void
App_CinemaCameraMoveCycles(
    struct App* app,
    int local_x,
    int local_z,
    int height,
    int cycles,
    int ignore_terrain,
    int easing,
    int height_relative);

/** CAM_MOVETO_ARC, at arrival: CAM_MOVETO_CYCLES along the arc through the
 *  scene-local `via` tile. */
void
App_CinemaCameraMoveArc(
    struct App* app,
    int local_x,
    int local_z,
    int via_local_x,
    int via_local_z,
    int height,
    int cycles,
    int ignore_terrain,
    int easing,
    int height_relative);

void
App_CinemaCameraRotateCycles(
    struct App* app,
    int local_x,
    int local_z,
    int height,
    int cycles,
    int easing,
    int height_relative);

/** Tell the server the free camera was left. */
void
App_SendExitFreecam(struct App* app);

/** CAM_SKYBOX: -1 clears. */
void
App_SetSkybox(
    struct App* app,
    int model_id);

void
App_CinemaCameraRotateBy(
    struct App* app,
    int yaw_delta,
    int pitch_delta,
    int cycles,
    int easing);

void
App_CinemaCameraRotateTo(
    struct App* app,
    int yaw,
    int pitch,
    int cycles,
    int easing);

/* Spawn a projectile (reference ClientProj) from a spotanim config, driven by
 * the MAP_PROJANIM zone packet, in the active view's world. The projectile
 * exists and flies at once; its body (config, model, seq) lands later through
 * a parallel task (app/app_tasks_effects.c). Returns its pool index.
 * Coordinates are scene tiles;
 * `src_height`/`dst_height` are the raw wire bytes (×4 applied internally);
 * `start_delay`/`end_delay`/`peak`/`arc` are the wire trajectory params
 * (Client-TS t1/t2/angle/startpos). `target` is the wire target-entity id
 * (>0 npc, <0 player) the arc homes on; `source` the actor it launches from,
 * same encoding, 0 = none (only rev-239's MapProjAnimV2 names one). The two
 * levels are the source's (painted on) and the destination's (landed on). */
int
App_WorldProjectileSpawn(
    struct App* app,
    int src_x,
    int src_z,
    int src_level,
    int dst_x,
    int dst_z,
    int dst_level,
    int spotanim_id,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target,
    int source);

/** OBJ_CUSTOMISE: the first stack on the tile with this id and count wears
 *  it; its model is rebuilt. */
void
App_WorldObjStackCustomise(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count,
    struct WorldEntity_ObjCustomise const* custom);

void
App_WorldObjStackUncustomise(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count);

/** OBJ_ENABLED_OPS: the five ground ops of the first stack of this id. */
void
App_WorldObjStackSetEnabledOps(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int enabled_ops);

/* Spawn a scripted projectile (deob class56). Like App_WorldProjectileSpawn it
 * exists at once and its body lands later; the body also loads the three
 * curve clientscripts and every script they call, because the world runs them
 * synchronously each cycle (it flies once they are here). The slot must be
 * free (World_ProjectileFindScriptedSlot): a second ADD for a slot in use is
 * the caller's to drop. Returns its pool index. */
int
App_WorldScriptedProjSpawn(
    struct App* app,
    struct WorldEntity_ScriptedProjSpec const* sp);

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
 * `App_WorldLocChange` carrying the PLACEMENT's own right-click menu — the
 * op mask and replacement labels LOC_ADD_CHANGE_V2 added at rev 228.
 *
 * `op_flags` is a 5-bit shown mask (bit 0 = op1); `ops[i]`, when non-empty,
 * replaces the loctype's label for slot i — including on a slot the loctype
 * leaves empty, which is what lets one loc record offer "Open" where the map
 * placed it and "Close" where a door script swung it to.
 *
 * A separate entry point rather than two more parameters on the one above,
 * because every other caller — a varp remorphing a multiloc, the loc editor —
 * is placing a loc that means what its type means. `App_WorldLocChange` is
 * that case spelled once, and it passes an all-shown mask and no labels.
 */
void
App_WorldLocChangeOps(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32]);

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

/**
 * Tell the game whether the host opened an output device at all.
 *
 * Separate from the per-frame feedback above, which reports on a device that
 * exists. A host that never calls this is assumed to have one, so the silent
 * path is opt-in and nothing changes for a host that does not know about it.
 */
void
App_SetAudioDevicePresent(struct App* app, bool present);

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
/** Whether the last App_RunOnce left async work queued. */
int
App_AsyncPending(const struct App* app);

/**
 * Step the asset runner until nothing more can happen without the host's
 * turn. The frame's own async stage, and also what the browser platform runs
 * between frames when reads land (see App_PumpAsync in app_frame.c).
 * `from_frame` is 1 from App_RunOnce and 0 from a platform pump.
 */
enum TaskRunnerStat
App_PumpAsync(
    struct App* app,
    int from_frame);

void
App_NoteFrameTime(
    struct App* app,
    uint64_t frame_us);

/**
 * Count one frame drawn -- what plugins read as `drawn_frames`, and what a
 * frame-rate readout differences. App_Render counts its own; a GPU lane, which
 * draws through App_BuildFrame and its own renderer instead, calls this once
 * per frame it presents, boot bar included, so the count means the same thing
 * whichever renderer is drawing.
 */
void
App_NoteFrameDrawn(struct App* app);

/**
 * Render skip (App::render_skip): switch it on or off. Turning it on starts
 * from a fresh pickset: the frame before was drawn.
 */
void
App_RenderSkipSet(
    struct App* app,
    int enabled);

int
App_RenderSkipEnabled(struct App const* app);

/**
 * Hold the screen (1) or release it (0): while held, App_RenderSkipFrame draws
 * only a frame that is forced (a retained GPU renderer's, which learns the
 * scene from the draw) or a capture, whatever `enabled` says. A replay seek
 * holds it so the frames it fast-forwards through are never shown; the
 * release owes one drawn frame.
 */
void
App_RenderSkipHold(
    struct App* app,
    int hold);

/**
 * Owe `frames` drawn frames, starting with the current one -- asked by input
 * that the NEXT frame resolves against this frame's pickset (a pushed click or
 * move). Counted in frames DRAWN: a frame the host does not commit
 * (App_RenderSkipFrame) leaves the debt in place. A no-op in effect while
 * skip is off: every committed frame draws then.
 */
void
App_RenderSkipRequestDraw(
    struct App* app,
    int frames);

/**
 * Make the render-time state (app->world_views.frame->world_pickset and the hover tile, the
 * posed model bounds) what it would be with skip off, before something reads
 * it: when the most recent frame was committed but skipped, draw it NOW, late,
 * into a scratch buffer -- same emit list, scene, camera and pointer, so the
 * same pick and the same poses. Call it before a read, from the plugin pump
 * (the top of App_RunOnce, where the app still holds that frame's state).
 * Returns 1 when the state is fresh (skip off, the frame was drawn, or it was
 * just caught up), 0 when it cannot be: an uncommitted frame came after the
 * skipped one and its emit list may point into freed components -- the reader
 * then treats the state as absent.
 */
int
App_RenderSkipCatchUp(struct App* app);

/**
 * The host's one call per loop iteration, AFTER every other say on whether
 * this frame draws (`redraw`: App_RunOnce's answer and anything that forced
 * it). Returns the final answer. With skip off it is `redraw`, unchanged.
 * With skip on it is `redraw` AND one of the reasons below -- skip takes
 * draws away and never adds one (an uncommitted frame's emit list may point
 * into freed tree components). The reasons:
 *   - `forced`: the host's own must-draw (content_test.c's mailbox requests,
 *     a renderer that cannot skip),
 *   - a screenshot is waiting (app->plugin_screenshots,
 *     plugin/plugin_screenshots.h),
 *   - App_RenderSkipRequestDraw owes this frame a draw, or
 *   - a world-entity view (sailing hull) stands in the loaded scene: which
 *     hulls are drawn in full (Wev render_visible / flattened) is decided
 *     while painting, and the actor projection, the pick classifier and a
 *     deck's dynamic population all read that decision.
 */
int
App_RenderSkipFrame(
    struct App* app,
    int redraw,
    int forced);

/** Live world-entity views (sailing hulls) inside the loaded scene: while
 *  any is, render skip draws every frame (App_RenderSkipFrame). */
int
App_RenderSkipHullsInScene(struct App* app);

/**
 * The most recent frame time reported to App_NoteFrameTime, in microseconds.
 *
 * The newest sample, not the frame-time panel's mean (both from
 * App::frame_clock's frame times): a caller that wants a window
 * keeps its own, and one that wants the last frame cannot recover it from a
 * mean. 0 before the host has reported anything.
 *
 * This is the frame's WORK, with the pacing sleep excluded -- see
 * App_NoteFrameTime. It is the only frame duration in the client that is not
 * just the frame cap read back, which is what makes it the one worth asking
 * for: divided into a second it says what the client could sustain uncapped,
 * where a wall-clock period says only what it was allowed to deliver.
 */
uint64_t
App_LastFrameUs(struct App const* app);

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

/**
 * The screen's frame cap, in frames per second; 0 is none.
 *
 * ONE answer for the loop, wherever the number comes from: revconfig's
 * `[frame] cap_fps` on every revision, or -- where that profile says
 * `cap_source=cs2` -- the cache's own Limit Framerate device option, with the
 * profile's number as the value while the option is unset. The loop hands it
 * to the pacer (ToriRS_Pacer_SetCapFps), which is the same draw budget its
 * adaptive step-down uses; nothing else may gate a draw.
 */
int
App_FrameCapFps(struct App const* app);

/** Whether the most recent App_RunOnce reached interaction. A shell retains
 * one-shot input while false so an async CS2/tick wait cannot eat a mouse-up or
 * key edge. */
int
App_InputFrameConsumed(struct App const* app);

/**
 * Does chrome DRAWN IN THIS CANVAS own this canvas point?
 *
 * The same question the game's own hover, wheel and click-to-walk gates ask
 * (app_chrome_wants_pointer), exported because the TOUCH layer has to ask it
 * too: a finger landing on the plugin window is landing on a window, not on the
 * world it happens to be drawn over, and only this can tell the two apart.
 * Const and side-effect free -- it may be asked at any point in a frame.
 */
int
App_ChromePointerOwned(struct App const* app, int x, int y);

/**
 * Does anything drawn over the 3D world own the pointer at (x, y) -- the
 * client's chrome, or one of the game's own interfaces?
 *
 * The touch layer's overlay test (@see ToriRS_TouchSetOverlayTest): a finger
 * that comes down here is pressing that thing, not turning the camera.
 */
int
App_PointerOwnedByUi(struct App* app, int x, int y);

/**
 * Is the map editor's panel open, so a finger's one-finger drag is the armed
 * tool's (the marquee, a brush stroke) and never the camera's? The host then
 * gives the touch layer no world viewport (ToriRS_TouchSetViewport w = 0):
 * the drag is a LEFT-button drag, the camera's middle-button synthesis is
 * bypassed, and the World pans under two fingers (workbench 6.1). The game
 * keeps the synthesis whenever the editor is closed.
 */
int
App_EditorOwnsTouch(struct App const* app);

/**
 * Should two fingers be gesture frames (ToriRS_TouchSetGestureFrames) rather
 * than the game's wheel notches and arrow keys? While the editor owns the
 * touch AND the World's camera is the free one (offline, a cutscene, or
 * unlocked online), which the frames move (EditorWorldGesture_Apply). Online
 * with the follow camera locked the frames would have no camera to move, so
 * the legacy synthesis stays on and turns and zooms it as in the game.
 */
int
App_EditorOwnsGestureFrames(struct App const* app);

/**
 * Should the pointer be the pan cursor? While the editor's grab pan runs or
 * is armed: Space held over the World, Alt+Ctrl (Option+Cmd) held there under
 * the Unity scheme, or a pan drag (that grab, or the Unity middle-drag) in
 * progress. main.c hands the answer to PlatformWindow_SetCursor every frame.
 */
int
App_WantsPanCursor(struct App const* app);

/*
 * The map editor, asked (app/app_mapeditor_app.c). Each tests for the editor
 * once and answers no / zero / "no override" in a boot without one, so a
 * caller outside the editor never reads App.map_editor.
 */

/** The map editor's record, NULL in a boot without one: the one accessor the
 *  editor's own units outside the App layer (the agent's editor verbs, the
 *  command panel) take it through. Shared code asks the queries below. */
struct MapEditorApp*
App_MapEditor(struct App const* app);

/** Is there a map editor session (an editor boot whose ToriRSMapEd
 *  answered)? */
int
App_MapEditorOpen(struct App const* app);

/** The open documents with unsaved edits; 0 without an editor. */
int
App_MapEditorUnsavedCount(struct App const* app);

/** The chrome's quarter scale under the editor's View > Chrome scale: `q`
 *  itself on Auto, without an editor, or before its panel is live. */
int
App_MapEditorChromeScaleFor(
    struct App const* app,
    int q,
    int touch);

/** View > Text rendering's "smooth always"; 0 without an editor or before its
 *  panel is live. */
int
App_MapEditorTextSmooth(struct App const* app);

/** Does the editor's layout remember the window's box this frame (its panel
 *  is live)? Then App_MapEditorSetWindowBox tells it. */
int
App_MapEditorKeepsWindowBox(struct App const* app);

void
App_MapEditorSetWindowBox(
    struct App* app,
    int x,
    int y,
    int w,
    int h);

/**
 * Relayout + CS1 re-evaluate + mark for redraw after an out-of-band tree
 * mutation (slot mounts, packet-driven component changes).
 */
void
App_RefreshAfterTreeMutation(struct App* app);

/* ------------------------------------------------------------------------ */
/* Notable moments                                                           */
/* ------------------------------------------------------------------------ */

/*
 * What the client noticed, on its way to the plugin layer.
 *
 * Called from the packet exec, which is why they are App entry points rather
 * than something the exec reaches into the plugin host with: the exec has an
 * App and knows nothing about plugins, and the recogniser these run
 * (game/rs_game_events.h) is a client concept rather than a plugin one.
 * Each is a no-op when no host is running.
 */

/** A line reached the chatbox. `sender` may be NULL for a system message. */
void
App_NotifyChatMessage(
    struct App* app,
    int type,
    char const* sender,
    char const* text);

/** Text was written onto an interface. Only the quest-completion scroll's
 *  title is recognised off this; nothing else is forwarded anywhere. */
void
App_NotifyInterfaceText(
    struct App* app,
    char const* text);

/** The server stated a skill's BASE level. Announces a level-up when it rose
 *  above the last level the client was told, and records it either way. */
void
App_NotifyStatLevel(
    struct App* app,
    int skill,
    int base_level);

/* ------------------------------------------------------------------------ */
/* Frame capture                                                             */
/* ------------------------------------------------------------------------ */

/**
 * Hand back the frame that was just presented, as width*height top-down ARGB.
 *
 * One per renderer lane, because only the lane knows where its pixels are: the
 * software rasteriser already has a client-side buffer, and a GPU lane has to
 * read one back off the device. Returns 1 when `pixels` was filled.
 */
typedef int (*App_FrameSupplier)(void* user, int* pixels, int width, int height);

/**
 * "The frame is on screen." Called by whichever lane presented it, AFTER the
 * present, and it is what turns a plugin's api->screenshot into a file.
 *
 * `supplier` is pulled rather than pushed, and never called unless a capture
 * is actually waiting -- this is RuneLite's DrawManager.processDrawComplete
 * arrangement and it is the reason a GPU readback is affordable at all. A
 * readback is a pipeline stall; done every frame on the chance someone wants a
 * picture it would cost the frame rate of the whole client, and done only when
 * asked it costs nothing until a skill goes up.
 *
 * A lane with no readback passes NULL, and a supplier that fails returns 0.
 * Either way the capture is still taken, from a software re-render of the same
 * frame -- see the fallback's own comment for why that is a substitute and not
 * an equivalent.
 */
/** Ask for a picture of the next frame, taken from the renderer that is
 *  actually drawing -- see App_RequestScreenshot in app.c for why
 *  TORIRS_EXIT_BMP is not that. Writes PNG. */
int
App_RequestScreenshot(
    struct App* app,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size);

void
App_DrawComplete(
    struct App* app,
    App_FrameSupplier supplier,
    void* supplier_user);

/**
 * Rasterize the current emit buffer into pixels (width x height ARGB).
 *
 * `width`/`height` is the BUFFER. The interface is always laid out at
 * UITREE_LAYOUT_ROOT_W/H; a larger buffer draws the world at its own pixels
 * and scales every 2D command into it, which is how interface scaling sizes
 * the interface without sizing the world. A layout-sized buffer is 1:1.
 */
void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height);

/**
 * Region the last App_Render actually wrote, for a presenter that can copy
 * less than the whole buffer. Returns 0 when the whole canvas must be
 * presented, which is every frame unless damage drawing is on and the frame
 * was retained. @see App::damage.
 */
int
App_PresentDamage(
    struct App const* app,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

/**
 * The same damage as separate rectangles, for a caller that can act on each
 * one independently (the clear, the present). Returns the count and points
 * `out_rects` at them, or 0 when there is no usable list and the single box
 * from App_PresentDamage should be used instead.
 */
int
App_DamageRects(
    struct App const* app,
    struct ToriRS_DamageRect const** out_rects);

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

/** True while APP_STATE_BOOTING; optionally returns the boot bar 0..100. */
bool
App_IsBooting(
    struct App* app,
    int* out_progress);

/**
 * Is this bake one that must show nothing at all?
 *
 * True only for the title bake a logout starts. A host that gets no commit
 * from App_RunOnce already retains its last frame; this is why it got none.
 * @see RS_ClientPhase_HoldsLastFrame.
 */
int
App_BootHoldsLastFrame(struct App const* app);

/**
 * The boot bar's caption, and the scene font id to draw it with.
 *
 * For the lanes that draw the boot bar themselves -- the GPU backends run
 * their own ToriRS_*_DrawBootBar while App_BuildFrame is still refusing a
 * frame -- so that the words, the face and the moment they change are decided
 * once here rather than three times, one per renderer.
 *
 * The call REGISTERS the face in the scene (ToriDraw_SceneFontAdd, which emits
 * TORIDRAW_EVENT_FONT_LOAD); the backends resolve a font id by looking it up
 * there, so a lane that draws the caption must take it from this and not
 * invent an id of its own.
 *
 * Returns NULL when there is nothing to draw -- no sentence for this phase, or
 * no face to draw it with -- and writes *out_font_scene_id only when it
 * returns a caption.
 */
char const*
App_BootBarCaption(
    struct App* app,
    int* out_font_scene_id);

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

/**
 * The quest driver's two RAW dispatcher seams, past the gated api_* wrappers.
 *
 * A quest test is a coroutine resumed on a bare frame tick, which is not a
 * kind the plugin host's dispatch_event allow-list names, so the gated path
 * refuses it. The driver still must press buttons and flip tabs through the
 * ONE dispatcher every real click uses, not a divergent copy -- so these two
 * expose it directly. Test-only by convention and by caller: the driver
 * module they serve is itself registered only when TORIRS_CONTENT_TEST is set.
 *
 * Implemented in plugin/torirs_plugin_bridge.u.c beside the statics they
 * forward to. Both return non-zero when the press was dispatched.
 */
int
App_PluginDriveIfClick(
    struct App* app,
    int component_id,
    int op);

/* The same press on one tree node: a cc_create child, which shares its
 * parent's component id and so cannot be named by it. */
int
App_PluginDriveIfClickNode(
    struct App* app,
    int32_t node,
    int op);

int
App_PluginDriveTabSelect(
    struct App* app,
    int tabno);

/**
 * Where the first live npc of the given cache type is drawn, in canvas pixels,
 * for a headless click on it. Returns the npc's server slot, or -1 when no
 * synced npc of that type is in the scene or its body projects off screen.
 * Wandering npcs make a fixed click coordinate a coin toss; a run that wants
 * the npc's right-click menu asks here on the frame it clicks.
 */
int
App_NpcScreenPosition(
    struct App* app,
    int npc_id,
    int* out_x,
    int* out_y,
    int* out_type);

/**
 * Where ONE npc copy -- the scene element `element_id` -- is drawn, in canvas
 * pixels (App_NpcScreenPosition's mid-body point and margin). Returns its
 * server slot; -2 when no synced npc owns the element, -1 when it projects off
 * screen. App_NpcScreenPosition answers for a TYPE and picks the copy nearest
 * the viewport's centre, which is another copy whenever several share the
 * type (Maiden's crabs): a press meant for one named copy asks here.
 */
int
App_NpcElementScreenPosition(
    struct App* app,
    int element_id,
    int* out_x,
    int* out_y);

/**
 * Where a copy of loc type `loc_id` standing on a DECK is drawn, in canvas
 * pixels -- or, with `loc_id` -1, of the deck loc whose type is NAMED `name`
 * (contains `name`, the name its menu rows carry: a deck's helm changes type with its tier and
 * its state, never its name) and, when `op` is not NULL, that offers the op
 * `op` (a mast's "Work": its name is empty); `name` NULL takes any name: the scenery of every live root-parented world entity's own world,
 * pushed out through its hull the way an aboard actor is
 * (app_world_project_actor). Of the copies on screen, the one nearest the
 * viewport's centre. 0 with the copy's scene element in `out_element_id`; -2
 * when no deck holds the type, -1 when every copy projects off screen.
 */
int
App_DeckLocScreenPosition(
    struct App* app,
    int loc_id,
    char const* name,
    char const* op,
    int* out_x,
    int* out_y,
    int* out_element_id,
    int* out_view_id);

/**
 * A point in world entity `view_id`'s deck -- deck-local fine x/z, `height`
 * fine units above the deck -- pushed out through its hull and projected to
 * canvas pixels, the transform an aboard actor's draw goes through. False when
 * the view is not live or the point projects behind the eye.
 */
bool
App_DeckPointScreen(
    struct App* app,
    int view_id,
    int fine_x,
    int fine_z,
    int height,
    int* out_x,
    int* out_y);

/**
 * The local player's TRUE tile (the server's whole tile, route[0]), level, and
 * where the current walk ends (the map flag; the true tile when no flag is
 * set). Absolute tiles. False before the local player has spawned.
 */
bool
App_LocalPlayerTiles(
    struct App* app,
    int* true_x,
    int* true_z,
    int* level,
    int* dest_x,
    int* dest_z,
    int* flag_x,
    int* flag_z,
    int* draw_x,
    int* draw_z);

/** Movement idleness: route_length == 0 AND no minimap flag set. False
 *  before the local player has spawned. */
bool
App_LocalPlayerIdle(struct App* app, bool* out_idle);

/** Set the orbit camera's yaw/pitch/zoom, the way content_test.c's "camera"
 *  cheat and the quest driver's drive.camera both need to. False (no state
 *  change) when pitch is outside [128,383] or zoom is outside [-1000,10000]. */
bool
App_SetCameraPose(struct App* app, int yaw, int pitch, int zoom);

/**
 * The centre of the open right-click menu's first row whose text starts with
 * `prefix` (colour tags included, e.g. "Tag @yel@"), in canvas pixels, for a
 * headless pick of a plugin's retained row. False when no menu is open or no
 * row matches. `out_text` receives the row's text when non-NULL.
 */
bool
App_MinimenuRowCenter(
    struct App* app,
    char const* prefix,
    int* out_x,
    int* out_y,
    char* out_text,
    size_t out_text_capacity);

/**
 * Exit-dump lines for the live world entities: one `NATIVE_NPC` per synced npc
 * (slot, type, absolute tile) and one `NATIVE_GROUND_STACK` per ground item
 * stack (tile, obj, count, name), so a headless run can say what stood and
 * lay in the scene when it ended.
 */
void
App_TraceWorldEntities(struct App* app);

/** How many plugin world objects exist, are active, and have a live scene element. */
void
App_PluginObjectCounts(
    struct App* app,
    int* in_use,
    int* active,
    int* built);

#endif
