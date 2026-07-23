#ifndef SRC_APP_H
#define SRC_APP_H

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/uitree_anim.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_builder/uitree_builder.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_audio.h"
#include "game/rs_chat.h"
#include "game/rs_cs1_host.h"
#include "game/rs_cs2_host.h"
#include "game/rs_entity_sync.h"
#include "game/rs_if1_buttons.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_player_stats.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "input/torirs_input.h"
#include "inv/inv_manager.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"
#include "toridraw_scene.h"
#include "ui/uitree.h"
#include "ui/uitree_cross.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_hovertext.h"
#include "ui/uitree_interact.h"
#include "varp/varp_manager.h"
#include "world/world_pickset.h"

#include <stdint.h>

struct World;
struct WorldBuilder;
struct PaintersBuffer;
struct RSCache_Dat1Disk;
struct Dat1BuildCache;
struct Dat2BuildCache;
struct ToriRS_CmdBus;
struct ToriRS_Network;

/*
 * Application shell: owns every subsystem and the update loop body, with no
 * platform (SDL) dependency so it compiles headless for tests and the future
 * WASM shell. The platform layer polls input, calls App_RunOnce once per
 * frame, and blits App_Render's pixels.
 */

/*
 * Component ids for the app-pushed overlay nodes (click cross, minimenu).
 * High interface group 0x7FFE keeps them outside any cache interface id.
 */
enum
{
    APP_COM_ID_CROSS = (0x7FFE << 16) | 0,
    APP_COM_ID_MINIMENU = (0x7FFE << 16) | 1,
    APP_COM_ID_HOVERTEXT = (0x7FFE << 16) | 2,
    APP_COM_ID_ENTITY_OVERLAY = (0x7FFE << 16) | 3,
};

/* Which on-disk cache format cache_dir holds. dat2 is the js5-era cache
 * (main_file_cache.dat2 + reference tables); dat1 is the 317/254-era one
 * (main_file_cache.dat + jagfile archives). They differ in every decoder, so
 * the whole asset pipeline is selected from this. */
enum AppCacheKind
{
    APP_CACHE_DAT2 = 0,
    APP_CACHE_DAT1 = 1,
};

struct AppConfig
{
    char const* cache_dir;
    char const* config_dir;
    char const* script_dir;
    int interface_id;
    enum AppCacheKind cache_kind;
    /** RevConfig layout INI. When set, the tree is built from it instead of
     * opening interface_id out of the cache (the only path a dat1 cache has:
     * its interfaces have no gameframe root). NULL/"" = open interface_id. */
    char const* revconfig_ui_ini;
    /** Companion RevConfig sprite/font INI. NULL/"" = none. */
    char const* revconfig_cache_ini;
    /** --connect target "host[:port]". NULL/"" = offline (no networking). */
    char const* connect_target;
    char const* connect_user;
    char const* connect_pass;
    /** Protocol revision name ("lc254", "lc245_2"). NULL = lc254, the
     * authoritative LostCity_Server build. Mock/loopback tests pass
     * lc245_2 explicitly. */
    char const* rev_name;
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
};

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

    /* Phase 3: scene + bridge. */
    struct ToriDraw_Scene* scene;
    struct UITreeSceneBridge bridge;

    /* Phase 4b: world sim + builder (needs provider + scene + varps; the
     * World references assets and scene elements by integer id only). */
    struct World* world;
    struct WorldBuilder* world_builder;
    struct PaintersBuffer* painter_buffer;
    struct ToriDraw_Camera world_camera;
    struct ToriDraw_Position world_camera_pos;
    /* Reference orbit camera (Client-TS followCamera): velocity-driven
     * yaw/pitch, a 1/16-eased anchor that trails the player, and a terrain
     * pitch clamp (cameraPitchClamp, 24.8 fixed) that keeps the eye above
     * nearby ground. cam_key_* latch arrow-held state for the follow step,
     * which runs before key sampling in the frame. */
    int orbit_yaw;
    int orbit_pitch;
    int orbit_yaw_vel;
    int orbit_pitch_vel;
    int orbit_x;
    int orbit_z;
    int camera_pitch_clamp;
    int cam_key_left;
    int cam_key_right;
    int cam_key_up;
    int cam_key_down;
    int world_active; /* 1 once Task_WorldLoad completed */
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
    struct UITreeMinimapDot minimap_dots[128];
    int minimap_dot_count;
    /* Per-frame entity overlay primitives (health bars + hitsplats), filled
     * by the GET_ENTITY_OVERLAYS host request and consumed by the same
     * frame's draw. Reference drawEntities budget: each entity contributes at
     * most 2 bar rects + 4 hitsplats x 3 primitives. */
    struct UITreeEntityOverlay entity_overlays[512];
    int entity_overlay_count;
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

    /* Persistent IF_SETNPCHEAD / IF_SETPLAYERHEAD store (reference keeps
     * model1Type/model1Id on IfType.list and re-resolves getModel every draw):
     * the head packet arrives before the chat interface mounts, so the request
     * must survive and re-apply on tree-topology changes. The async load task
     * makes the composited head scene model available; the per-frame poll binds
     * it onto the MODEL node once mounted (and after each remount). */
    struct AppIfHead
    {
        int com_id;
        int kind;             /* AppIfHeadKind: 0 = npc, 1 = player, 2 = obj (IF_SETOBJECT) */
        int npc_id;           /* npc id, or the obj id for the obj kind */
        int zoom;             /* IF_SETOBJECT wire zoom (modelZoom = zoom2d*100/zoom) */
        int anim_id;          /* IF_SETANIM seq for this head, or -1 (reference modelAnim) */
        uint32_t applied_gen; /* tree generation this was last applied at; 0 = pending */
    }* if_heads;
    int if_head_count;
    int if_head_cap;
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
    struct RS_PlayerStats stats;
    struct RS_CS2Host host;
    struct RS_CS1Host cs1_host;
    /** Runtime interface slots (tabs, modals, chat) — reference Client-TS
     *  mainModalId/sideOverlayId/sideTab fields. */
    struct RS_UISlots slots;
    /** Friends/ignores store feeding the clientCode row pass. */
    struct RS_Social social;
    /** Chat message ring + input state, and its per-frame flattened draw
     *  model (the host hands chat_view to the emit walk). */
    struct RS_Chat chat;
    struct UIChatView chat_view;
    /** Frames the left button has been held over the chat scrollbar (reference
     *  scrollCycle); drives arrow-scroll acceleration and gates grip drag. */
    int chat_scroll_cycle;
    /** Chat input focus. When set, typed keys feed the chat input line and the
     *  debug camera/world hotkeys (WASD/M/spawn digits) are suppressed so they
     *  cannot fire while composing a message. Clicking the chat region focuses;
     *  clicking elsewhere or pressing Escape unfocuses. */
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
    uint64_t last_logic_ms;
    int hover_com_id;
    int clicked_com_id;
    int need_redraw;

    /* Async lifecycle state (no blocking IO outside the platform pump). */
    int app_state;     /* enum AppState */
    int boot_progress; /* 0..100, drives the loading bar while BOOTING */
    int boot_interface_id;
    /** Set when async work mutated the tree; App_RunOnce consumes it with a
     * relayout + CS1 re-eval request + redraw. */
    int pending_tree_refresh;
    int cs1_eval_inflight;
    /** Tree-affecting async work (CS2 hooks, transmits) is in flight; when the
     * runner queue next goes idle the tree gets a refresh pass. */
    int runner_had_work;
    int world_load_inflight;
    /** Send MAP_BUILD_COMPLETE when the in-flight world load finishes (set by
     * the REBUILD_NORMAL packet task, not by hotkey/lazy loads). */
    int world_load_server_driven;
    /** Texture ids requested but not yet published into the scene. */
    int tex_pending[256];
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
    /** Camera scripting (CAM_* packets). shake_axis -1 = no shake. */
    struct
    {
        int scripted; /* 1 while a CAM_MOVETO/LOOKAT script overrides free-fly */
        int shake_axis;
        int shake_amplitude;
        int shake_frequency;
        int shake_speed;
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
    /** Server "local" coords (PLAYER_INFO teleport, exact-move, zone bases)
     * are relative to the classic 104x104 scene origin ((zone-6)*8), but our
     * scene is map-square aligned. Offset from OUR scene base to the classic
     * origin, set on REBUILD_NORMAL: scene_tile = server_local + scene_off. */
    int scene_off_x;
    int scene_off_z;

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
     * drag_com_id -1 = not armed. */
    int inv_drag_com_id;
    int inv_drag_can_drag; /* armed grid's objSwap||objReplace (drag allowed) */
    int inv_drag_from_slot;
    int inv_drag_source_id; /* inv container source id */
    int inv_drag_cycles;
    int inv_drag_grab_x; /* mouse at arm time (reference objGrabX/Y) */
    int inv_drag_grab_y;
    int inv_drag_threshold; /* moved >5px since arm (objGrabThreshold) */
    int inv_drag_dx;        /* emit offset for the armed slot (deadzoned) */
    int inv_drag_dy;
};

/** Construct all subsystems in dependency order. Asserts on failure (parity
 * with the previous bootstrap). */
void
App_Init(
    struct App* app,
    struct AppConfig const* cfg);

/** Tear down in strict reverse of App_Init. */
void
App_Shutdown(struct App* app);

/** Begin opening an interface as the tree root (TS WidgetManager
 * .setRootInterface). Fully async: enqueues the boot task and returns —
 * App_RunOnce pumps it and flips app_state to APP_STATE_READY when the tree
 * (and initial assets) are built. App_Render draws a loading bar meanwhile. */
void
App_OpenRootInterface(
    struct App* app,
    int interface_id);

/** IF_SETTEXT: persist (reference IfType.list semantics) + apply if mounted. */
void
App_IfTextSet(
    struct App* app,
    int com_id,
    char const* text);

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
 * clear projectiles/spotanims (reference mapBuild), and move the minimap
 * destination flag. Entity element ids are untouched — they live in the
 * scene's DYNAMIC pool, which the rebuild's static clear skips.
 */
void
App_WorldRebuildShift(
    struct App* app,
    int base_dx,
    int base_dz);

/** Post-load wiring (camera, height fn, texture sync, minimap bake, and the
 * server ack for a REBUILD_NORMAL-driven load). Runs at the tail of the world
 * load: the fire-and-forget path wires it as Task_WorldLoad's on_done; the
 * server-driven path, which awaits the load, calls it directly after the await.
 */
void
App_WorldLoadFinish(struct App* app);

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
 * One loop-body iteration: pump tasks, run pending 20ms logic ticks
 * (client clock, widget timers, animations), then the per-frame interaction
 * pass and emit rebuild. Returns non-zero when the frame needs re-rendering.
 */
int
App_RunOnce(
    struct App* app,
    uint64_t now_ms,
    struct LibToriRS_Input* input);

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

#endif
