#ifndef SRC_APP_APP_INTERNAL_H
#define SRC_APP_APP_INTERNAL_H

/*
 * The App layer's private header.
 *
 * The App layer is src/app.c plus every unit under src/app/. They share
 * `struct App` by design: each is a subsystem of the orchestrator, not a
 * module lifted out of it, and the boundary gate (tools/appc_map.py) treats
 * the directory as one thing that nothing outside it may include.
 *
 * What is here is exactly what crosses from one unit of the layer to
 * another: the shared macros, enums and structs, and a prototype for every
 * function one unit defines and another calls. A function that appears
 * below is one some other unit depends on; a function that does not is
 * private to its file and static there. The prototypes are grouped by the
 * unit that defines them, so this header is also the layer's map.
 *
 * Written by the split that created the layer; maintained by hand since.
 */

#include "app.h"


#include "ui/uitree_canvas_measure.h"
#include "ui/uitree_canvas_floor.h"
#if defined(TORIRS_UI_EMIT_PMU)
#include "../tools/perf/ui_emit_pmu.u.h"
#endif
#include "bmp.h"
#include "game/rs_minimap_state.h"
#include "game/settle_gate.h"
#include "engine/asset_requests.h"
#include "app/app_tasks.h"
#include "log/torirs_log.h"
#include "torirs_env.h"
#include "torirs_env_values.h"
/* Screenshot encoding. Already linked for the cache codecs; the PNG writer
 * rides along, so a plugin capture costs no new dependency. */
#include "bootmanifest/bootmanifest.h"
#include "engine/boot_bar.h"
#include "miniz.h"
#include "revconfig/revconfig_load.h"
#if !defined(TORIRS_PLATFORM_WEB)
/* The dat1 cache source that is a LostCity server rather than a directory.
 * Native only: a browser build has no host cache to replace, and its reads
 * already leave the process (platform_x_io_web.c). */
#include "platform/platform_x_io_ondemand.h"
#endif
#include "cmd/cmdbus.h"
#include "cs2vm2/cs2vm2.h"
#include "editor/editor.h"
#include "editor/map_editor_app.h"
#include "graphics/convex_hull.h"
#include "torirsmaped/torirs_maped.h"

#include "sideapp/side_app.h"

/*
 * The side-app host: the registry forwards an opaque SideAppHost to every
 * hook, and it is the App. These two are the only conversions.
 */
static inline struct SideAppHost*
app_side_app_host(struct App* app)
{
    assert(app);
    return (struct SideAppHost*)app;
}

static inline struct App*
app_from_side_app_host(struct SideAppHost* host)
{
    assert(host);
    return (struct App*)host;
}

/* Highlight colours. The model silhouette is the brighter of the two because it
 * is the "this is the thing" mark; the ground footprint under it is supporting
 * information and reads as such. */
#define APP_OUTLINE_COLOR_HOVER 0xFFFFFF00u
#define APP_OUTLINE_COLOR_FOOTPRINT 0xFFFF0000u
/* The map editor's SELECT latch -- green, so it never reads as the yellow
 * hover or the red footprint mark it can be drawn alongside. */
#define APP_OUTLINE_COLOR_EDITOR_SELECT 0xFF00FF00u
/* 0 opaque .. 255 invisible. High enough that the model reads through it. */
#define APP_OUTLINE_FILL_TRANS 205
#if defined(TORIRS_PLATFORM_ANDROID)
/* For the boot refusal below: on a phone there is no terminal to print to and
 * no shell to have typed the command, so a refusal has to reach the screen. */
#include "platform/platform_android.h"
#endif


#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "engine/png_decode.h"
#include "engine/task_obj_model_load.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/toridraw_element_anim.h"
#include "engine/world_seq_source_toridraw.h"
#include "engine/torirs_chrome_skin_baked.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_model_inst_cache.h"
#include "engine/torirs_worldmap_from_rscache.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_cmd_render.h"
#include "engine/uitree_role_load.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "game/preview_state.h"
#include "game/rs_attack_option.h"
#include "game/rs_client_trigger.h"
#include "game/rs_clientcode.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_entity_apply.h"
#include "game/rs_ground_items_dirty.h"
#include "game/rs_game_events.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_minimenu_cross.h"
#include "game/rs_worldmap.h"
#include "game/rs_worldmap_drag.h"
#include "game/rs_worldmap_render.h"
#include "game/sailing_navigation.h"
#include "game/task_cs1_run.h"
#include "game/task_cs2_run.h"
#include "game/task_exec_entity_info.h"
#include "game/task_gameproto_exec.h"
#include "game/varc_ids.h"
#include "input/torirs_input_cmd.h"
#include "input/torirs_keymap.h"
#include "net/jbase37.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/revpacket.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "painters/painters.h"
#include "painters/painters_cull_project.h"
#include "painters/scene_occluders.h"
#include "perf/torirs_perf.h"
#include "platform/platform_memory.h"
#include "platform/platform_renderer_soft3d.h"
#include "plugin/task_plugin_io.h"
#include "plugin/torirs_plugin_lua.h"
#include "plugin/torirs_plugin_mesh.h"
#include "plugin/torirs_plugin_registry.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"
#include "render/torirs_wedge_camera_path.h"
#include "render/torirs_viewport_projection.h"
#include "render/torirs_world_projection.h"
#include "toridraw.h"
#include "toridraw_model_transform.h"
#include "ui/dev_overlay.h"
#include "ui/torirs_chrome_panel_draw.h"
#include "ui/uitree_build.h"
#include "ui/uitree_frame.h"
#include "ui/uitree_input_signature.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "ui/uitree_keyboard_owner.h"
#include "ui/uitree_popup_place.h"
#include "ui/uitree_iface_stats.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_obj_cell.h"
#include "world/world.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Includes that used to sit beside the definition that needed them. */
#include "plugin/torirs_plugin_panel_route.h"
#include "ui/uitree_inv_view.h"
#include "ui/uitree_scroll.h"
#include <time.h>

/* ---- forward struct declarations the prototypes below need ---- */
struct App;
struct AppConfig;
struct EntitySpotanim;
struct AppModelRecolorSpec;
struct PluginObject;
struct CS2VM2_Thread;
struct LibToriRS_Input;
struct RS_CS2GroundObj;
struct RS_ChatFilters;
struct RS_MinimenuBuildCtx;
struct RS_MinimenuSelection;
struct RS_PreloadStep;
struct Task_AppSpawn;
struct ToriDraw_Model;
struct ToriRSChrome;
struct ToriRSChromePrim;
struct ToriRS_Frame;
struct ToriRS_GroundItemSnapshot;
struct ToriRS_HoverTarget;
struct ToriRS_MapElement;
struct ToriRS_NpcEntityFacts;
struct ToriRS_NpcSnapshot;
struct ToriRS_Npctype;
struct ToriRS_PickHits;
struct ToriRS_PluginEngine;
struct ToriRS_Spotanimtype;
struct ToriRS_Task;
struct ToriRS_WidgetRef;
struct UIIntent;
struct UIInteractOut;
struct UIMinimenu;
struct UITree;
struct UITreeEntityOverlay;
struct UITreeHostRequest;
struct UITreeObjCell;
struct Wev;
struct WevDeckBox;
struct World;
struct WorldEntityFacet_Animation;
struct WorldEntityFacet_Chat;
struct WorldEntityFacet_DrawPosition;
struct WorldEntityFacet_IdleAnimations;
struct WorldEntityFacet_ViewPlacement;
struct WorldEntity_NPC;
struct WorldEntity_ObjStack;
struct WorldEntity_Player;
struct WorldEntity_Scenery;

/* ---- definitions shared across the layer ---- */


enum
{
    APP_LOGIC_TICK_MS = 20,
    APP_MAX_CATCHUP_TICKS = 5,
    /*
     * The frame queue's runaway tripwire. NOT a frame budget.
     *
     * The bound that used to live at the call site (512 booting, 32 otherwise,
     * from 8f3028ede under the note "a small budget keeps frame pacing")
     * throttled the pipeline to budget-times-framerate -- 1600 steps a second
     * once past boot -- which made the frame cap decide how fast the world
     * could load. This client streams its whole world through that pipeline.
     *
     * Set far above any frame that is making progress, so reaching it means a
     * task never returns IDLE. That aborts, because the alternative is a
     * client that looks merely slow for a reason nothing reports.
     */
    APP_ASYNC_STEP_LIMIT = 5000,
    /* Outbound silence that has to pass before the NO_TIMEOUT keepalive goes
     * out. The reference's own figure (Client.ts:2181), and far below the
     * server's idle cutoff, so one late tick cannot cost the session. */
    APP_NET_KEEPALIVE_MS = 1000,
    /* Mouseover text origin inside the viewport. The reference container puts
     * its text child at (0,0); the classic client drew the same line at
     * (4, 15) — one padded cell in, with the baseline a line down. Ours is a
     * text box, so the baseline offset comes from the font ascent. */
    APP_HOVERTEXT_INSET_X = 4,
    APP_HOVERTEXT_INSET_Y = 2,
    /* Middle-button rotate. Yaw is 2048 units per turn, so 4 units per pixel
     * puts a full turn at 512 px of travel — roughly the viewport's width.
     * The orbit pitch band is only 255 units wide, so it moves at half that. */
    APP_WORLD_MMB_YAW_PER_PX = 4,
    APP_WORLD_MMB_PITCH_PER_PX = 2,
    /* Free camera (offline / scripted): no orbit distance to scale, so a notch
     * dollies along the view axis instead. The follow camera's notch is not
     * here — it is `[camera] wheel_step=` in the revconfig, beside the
     * `zoom_closest=`..`zoom_furthest=` band it moves in. */
    APP_WORLD_ZOOM_FREECAM_STEP = 140,
};


/*
 * Send an outbound packet built by a net_out_* builder, gated on networking.
 * The builder writes into a scratch buffer using the game out-cipher; the
 * bytes then queue to the socket via the subsystem's SEND_DATA ring.
 *
 * Building and queueing remain one operation so the outbound ISAAC stream
 * cannot advance without the corresponding packet being sent.
 */
#define APP_NET_SEND(app, builder_call)                                                            \
    do                                                                                             \
    {                                                                                              \
        if( (app)->net && (app)->net->state == TORIRS_NET_GAME )                                   \
        {                                                                                          \
            uint8_t _nsbuf[1100];                                                                  \
            int _nslen = builder_call;                                                             \
            if( _nslen > 0 )                                                                       \
            {                                                                                      \
                ToriRS_Network_SendRaw((app)->net, _nsbuf, _nslen);                                \
                NetKeepalive_NoteSend(                                                             \
                    &(app)->net_keepalive,                                                         \
                    FrameClockSerial_LastFrameMs(&(app)->frame_clock.serial));                     \
            }                                                                                      \
        }                                                                                          \
    } while( 0 )


/* The height OVERHEADS hang off, which is not always the model's height.
 *
 * Reference `Actor.getLogicalHeight` and the NPC override of it: an npc whose
 * type states `height` (opcode 124) anchors its bar and splats at that instead
 * of at the model, and the model is unaffected either way. Otherwise the anchor
 * is `logicalHeight`, which the reference refreshes from each model it builds
 * and initialises to 200 -- so an actor that never builds a model keeps 200
 * rather than collapsing to the floor. That default is the whole reason a
 * model-less marker npc reads as floating slightly above its tile there, and
 * `height` is how a record moves it deliberately. */
#define APP_OVERLAY_DEFAULT_LOGICAL_HEIGHT 200


/* Scene font id for the minimenu (reference uses bold-12; dat2 fonts-table
 * archive 496 in this cache era, e.g. bank title font). Dat1 has no fonts
 * table: its fonts live in the title jagfile and are pinned at cache-font
 * slots 0-3 by RevConfig, where b12 is slot 2. Falls back to any text node's
 * already-resolved scene font when b12 cannot load. Declared above the plugin
 * includes because the plugin window's CS2 presentation sets its rows in the
 * same p12 the interfaces use. */
/*
 * The three fonts the client draws with directly, named the way the RevConfig
 * profile names them. `app_font_cache_id` turns one of these into the number
 * this cache uses — a dat1 scene slot or a dat2 fonts-table archive id, which
 * are different numbers for the same font.
 *
 * Minimenu and hovertext use bold-12; hitsplat numbers use p11 (reference
 * `this.p11.centreString`); the rebuild overlay uses p12 (Client-TS
 * `p12.centreString`).
 */
#define APP_FONT_B12 "b12"

#define APP_FONT_P11 "p11"

#define APP_FONT_P12 "p12"


/*
 * A multiNpc cannot be resolved before its wrapper config is resident. The
 * packet path used to try exactly that, get a cache miss, and permanently
 * spawn the model-less wrapper. Keep the config walk and its asset waits in a
 * reusable task so initial adds, server retypes and local-var remorphs all obey
 * the same cold-cache-safe rule.
 */
struct Task_NpcMultiLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int base_npc_id;
    int* out_npc_id;
    int resolved_npc_id;
    int model_i;
    int seq_i;
    /** Body parts and movement sequences queued and not yet ended. */
    int pending;
};


enum
{
    APP_CAMERA_MOVEMENT_SPEED = 70, /* v1 RUNESCAPE_CAMERA_MOVEMENT_SPEED */
    APP_CAMERA_ROTATION_SPEED = 10,
};


/* Classic human animation set (players; INTERFACE_PLAYER_IDLE_SEQ parity). */
enum
{
    APP_PLAYER_SEQ_READY = 808,
    APP_PLAYER_SEQ_WALK = 819,
    APP_PLAYER_SEQ_WALK_B = 820,
    APP_PLAYER_SEQ_WALK_L = 821,
    APP_PLAYER_SEQ_WALK_R = 822,
    APP_PLAYER_SEQ_TURN = 823,
    APP_PLAYER_SEQ_RUN = 824,
};


/* Config-driven color/texture swaps for a built model (npc/loc style). NULL
 * where the caller has none. */
struct AppModelRecolorSpec
{
    const int* recolors_from;
    const int* recolors_to;
    int recolor_count;
    const int* retextures_from;
    const int* retextures_to;
    int retexture_count;
    /** Every face to this colour first (deob class131.method4474). */
    bool has_tint;
    int tint;
};


/* Convert + merge + recolor + scale + light one drawable model from cache model ids.
 * SYNCHRONOUS: the models must already be resident (callers await
 * CreateTask_ModelLoad first — the spawn tasks do). Returns an owned model
 * or NULL when any part is missing.
 *
 * scale_xz/scale_y are 128 == 1.0 (pass 128,128 for none). Applied before the
 * rest-pose capture, because animation frames reset vertices from the capture —
 * a scale applied after it would vanish on the first animated tick. The
 * reference re-scales the animated copy every frame instead (NpcModelLoader);
 * scaling the base is equivalent for rotation frames and off by the scale
 * factor only on a frame's translate deltas, which nothing visible exercises.
 *
 * light_actor selects the actor regime (players/NPCs/spotanims/projectiles) vs
 * the scene regime (ground objs). light_ambient/contrast are signed config
 * offsets added onto the regime base (0,0 for Client-TS NPC bodies). */
enum
{
    APP_LIGHT_SCENE = 0,
    APP_LIGHT_ACTOR = 1,
};


/*
 * The sprite-group id of `headicons_prayer`.
 *
 * An npc's opcode-102 icon names its group as a NUMBER, and the client
 * resolves that pack by NAME (static_sprites.c, STATIC_SPRITE_HEADICONS_PRAYER)
 * — the provider offers no synchronous name -> group-id lookup to close the
 * gap with, only an async load task. So the number is stated here, from
 * `OSRS-Content/osrs-content/pack/8_sprites.pack` line 441, where it is the
 * only group any of this cache's 77 headicon-bearing npc records names.
 *
 * Failure mode if a future cache renumbers it: npcs stop drawing overheads.
 * That is the deliberate direction — a record naming an unrecognised group is
 * skipped rather than drawn out of the prayer pack, because an icon that says
 * "Protect from Magic" when the record meant something else is worse than no
 * icon at all.
 */
#define APP_HEADICONS_PRAYER_GROUP 440

/*
 * The async boot, and opening interfaces into the tree.
 *
 * Included into app.c rather than compiled on its own. The boot protothread
 * builds the root tree, awaits the overlay font, runs the seeding steps and
 * flips the app READY -- it is a SEQUENCE over most of App, and the sequence is
 * the contract. The split is for READING; the translation unit is unchanged.
 */

/* The async boot protothread: builds the root tree (RevConfig or cache
 * interface open), awaits the overlay font, then runs the synchronous
 * seeding steps and flips the app READY. All IO flows through the platform
 * pump via the per-frame task stepping — nothing here blocks the frame loop. */
struct Task_AppBoot
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    /** The profile's preload task, run as this task's first inline child
     *  (on the main queue, where its frame requests are legal); NULL once
     *  taken, or for a profile that names none. */
    struct ToriRS_Task* preload;
};


enum AppSpawnKind
{
    APP_SPAWN_PLAYER = 0,
    APP_SPAWN_NPC,
    APP_SPAWN_OBJ,
    APP_SPAWN_ENTITY_SPOTANIM,
    APP_SPAWN_PLUGIN_OBJECT,
};


struct Task_AppSpawn
{
    struct ToriRS_Task task;
    struct pt pt;
    /* Listed in app->pending while it is a parallel task (app_spawn_task_add:
     * every kind but APP_SPAWN_ENTITY_SPOTANIM, the assets table's loader). */
    struct RS_ParallelTask par;
    struct App* app;
    /* The worldview cursor AS OF ENQUEUE (WorldviewRegistry_Active). Spawn tasks run
     * at the END of the exec FIFO, behind later packets — including the
     * SERVER_TICK_END that resets the live cursor — so the apply cannot read
     * the live cursor when it finally runs; it must carry its own copy. The
     * view can legitimately die while the task is parked (a boat despawns
     * mid-flight), so applies guard on WorldviewRegistry_IsLive rather than
     * asserting. */
    int view;
    /* No kind left here carries a scene generation. Projectiles, map
     * graphics and loc changes, which did, are parallel tasks with their own
     * (app/app_tasks_effects.c, app/app_tasks_locs.c). */
    enum AppSpawnKind kind;
    int tile_x;
    int tile_z;
    int level;
    int npc_id;
    int obj_id;
    int model_id;
    int seq_id;
    int model_i;
    int spotanim_id;
    /* APP_SPAWN_ENTITY_SPOTANIM: the body scene element of the player/npc the
     * attached graphic belongs to (stable, scene-unique key to re-find the live
     * entity when the async load lands). */
    int entity_element_id;
    /** The models and sequences this spawn awaits, fanned out and not yet ended. */
    int pending;
    /* APP_SPAWN_PLUGIN_OBJECT: the plugin object handle whose assets this task
     * is waiting on. Not a pointer -- the record can be destroyed while the
     * task is parked, and the handle re-resolves to NULL rather than to freed
     * memory. */
    int plugin_object;
};

/* ---- variables shared across the layer ---- */
extern long g_torirs_max_frames;
extern long g_torirs_frame_no;
extern int g_plugin_page;
extern int g_plugin_page_built;
extern int g_plugin_fullscreen;
extern int g_plugin_fullscreen_built;
extern int g_tex_trace_frame;
extern int g_torirs_painter_force;

/* ---- app_boot.c ---- */
void
app_provider_set_cache_profile(
    struct App* app,
    struct AppConfig const* cfg);

void
app_boot_refuse(char const* message);

void
app_async_polls(struct App* app);

/* ---- the assets table's loaders (app/app_assets.c) ---------------------- */

/** Make app->assets and register a loader for every kind. */
void
App_AssetsInit(struct App* app);

void
App_AssetsFree(struct App* app);

/** A composite kind's loader, owned by the unit whose task follows its
 *  chain. `data` is the App. */
void
App_AssetsSetLoader(
    struct App* app,
    enum AssetKind kind,
    enum AssetStart (*start)(
        void* data,
        struct Assets* assets,
        struct AssetKey key),
    bool (*resident)(
        void* data,
        struct AssetKey key));

/** Is a leaf asset here (model, texture, sprite, sequence, component pack,
 *  sound, skybox, obj model)? */
bool
App_AssetResident(
    struct App* app,
    struct AssetKey key);

/** Every asset an attached graphic needs (its type, model, sequence) is in. */
int
app_entity_spotanim_resident(
    struct App* app,
    int spotanim_id);

/** Per frame, before the polls: the table's clock, and back-offs that are
 *  over restarted. */
void
app_assets_frame(struct App* app);

void
app_boot_bar_caption(
    struct App* app,
    int* pixels,
    int width,
    int height,
    int center_x,
    int baseline_y,
    char const* text,
    int font_scene_id);

void
app_title_swap_if_pending(struct App* app);


/* ---- app_camera.c ---- */
int
app_world_clamp_pitch(
    struct App const* app,
    int pitch);

/** The world camera keys, once per view they steer: the side-apps' keys of
 *  the camera-flight and camera-keys phases are delivered inside. */
void
app_world_camera_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out);

/** Move the frame's camera eye along the ground: forward along its yaw
 *  (negative is back), and sideways (positive is RIGHT). */
void
app_camera_move_forward(
    struct App* app,
    int amount);

void
app_camera_move_left(
    struct App* app,
    int amount);

int
app_world_camera_zooms(struct App const* app);

void
app_world_camera_mouse(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out);

/** TORIRS_CAM_DEBUG=1: one line whenever a mouse gesture moves the camera
 *  (app_camera.c; the editor's gestures say so too). */
void
app_debug_log_camera(
    struct App* app,
    char const* what,
    int follow_cam);

/* ---- app_mapeditor_camera.c ----
 *
 * The map editor's camera, asked by app_camera.c at three points. Each is a
 * no-op answering 0 while the editor does not own the camera. */

/** The camera keys' flight: the editor's own (while it owns the camera),
 *  then the editor's free camera held over the loaded scene whichever
 *  flight moved it. Once per view the keys steer. */
void
app_map_editor_camera_flight(
    struct App* app,
    int move);

/** The editor's turn / tilt actions into the view's four turn keys, while it
 *  owns the camera: 1 when it set them, 0 when the game's rules apply. */
int
app_map_editor_camera_turn_keys(struct App* app);

/** The editor's grab pan, then its Unity-style scheme: 1 when the scheme took
 *  the mouse whole (the default scheme's drags and wheel do not run). */
int
app_map_editor_camera_mouse(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out,
    int follow_cam);

/**
 * The editor's Unity-style camera scheme (editor_recent.h
 * EDITOR_CAMERA_SCHEME_UNITY): run from the hotkey dispatcher BEFORE the
 * editor's world click, it latches an Alt+left press over the world as an
 * orbit (and a right / middle press as a look / pan) and consumes the frame,
 * so the click never paints, selects or opens the minimenu. The drags
 * themselves move the camera in app_world_camera_mouse. No-op under the
 * default scheme.
 */
void
app_camera_unity_gate(
    struct App* app,
    struct LibToriRS_Input* input);

/**
 * The grab pan's press (docs/MAPED_HOTKEYS.md "Pan (Space)"): with the
 * camera.pan action held (Space) a left press over the world latches the pan
 * under either scheme and consumes the frame, so the editor's click never
 * sees it. Runs after Editor_HotkeysResolve, which says whether the action is
 * held; the drag itself moves the camera in app_world_camera_mouse, and
 * app_camera_unity_gate ends it when the button comes up.
 */
void
app_camera_grab_gate(
    struct App* app,
    struct LibToriRS_Input* input);

/** TORIRS_EDITOR_HOVER's parked tile (a photograph's pointer), ABSOLUTE:
 *  1 when one is parked. */
int
app_map_editor_hover_env_tile(
    struct App const* app,
    int* out_world_x,
    int* out_world_z);

/** The editor's free camera over ABSOLUTE tile (x, z) on `level`, keeping
 *  its yaw; `zoom` fine units out from the tile, or <= 0 to keep the
 *  distance it has now. 0 when the tile is not in the loaded scene. */
int
app_map_editor_camera_to_tile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    int zoom);

int
app_cinema_level(struct App* app);

void
app_world_camera_cinema(struct App* app);

void
app_world_camera_follow(struct App* app);

void
app_world_camera_smooth_reset_end(struct App* app);


/* ---- app_canvas_layout.c ---- */
int
app_wants_text_input(struct App const* app);

/** What the settle gate (game/settle_gate.h) reads: the runners' latches and
 *  pending settle, the server tick, the held scripts, the CS2 host's
 *  undrained requests, the packet queue and the world loader. */
void
app_settle_gate_input(
    struct App const* app,
    struct SettleGateInput* out);



/* ---- app_chat_focus.c ---- */
struct RS_ChatFilters
app_chat_filters(struct App const* app);

int32_t
app_chat_node_index(struct App const* app);

int
app_chat_region(
    struct App const* app,
    int* out_x,
    int* out_y,
    int* out_height,
    int* out_font_id);

int
app_iface_text_input_focused(struct App const* app);

int
app_text_input_focused(struct App const* app);

int
app_text_input_focused_outside_chrome(struct App const* app);

int
app_chrome_holds_keyboard(struct App const* app);

/* The runner camera split's drain steps (app_render.c; struct App_ViewSplit):
 * App_DrainCommands calls them while a script's view is attached. */
void
app_view_split_drain_begin(
    struct App* app,
    struct LibToriRS_Input* game_input);

void
app_view_split_note_runner_command(
    struct App* app,
    struct ToriRS_CmdHeader const* header,
    uint8_t const* payload);

void
app_view_split_drain_end(struct App* app);

void
app_view_split_latch_pointers(struct App* app);

int
app_chat_focus_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed,
    int* out_submit);

void
app_chat_lock_keys(
    struct App* app,
    struct LibToriRS_Input* input);

void
app_chat_lock_paint(struct App* app);

void
app_chat_build_view(struct App* app);

int
app_chat_line_at(
    void* user,
    int x,
    int y,
    char* out_sender,
    int sender_cap,
    int* out_chat_type);


/* ---- app_chrome.c ---- */
struct ToriRSChromePrim const*
app_chrome_merged_prims(
    struct App* app,
    int* out_count);

/** Rasterise the plugin page into its SURFACE and present it, when it changed
 *  since the last time. A no-op while the page is in the canvas. */
void
app_chrome_plugin_surface_present(struct App* app);

/** Does chrome drawn in this canvas own the pointer at (x, y)? */
int
app_chrome_wants_pointer(
    struct App const* app,
    int x,
    int y);

/** Feed one frame's pointer and keyboard to one chrome instance. */
void
app_chrome_route_input(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input);

/** The keyboard half of app_chrome_route_input. */
void
app_chrome_route_keys(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input);

/** Is the chrome's focus on something that types (a text field, a code
 *  view)? */
int
app_chrome_focus_types(struct ToriRSChrome const* ui);

/** The dev panels' tick: the panel toggles, the pick filter, this frame's
 *  input routed into the developer chrome around the side-apps' dev-panel
 *  points, and the chrome built. Early chrome phase, after the plugin
 *  window's tick. */
void
app_dev_panels_tick(
    struct App* app,
    struct LibToriRS_Input* input);


/* ---- app_client_trigger.c ---- */
void
app_client_trigger_loc(
    struct App* app,
    struct World* world,
    struct WorldEntity_Scenery* loc,
    int trigger);

void
app_client_triggers_world_loaded(
    struct App* app,
    struct World* world);

void
app_client_triggers_refire(struct App* app);


/* ---- app_cs2_flush.c ---- */
int
app_cs2_flush_settings_mirrors(struct App* app);

int
app_cs2_flush_notifications(struct App* app);

int
app_cs2_flush_triggeroplocal(struct App* app);

int
app_cs2_enqueue_followups(struct App* app);

enum TaskRunnerStat
app_settle_cs2_frame(struct App* app);


/** The skybox model for this frame, built once resident. */
struct ToriDraw_ModelHandle
app_skybox_handle(struct App* app);

/* The background queue's lifetime (app_world_job.c): made beside the packet
 * FIFO at init, with its clock and the rebuild mode; freed with the other
 * queues, a pending job cancelled first. */
void
app_background_runner_init(struct App* app);
void
app_background_runner_free(struct App* app);

/* A local world load's job (app_world_load.c): the frame's poll, which
 * builds and finishes a load whose gather has ended on the background queue,
 * and its teardown (from app_background_runner_free, before the queue). */
void
app_world_local_load_poll(struct App* app);
void
app_world_local_load_free(struct App* app);
/* What is stepping a queue now (app_world_job.c): the script settle, the
 * logic ticks and the parallel pump name themselves for their span, and a
 * background pass entered inside one is a contract breach. Enter returns the
 * previous site; Leave restores it. */
char const*
App_QueueSiteEnter(char const* site);
void
App_QueueSiteLeave(char const* previous);
/* The site stepping a queue now, or NULL outside every one. */
char const*
App_QueueSiteNow(void);
/* App_RunOnce's floor slice: App_RunBackground for a couple of ms, so every
 * platform loop moves the queue with no change of its own. */
void
app_run_background_floor(struct App* app);
/* TORIRS_QUEUE_CONTRACT_TRACE's tally of floor pumps that had work
 * (app_frame.c, App_QueueContractTraceLine). */
void
app_contract_note_background_floor(void);
/* Is a task left on the parallel queue waiting on WORK -- its read, a load,
 * a join, a trigger -- rather than only on game time? A task parked on a
 * CYCLE waits for the logic tick, not for loading, and is not pending work
 * for App_AsyncPending (app_frame.c). */
int
app_parallel_waits_on_work(struct App const* app);
/* TORIRS_QUEUE_CONTRACT_TRACE: one App_RunOnce while a world job is
 * pending, and whether it committed a frame (app_world_job.c). */
void
app_world_job_contract_frame(
    struct App* app,
    int committed);
/* The packet pump's return, in the frame a background rebuild committed in:
 * how long the packets held behind the REBUILD took (rebuild_frames.held_ms). */
void
app_world_job_held_note(struct App* app);

/* A one-line digest of the root world (base, and per entity kind a count and
 * a hash of ids and tiles), for comparing two runs that applied the same
 * packets at different frames (app_world_job.c). Diagnostics only:
 * TORIRS_WORLD_DIGEST=1 after every finished load, and
 * TORIRS_WORLD_DIGEST_EVERY=<server ticks> from the packet pump, each time a
 * tick's fence has been applied. */
void
app_world_digest_report(
    struct App* app,
    char const* why);
void
app_world_digest_loaded(struct App* app);
void
app_world_digest_tick(struct App* app);

/* TORIRS_REBUILD_TIMING's frame window (app_world_job.c): opened by
 * App_WorldRebuildBegin, told the build/tail split by the background commit,
 * closed by App_WorldLoadFinish, fed by App_NoteFrameTime. Each is a no-op
 * with timing off. */
void
app_rebuild_frames_open(
    struct App* app,
    int background);
void
app_rebuild_frames_finished(struct App* app);
/* TORIRS_REBUILD_TIMING: stamp the end of a stage of this frame's
 * App_RunOnce while a rebuild's window is open. `name` must be a literal. */
void
app_rebuild_mark(
    struct App* app,
    char const* name);

void
app_rebuild_frames_note(
    struct App* app,
    uint64_t frame_us);

/* The world a task that deferred its load may land in, or NULL when the
 * scene it was aimed at is gone: the view died, a rebuild replaced the scene,
 * or a rebuild is in progress (app_parallel_task.c, beside
 * App_ParallelTaskWorld, which is this on a task's header). The effect
 * spawns use it directly. */
struct World*
app_deferred_land_world(
    struct App* app,
    int view,
    unsigned world_load_seq,
    char const* owner,
    int kind);

/* app_world_rebuild.c: show the model a stack's state selects now (its obj,
 * the count variant, its OBJ_CUSTOMISE): give a stack without an element one
 * (and its name and menu), or put the model on the element it has. 1 if it
 * did; 0 if a part is still not resident; -1 if every part is resident and
 * there is still no model (an obj with no ground model), or the scene had no
 * element to give. `announce`: tell the plugins the stack changed (a stack
 * that was waiting for its model; the add announces its own spawn). */
int
app_obj_stack_apply_model(
    struct App* app,
    struct World* world,
    int idx,
    int announce);

/* app_world_rebuild.c: is every part of the model the stack's state selects
 * resident (so app_obj_stack_apply_model can build it now)? */
int
app_obj_stack_model_resident(
    struct App* app,
    struct WorldEntity_ObjStack const* stack);


/* ---- app_if_models.c: the CS2 host's lazy-model hooks ---- */
void
app_cs2_widget_model_lazy(
    void* user,
    int component_id,
    int model_id);

void
app_cs2_widget_npc_head_lazy(
    void* user,
    int component_id,
    int npc_id);

void
app_cs2_widget_model_direct(
    void* user,
    int component_id);


/* ---- app_cs2_scene.c ---- */
int
app_cs2_loc_at_coord(
    void* user,
    int coord,
    int loc_type,
    int* out_layer,
    char* out_name,
    int name_cap,
    int* out_size_x,
    int* out_size_z);

int
app_cs2_player_route(
    void* user,
    int player_uid,
    int index,
    int* out_coord);

void
app_cs2_set_active_player(
    struct App* app,
    int pid);

void
app_cs2_set_active_tile(
    struct App* app,
    int coord);

int
app_cs2_local_route_signature(struct App* app);

int
app_cs2_player_slot_by_name(
    void* user,
    char const* name);

char const*
app_cs2_worldentity_config_name(
    void* user,
    int config_id);

int
app_cs2_npc_by_uid(
    void* user,
    int uid,
    struct RS_ClientOpContext* out);

int
app_cs2_coord_in_scene(
    void* user,
    int coord);

int
app_cs2_objs_on_coord(
    void* user,
    int coord,
    int index,
    struct RS_CS2GroundObj* out);

void
app_ground_items_mark(
    struct App* app,
    struct World const* world,
    int scene_x,
    int scene_z,
    int level);

void
app_ground_items_tick(struct App* app);


/* ---- app_debug_ui.c ---- */
void
app_debug_overlay_init(struct App* app);

/** Release the side-apps and the developer overlay; last in App_Shutdown. */
void
app_debug_overlay_free(struct App* app);

/** The dev chrome's tick: the side-apps' keys and frame hooks of the early
 *  chrome point, then the chrome's display list. */
void
app_debug_overlay_tick(
    struct App* app,
    struct LibToriRS_Input* input);

void
app_settings_colour_tick(struct App* app);

void
app_settings_number_tick(struct App* app);

void
app_debug_log_position(struct App* app);

void
app_debug_height_profile(struct App* app);

void
app_debug_tile_flags(struct App* app);

void
app_debug_tile_project(struct App* app);

void
app_debug_log_bridges(struct App* app);

int
app_xpdrop_debug(void);

void
app_xpdrop_debug_tick(struct App* app);


/* ---- app_entity_sync.c ---- */
void
app_request_entity_seq(
    struct App* app,
    int seq_id);

/* World_SeqSource.want (app_world_load.c binds it): a track was set to it. */
void
app_world_seq_want(
    void* want_userdata,
    int seq_id);

void
app_world_apply_entity_anim_tracks(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Animation const* anim,
    struct WorldEntityFacet_IdleAnimations const* idle);

void
app_world_sync_entity_animations(struct App* app);

struct EntitySpotanim*
app_entity_spotanim_find(
    struct App* app,
    int body_element_id,
    int owner_entity_id);

void
app_entity_spotanim_drop(
    struct App* app,
    int body_element_id);

/* The table's owner ends (App_Shutdown): every entry's body snapshot and
 * graphic models are freed, then the table. */
void
app_entity_spotanims_free(struct App* app);

void
app_world_sync_entity_spotanims(struct App* app);

/* Player TINT (rev-239 tinting block): the revert runs first in the world
 * frame, before the body reconcile; the apply runs last, after the attached-
 * graphic combine. See app_entity_sync.c. */
void
app_world_revert_player_tints(struct App* app);

void
app_world_sync_player_tints(struct App* app);

void
app_world_sync_npc_tints(struct App* app);

/* Npc TRANSPARENCY (rev-241 transparency block): nested inside the tint --
 * reverted before it, applied after it. See app_entity_sync.c. */
void
app_world_revert_npc_transparency(struct App* app);

void
app_world_sync_npc_transparency(struct App* app);

void
app_world_sync_player_transparency(struct App* app);

/* Mount every npc's body whose customisation changed (WorldEntity_NPC
 * body_serial), once its models are resident. Before the spotanim sync, which
 * re-snapshots a body swapped under its merge. */
void
app_world_sync_npc_bodies(struct App* app);


/* ---- app_host_request.c ---- */
int
app_host_request(
    void* user,
    struct UITreeHostRequest* req);


/* ---- app_editor_view.c ----
 *
 * The map editor's viewer glue (editor/editor_view.h has the view's contract;
 * one view per process: the process IS the window). */
struct EditorViewCamera;

/** Every editor boot, right after the editor session opened. A viewer boot
 *  (`[editor:boot] panel=view` / `--editor-view`) builds this process's view,
 *  its toolbar in app->dev_overlay->ui, unlocks the free camera and puts it at the
 *  boot pose; any other editor boot is only remembered, for the page's
 *  "spawn view" (torirs_editor_view_spawn on the web). */
void
EditorView_AppBoot(struct App* app);

/** 1 when this process is a window popped out to show a panel that draws no
 *  world (`--editor-panel <module>`, the module neither "world" nor
 *  "preview": Editor_UiModuleNeedsWorld): the boot's default world load and
 *  the load around the camera stand down (app_boot.c, EditorView_AppFrame).
 *  The mirror, the catalog link and the preview raster are untouched.
 *  0 without the map editor. */
int
EditorView_AppHoldsNoWorld(struct App const* app);

/** Per frame, in the editor block: the toolbar's activation (while the
 *  command panels are hidden -- once "Open panels" showed them, their
 *  dispatcher owns the frame's one activation and offers it to
 *  EditorView_AppActivate), the camera read-back, the publish, and a pose
 *  the view changed (follow, preset, selection) applied to the App's camera,
 *  loading the square it looks at when that is outside the scene. */
void
EditorView_AppFrame(struct App* app);

/** The frame's activated widget, offered by whichever dispatcher drained it.
 *  Returns 1 when it was the view toolbar's. */
int
EditorView_AppActivate(
    struct App* app,
    int widget);

/** The Editor state callback's fan-out (app_editor_on_state). Returns 1 when
 *  the key was the view's alone. */
int
EditorView_AppOnState(
    struct App* app,
    uint32_t key,
    const int32_t* values,
    int count);

/** The painter's level mask for this process's view, 0 when there is no
 *  view or it shows every level. */
uint8_t
EditorView_AppLevelMask(const struct App* app);

/**
 * Open a new view of this session looking at `camera` (the main window's
 * "Open in new view"): EditorView_AppSpawnModule(app, NULL, camera, ...) --
 * a plain viewer -- with the reason logged. Returns 1 when the request went out.
 */
int
EditorView_AppSpawn(
    struct App* app,
    const struct EditorViewCamera* camera);

/**
 * Open a window of this session that shows editor panel `module` alone
 * ("world" = the World panel at `camera`, editing with the Client's shared
 * tool; any other module = that panel filling the window, the world loaded
 * around `camera`'s square; NULL = a plain viewer, no panels). Natively the
 * launch service's `view` verb starts the process (`--editor-view
 * [--editor-panel <module>]`): against the daemon (server=tcp) it dials the
 * daemon -- or, for a plain viewer, the daemon's own SPAWN_VIEW when it has
 * one; under server=embed this process first opens a loopback listener on
 * its embedded ToriRSMapEd (an ephemeral port, polled from
 * EditorView_AppFrame) and the window dials that. On the web the page opens
 * a tab (torirsSpawnEditorView), which needs a daemon. Returns 1 when the
 * request went out; 0 with the reason in `why`.
 */
int
EditorView_AppSpawnModule(
    struct App* app,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size);

/** EditorUiAppServices::world_camera (user_data = the App): the free
 *  camera as a pose read on `level`'s ground. */
int
EditorView_AppWorldCamera(
    void* user_data,
    int level,
    struct EditorViewCamera* out);

/** EditorUiAppServices::world_camera_set (user_data = the App): the free
 *  camera put at `pose` (a scene's Enter, workbench 6.5); 0 while the point
 *  is outside the loaded scene. */
int
EditorView_AppWorldCameraSet(
    void* user_data,
    const struct EditorViewCamera* pose);

/** EditorUiAppServices::spawn_panel (user_data = the App). */
int
EditorView_AppSpawnPanel(
    void* user_data,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size);

/** Before the editor session closes: the embedded server's loopback
 *  listener and every window attached through it. */
void
EditorView_AppShutdown(struct App* app);

/* ---- app_editor_hotkeys.c ---- */
/** The map editor's hotkeys for this frame: snapshot, gates, resolve,
 *  dispatch (docs/MAPED_HOTKEYS.md). Before the developer overlay and every
 *  manifest debug hotkey, which skip the chords it claims. */
void
app_editor_hotkeys_frame(
    struct App* app,
    struct LibToriRS_Input* input);

/** The editor's table claims `key` under this frame's modifiers (bound in a
 *  live context): the side-apps' CTRL keys skip it. */
int
app_editor_hotkeys_claimed(
    struct App const* app,
    enum LibToriRS_KeyCode key);

/** A held editor action (the camera flight, the click accelerators) is
 *  active this frame. */
int
app_editor_hotkeys_active(
    struct App const* app,
    enum Editor_HotkeyAction action);

/** The editor's panels are open: the arrows and W/A/S/D/Q/E are the
 *  editor's camera actions, not the game's. */
int
app_editor_hotkeys_owns_camera(struct App const* app);

/** Re-mark this frame's editor keys in input_claims (app_ui_hotkeys
 *  clears the array first). */
void
app_editor_hotkeys_mark_consumed(struct App* app);

/** Write editor_hotkeys.ini at shutdown when a binding changed. */
void
app_editor_hotkeys_flush(struct App* app);

/* ---- app_hotkeys.c ---- */
/** The OSRS key code (input_claims' key index) of a LibToriRS key, -1. */
int
app_keycode_osrs(int key);

void
app_ui_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input);

/** The world-hotkeys point: its gates, then the side-apps' world keys. */
void
app_world_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out);

/** Deliver the side-apps' key actions of `phase` from `input`, under the
 *  App's key predicate (the CTRL and WORLD gates above). The caller has
 *  passed the phase's own gates. */
void
app_side_app_deliver_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    enum SideAppPhase phase);

/** Probe the side-apps' keys of `phase` from `input` where the phase is
 *  read, before its gates (SideApps_ProbeKeys). */
void
app_side_app_probe_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    enum SideAppPhase phase);


/* ---- app_if_events.c ---- */
int
app_minimenu_events_for_component(
    void* user,
    int com_id,
    int sub_id);

int
app_cs2_events_override_for_component(
    void* user,
    int com_id,
    int* out_events);

int
app_component_target_mask(
    struct App const* app,
    int com_id);

int
app_targetsel_wire_component(struct App const* app);


/* ---- app_design_predict.c ---- */
/**
 * Step the character-design panel's arrow `com_id` on the local player NOW,
 * so the figure does not wait out the tick the server answers in. Returns
 * nonzero when it was one of the panel's arrows and the appearance moved.
 *
 * Called from the click path AFTER the IF_BUTTON has been sent: the server is
 * still the authority and its appearance overwrites this within a tick. @see
 * game/rs_design_panel.h.
 */
int
App_DesignPredictIfButton(
    struct App* app,
    int com_id,
    int op_num);


/* ---- app_if_models.c ---- */
void
app_if_head_poll(struct App* app);

/* Ask again for the heads the tree needs and that have no task: before the
 * frame's last parallel pump (app_frame.c), so the task a want adds is
 * drained into the frame it was asked in, never published ready. Once per
 * tree generation, as the bind (app_if_head_poll) re-applies. */
void
app_if_head_wants(struct App* app);

/** The head entry a head task was made for, if `com_id` still holds it
 *  unlanded (UIIfModels_HeadFor), after forgetting what a close unloaded;
 *  -1 otherwise. */
int
app_if_head_current(
    struct App* app,
    int com_id,
    enum UIIfHeadKind kind,
    int npc_id,
    int zoom,
    uint32_t close_serial);

/** The obj an IF3 widget draws for `count` (ObjType.getCountObj): its count
 *  variant, else `obj_id`. Reads the resident objtype. */
int
app_if_obj_count_variant(
    struct App* app,
    int obj_id,
    int count);

void
app_if_player_model_poll(struct App* app);

void
app_player_model_poll(struct App* app);


/* ---- app_lookup.c ---- */
int
app_font_cache_id(
    struct App const* app,
    char const* font_name);

int
app_font_b12_cache_id(struct App const* app);

int
app_iface_com(
    struct App const* app,
    char const* iface_name,
    int child);

int
app_setting_id(
    struct App const* app,
    char const* setting_name);

int
app_hitsplat_font_scene_id(struct App* app);

int
app_minimenu_font_scene_id(struct App* app);


/* ---- app_map_editor.c ---- */

/** The minimenu's "Select spawn" row: the previewed spawn that scene element
 *  is joins the Spawns selection (Shift adds it, as a list click does). */
void
app_map_editor_spawn_preview_select_element(
    struct App* app,
    int element_id);

/** A world load is starting: the editor's previews and ghosts come down while
 *  the scene they were placed in still stands, and land again in the new one. */
void
app_map_editor_world_load_begin(struct App* app);

/** The editor's hooks.floor_define (editor/editor_ui.h): floor config
 *  `config_id` in the provider's table as given, replacing the one there. A
 *  record that is new to the provider or differs from the one it held, with
 *  `rebake` set, has the window baked again at the next flush. Nothing
 *  happens when the provider already holds exactly this. */
void
app_map_editor_floor_define(
    struct App* app,
    int is_overlay,
    int config_id,
    uint32_t rgb,
    int texture,
    int blend_rgb,
    int hide_underlay,
    int rebake);

struct MapEditorSpawnPreview;

/** The active player in the spawn preview, for the overlay: 1 when it stands
 *  in this world (its scene tile, level, element; selected in the view). */
int
app_map_editor_player_preview_view(
    struct App const* app,
    int* out_scene_x,
    int* out_scene_z,
    int* out_level,
    int* out_element_id,
    int* out_selected);

/** Escape: the active player's mark in the spawn preview goes (its tile
 *  goes with the selection the ui-side Deselect clears). */
void
app_map_editor_player_preview_deselect(struct App* app);

/** The spawn preview's table, for the overlay's markers and outlines. */
struct MapEditorSpawnPreview*
app_map_editor_spawn_preview(struct App const* app);

int
app_modelview_focused(struct App const* app);

void
app_map_editor_open_pending_square(struct App* app);

void
app_editor_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count);

void
app_map_editor_drain(struct App* app);

/** Write the dock layout before the editor is torn down (App_Shutdown). */
void
app_map_editor_layout_flush(struct App* app);

/* The editor's World panel owns the world rect while the editor's panels
 * show (app_map_editor.c, docs/MAPED_STATUS.md). */
void
App_MapEditorWorldView(struct App* app);
int
App_MapEditorWorldLetterbox(
    struct App const* app,
    int box_w,
    int box_h,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);
int
App_MapEditorWorldSignature(struct App const* app);

/** Release the EDIT flush's per-square shadows (app_map_editor.c), at the
 *  editor's close. Safe when none were taken. */
void
app_map_editor_free(struct App* app);

/** STATE_VAR_PREVIEW's values into `varps` (VarPManager_SetPreview): the
 *  world's multiloc / multinpc resolve then honours the shell editor's
 *  preview. App-free (edit_world_test drives it). Returns how many vars are
 *  previewed. */
int
app_map_editor_var_preview(
    struct VarPManager* varps,
    const int32_t* values,
    int count);

/** The editor's whole shutdown (App_Shutdown): the layout and hotkeys files,
 *  Editor_PanelFree, then app_map_editor_free. Needs the map editor. */
void
app_map_editor_shutdown(struct App* app);

/** Fill the App-bound editor modules' services (editor/editor_ui.h struct
 *  EditorUiAppServices) with this App; Editor_PanelInit calls it before the
 *  modules register. */
struct EditorUiAppServices;
void
app_map_editor_ui_services(
    struct App* app,
    struct EditorUiAppServices* out);

/** EDIT layout: a square about to load gets its first shadow, and the
 *  provider is seeded from the document (app_map_editor.c). No-op under STATIC. */
void
app_map_editor_shadow_seed(
    struct App* app,
    int map_x,
    int map_z);

void
app_map_editor_world_click(
    struct App* app,
    struct LibToriRS_Input* input);

/** This frame's gesture frames (input/torirs_gesture.h), editor open: each
 *  offered to the chrome first (Editor_UiGesture), the rest to the World's
 *  free camera (editor/editor_world_gesture.h). Sets the editor's touch
 *  flag from the input's last-event kind. */
void
app_map_editor_gestures(
    struct App* app,
    struct LibToriRS_Input* input);

void
app_map_editor_ghost_update(struct App* app);

void
app_map_editor_preview_update(struct App* app);

struct LocEditorGizmoScreen;
/** The move gizmo (workbench 6.3) on the primary selected loc, projected
 *  through the World panel's camera this frame: 0 (and `out` not valid)
 *  while there is none -- no selection, a tool other than Select or Move,
 *  the primary off the scene. `*out_hot` the handle under the pointer (or
 *  being dragged), LOC_EDITOR_GIZMO_NONE when none. The overlay draws it;
 *  the world click hit-tests the same points. */
int
App_MapEditorGizmoScreen(
    struct App* app,
    struct LocEditorGizmoScreen* out,
    int* out_hot);


/* ---- app_minimap.c ---- */
int
app_minimap_click(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int ctrl_held);

int
app_minimap_flag_abs(
    struct App* app,
    int* out_x,
    int* out_z);


/* ---- app_minimenu.c ---- */
struct RS_MinimenuSelection
app_minimenu_selection(struct App const* app);

struct World*
app_minimenu_view_world(
    void* user,
    int view_id);

void
app_hover_text_update(
    struct App* app,
    int mouse_x,
    int mouse_y);

/**
 * Publish the whole mouseover ENTRY -- subject, text, op count, component --
 * from one menu's acting row. A NULL menu means the pointer is on nothing the
 * menu speaks for, and clears all four.
 *
 * Not static, and not only for a test: the four halves coming from ONE row is
 * the contract that broke (the subject used to be published from the pick set
 * in the logic tick, a different entry), and a contract nothing can call is a
 * contract nothing can check.
 */
void
app_minimenu_entry_publish(
    struct App* app,
    struct UIMinimenu const* menu);

/**
 * The subject one menu row is about, as a dispatch context, and the
 * `RS_MINIMENU_TYPE_*` that names its kind -- RS_MINIMENU_TYPE_NONE with
 * `out` reset for a row that is not about a live world entity.
 */
int
app_minimenu_pick_subject(
    struct App* app,
    struct UIMinimenuPick const* pick,
    struct RS_ClientOpContext* out);

void
app_minimenu_ctx_ground_fallback(
    struct App* app,
    struct RS_MinimenuBuildCtx* mctx,
    int click_x,
    int click_y);

/** After RS_Minimenu_Build, at every site that builds a menu: add the cache's
 *  client ops (clientop_*_set) and drop the rows its per-property modes block
 *  (minimenu_setblockmode / setaddmode; native Minimenu::CanAddOp). `opened` is
 *  1 for the menu a right click or long press shows, 0 for a hover readout or
 *  a left click's default row. */
void
app_minimenu_apply_modes(
    struct App* app,
    struct UIMinimenu* menu,
    int opened);

void
app_minimenu_stamp_node_identities(
    struct App const* app,
    struct UIMinimenu* menu);

void
app_minimenu_open(
    struct App* app,
    int click_x,
    int click_y,
    int click_in_world);

int
app_selection_clear(struct App* app);

bool
app_obj_cell_at(
    struct App* app,
    int px,
    int py,
    struct UITreeObjCell* out);

int
app_inv_drag_ghosting(struct App const* app);

void
app_inv_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed);

void
app_minimenu_close_if_stale(struct App* app);

/* NULL when this pick would run, otherwise the sentence naming the condition
 * that would make app_minimenu_run_option drop it in silence.  For a caller
 * that SYNTHESISES a pick (the plugin bridge's inv/world op bypass) rather
 * than replaying one the user right-clicked: the silence is indistinguishable
 * from a server that ignored the packet, and that ambiguity is what sends a
 * driver hunting content for a press the client never sent.  The string is
 * static storage owned by app_minimenu.c. */
char const*
app_minimenu_pick_refusal(
    struct App const* app,
    struct UIMinimenuPick const* pick);

int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

/** Build the UI-row menu a left click at (x, y) would show and run its
 *  default row -- or, with `plugin_top_row`, only a plugin's row on top
 *  (RuneLite's left click runs the top entry whatever its type; any other top
 *  row leaves the click to the caller's surface). 1 when a row ran, 0 when
 *  none did. */
int
app_minimenu_run_default_ui_row(
    struct App* app,
    int click_x,
    int click_y,
    int plugin_top_row);

int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);


/* ---- app_net.c ---- */
bool
app_client_cheat(
    struct App* app,
    char const* body);

void
app_attack_options_reset(struct App* app);

/**
 * Drain the logout request in App::session_end and run the wait it arms.
 * Once per logic tick.
 *
 * The request is sent and then waited on -- the server's LOGOUT packet, or the
 * socket it closes instead, is what actually ends the session. True when the
 * screen changed, which is only ever on the tick the session ends.
 */
bool
app_logout_tick(struct App* app);

void
app_net_lost(
    struct App* app,
    char const* why);

void
app_net_link_watch(
    struct App* app,
    uint64_t now_ms);

int
app_pump_net_packets(struct App* app);

void
app_cs2_if_written(
    void* user,
    enum RS_CS2IfWrite what,
    int com_id,
    int value,
    char const* text);

void
app_send_if_button(
    void* user,
    int com_id);

void
app_send_resume_pausebutton(
    void* user,
    int com_id);

void
app_send_close_modal(void* user);


/* ---- app_overlay.c ---- */
void
app_overlay_push(
    struct App* app,
    struct UITreeEntityOverlay const* item);

int
app_overlay_count(struct App const* app);

/* The in-scene tile markers: the stage a draw_tile call opens, the reset that
 * empties it and the per-frame placement against the painter buffer. The
 * stage itself is render/tile_mark_stage.h's; these carry the App's world,
 * painter and log to it.
 * @see app_world_tile_marks_place. */
void
app_world_tile_marks_reset(struct App* app);
void
app_world_tile_mark_begin(
    struct App* app,
    int key,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h);
void
app_world_tile_mark_end(struct App* app);
void
app_world_tile_marks_place(struct App* app);

/* Whether an actor's overhead chat line is up (what app_overlay_build_chat
 * draws, and what lifts the overhead stack above it). */
bool
app_overlay_chat_shown(struct WorldEntityFacet_Chat const* chat);

void
app_overlay_build_chat(
    struct App* app,
    int anchor_height,
    int footprint,
    struct WorldEntityFacet_Chat const* chat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int font_id);

/*
 * The overhead stack's lift: how far above an actor's overhead anchor the
 * next thing goes (deob Statics.method6709's var27). It starts at -2; the
 * overhead chat line, when shown, takes 12 (the line is drawn at the lift
 * before it); each health bar drawn takes its height and 2 more
 * (`bars_lift`, their sum); and a lift still at -2 then (nothing above the
 * head) becomes 5. The skull and each prayer icon add 25 and are drawn at
 * the lift that leaves; a player's hint arrow sits its frame's height above
 * the last.
 */
int
app_overlay_overhead_lift(
    bool chat_shown,
    int bars_lift);

/* The lift the first health bar stacks on: -2, or 10 above a chat line
 * (var27 before the bars, app_overlay_overhead_lift's start). */
int
app_overlay_bars_base_lift(bool chat_shown);

/*
 * One overhead health bar (deob Statics.method6709's bar loop), drawn above
 * `lift`, the stack's lift before it (app_overlay_bars_base_lift, then each
 * earlier bar's return): `var27 += height`, drawn with its top at
 * screen_y - var27, `var27 += 2`. A sprite bar's height is its back sprite's
 * (var32.field6911), a rectangle's 5. Returns what it added to the lift
 * (height + 2), 0 when nothing was drawn.
 */
int
app_overlay_build_healthbar(
    struct App* app,
    struct WorldEntityFacet_Combat const* combat,
    int screen_x,
    int screen_y,
    int lift);

/* One overhead icon a player draws: from the PK pack (the skull) or the
 * prayer pack, the frame, and the lift it is drawn at (canvas y = anchor y
 * minus it, x = anchor x - 12). */
struct AppPlayerHeadicon
{
    bool pk;
    int frame;
    int lift;
};

/*
 * A player's overhead icons, bottom up (deob Statics.method6709's player
 * branch): the skull (`skull` >= 0, field530) and then the prayer icons (each
 * set bit of `prayer_mask`, field533's index as a mask), each 25 above the
 * one before, starting from the lift `*io_lift` (app_overlay_overhead_lift).
 * Fills `out` and returns how many; `*io_lift` becomes the lift after them,
 * which the hint arrow stacks on.
 */
int
app_overlay_player_headicon_stack(
    int skull,
    int prayer_mask,
    int* io_lift,
    struct AppPlayerHeadicon out[32]);

/* Draws a player's overhead icons above `lift` (app_overlay_overhead_lift)
 * and returns the lift after them. */
int
app_overlay_build_player_headicons(
    struct App* app,
    int anchor_height,
    int footprint,
    int skull,
    int headicons,
    int lift,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int headicons_scene,
    int pk_scene);

/* The per-frame assets a player's overheads draw with. */
struct AppOverheadAssets
{
    int font_id;         /* the hitsplat font's scene id, -1 none */
    int hitmarks_scene;  /* the dat1 hitmarks archive, 0 none */
    int headicons_scene; /* the prayer icons */
    int pk_scene;        /* headicons_pk: the skull */
    int name_font;       /* p12_full, for the names; -1 none or not resident */
};

/*
 * Whether a player's name is drawn over its head (deob Statics.method6709,
 * the mask client.field777 that CS2 3120..3124 SETDRAWPLAYERNAMES_* set, kept
 * as RS_CS2ClientState.draw_player_names): none at mask 0; the local player
 * by RS_CS2_DRAW_NAMES_SELF; anyone else by _OTHERS, or by _FRIENDS when on
 * the friend list and on a world (method1321), or by _CLAN when in the
 * friends chat channel (method1363). Only the deadman spectator scripts set
 * the mask (clientscripts 2070/2071, deadman_spectator_enable / _update,
 * through proc 2179 deadman_spectator_names: 1 friends, 2 others).
 */
bool
app_overlay_player_name_shown(
    int mask,
    bool is_local,
    bool friend_online,
    bool in_friends_chat);

/* A name's place in the overhead stack: `var27 += 4`, the text's baseline
 * at the anchor minus that, `var27 += 18`. Returns the baseline's lift;
 * `*io_lift` becomes the lift after the name. */
int
app_overlay_player_name_lift(int* io_lift);

/* Whether the player was put in the scene by the last dynamics pass (deob
 * field1463 == the cycle): not hidden, and for a root player stamped by
 * this pass (WorldEntity_Player.drawn_scene_cycle) -- one stacked on a tile
 * another claimed is not. A rider on a deck is drawn by the deck's pass,
 * which claims no tiles. */
bool
app_overlay_player_drawn(
    struct App* app,
    struct WorldEntity_Player const* player);

/*
 * One player's overheads, as deob Statics.method6709's player branch: its
 * health bars over its chat line, its splats, its name when names are shown
 * (app_overlay_player_name_shown) and it was drawn, its skull and prayer
 * icons stacked above them. A hidden player (WorldEntity_Player.hidden, class60
 * .field551) keeps its bars and chat line and draws nothing after them.
 * Returns the stack's lift after the last icon, which a hint arrow on the
 * player stacks on.
 */
int
app_overlay_build_player(
    struct App* app,
    struct WorldEntity_Player const* player,
    struct AppOverheadAssets const* assets);

/* How far above its anchor a hint arrow on a player goes: its frame's art
 * height above the lift the player's icons left (`field1034[1].field6911 +
 * var27`). */
int
app_overlay_player_hint_lift(
    int art_height,
    int player_lift);

/* `player_lift`: for a hint arrow on a player, the lift after that player's
 * overhead icons (app_overlay_build_player_headicons); unused for the other
 * subjects. */
void
app_overlay_build_hint_arrow(
    struct App* app,
    int player_lift);

/* One overhead icon an npc draws: the sprite group's scene id, the frame and
 * the slot (0..7) it is in, which places it. */
struct AppNpcHeadicon
{
    int scene_id;
    int frame;
    int slot;
};

/**
 * Where the npc overhead icon in `slot` is drawn, on the canvas (deob
 * Statics.method6709, the npc branch):
 *
 *     draw(sx + vx - (w >> 1), sy + ((vy - h - 2) * (slot + 1) - h) - 4)
 *
 * with (sx, sy) the anchor projected in the viewport (method6186 at the
 * actor's height + 15), (vx, vy) the viewport's origin and w, h the frame's
 * ART width and height (field6921 / field6911, not its canvas). `screen_x`
 * and `screen_y` are the anchor on the canvas (sx + vx, sy + vy). The slot
 * is the icon's own, not its rank among the drawn ones, and the step between
 * slots is h + 2 - vy: the reference multiplies the viewport's y into it.
 */
void
app_overlay_npc_headicon_place(
    int screen_x,
    int screen_y,
    int viewport_y,
    int slot,
    int art_width,
    int art_height,
    int* out_x,
    int* out_y);

/* The overhead icons an npc draws, in slot order (deob Statics ~24626: every
 * slot whose group and frame are set, the override's when the server set one,
 * else the type's). The prayer group is the client's own pack
 * (`prayer_scene`); any other group is bound by its id through the bridge,
 * which loads it through the assets table (UITreeSceneBridge_BindSprite). A
 * frame the resident group does not have draws nothing, as the reference's
 * `index < sprites.length` test. Returns how many were written to `out`. */
int
app_overlay_npc_headicons(
    struct App* app,
    struct ToriRS_Npctype const* npctype,
    struct WorldEntity_NPC const* npc,
    int prayer_scene,
    int prayer_group,
    struct AppNpcHeadicon out[WORLD_NPC_HEADICON_SLOTS]);

void
app_overlay_build_npc_headicon(
    struct App* app,
    int anchor_height,
    int footprint,
    struct ToriRS_Npctype const* npctype,
    struct WorldEntity_NPC const* npc,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int prayer_scene,
    int prayer_group);

/** The thickness the client's OWN overlay marks are drawn at: the constant
 *  app_overlay_push_segment used to hold. A caller with a thickness of its own
 *  -- a cache highlight group states one, and 0 there means no border at all --
 *  passes that instead. */
#define APP_OVERLAY_SEGMENT_WIDTH 2

void
app_overlay_push_segment(
    struct App* app,
    int screen_x0,
    int screen_y0,
    int screen_x1,
    int screen_y1,
    uint32_t color,
    int line_width);

void
app_overlay_push_polygon_filled(
    struct App* app,
    const int* points_x,
    const int* points_y,
    int point_count,
    uint32_t color,
    int trans);

void
app_overlay_push_polygon(
    struct App* app,
    const int* points_x,
    const int* points_y,
    int point_count,
    uint32_t color,
    int line_width);

int
app_overlay_outline_element_model_trans(
    struct App* app,
    int element_id,
    uint32_t color,
    int fill_trans);

int
app_overlay_outline_element_mesh_trans(
    struct App* app,
    int element_id,
    uint32_t color,
    int fill_trans);

/** The mark the hover footprint and the editor selection both draw: the
 *  silhouette with the standard wash under it. */
int
app_overlay_outline_element_model(
    struct App* app,
    int element_id,
    uint32_t color);

/** The ground footprint of a placed loc: the screen-space hull of its tiles'
 *  corners, at its SW corner's ground height, in the footprint red. Drawn by
 *  the hover footprint and the map editor's ghost and selection marks. */
void
app_overlay_outline_scenery(
    struct App* app,
    struct WorldEntity_Scenery const* scenery);

/** A client cycle, which is what the click feedback's clocks count in. */
#define APP_FEEDBACK_CYCLE_MS 20

void
app_overlay_build_feedback_text(struct App* app);

/** The map editor's world overlay (app_mapeditor_overlay.c): its previews,
 *  grids, gizmo and selection while its panel is shown. Called from the map
 *  editor side-app's world_overlay hook only. */
void
app_overlay_build_editor_selection(struct App* app);


/* ---- app_overlay_entities.c ---- */
void
app_entity_overlay_layout(struct App* app);

int
app_build_canvas_overlays(
    struct App* app,
    struct UITreeEntityOverlay const** out_items);

int
app_build_entity_overlays(
    struct App* app,
    struct UITreeEntityOverlay const** out_items);


/* ---- app_plugin_assets.c ---- */
int
app_plugin_asset_read(
    void* user,
    char const* plugin,
    char const* name);

int
app_plugin_asset_write(
    void* user,
    char const* plugin,
    char const* name,
    void const* data,
    int size);

int
app_plugin_script_read(
    void* user,
    char const* path,
    int serial,
    void (*deliver)(void* deliver_user, int serial, char const* path, void* data, int size),
    void* deliver_user);

int
app_plugin_screenshot(
    void* user,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size);

int
app_plugin_capture_root(
    void* user,
    char const* plugin,
    char const* dir,
    char* out,
    int out_size);

int
app_plugin_files_request(
    void* user,
    int serial,
    char const* verb,
    char const* body,
    int size);

int
app_plugin_capture_read(
    void* user,
    int serial,
    char const* path);


/* ---- app_plugin_bridge.c ---- */
void
app_plugin_fill_npc_for_world(
    struct App* app,
    struct World const* world,
    struct WorldEntity_NPC const* npc,
    struct ToriRS_NpcSnapshot* out);

void
app_plugin_fill_npc(
    struct App* app,
    struct WorldEntity_NPC const* npc,
    struct ToriRS_NpcSnapshot* out);

void
app_plugin_fill_obj(
    struct App* app,
    struct WorldEntity_ObjStack const* stack,
    struct ToriRS_GroundItemSnapshot* out);

void app_script_callback(void* user,struct CS2VM2_Thread* thread,char const* name);
/** RS_CS2Host::item_search: the GE item search offered to the plugins
 *  (on_item_search); -1 when none answered. */
int app_item_search(void* user, char const* query, bool tradeable_only, int* out, int cap);
/** A "::" command the player entered (`body` after the "::"), offered to the
 *  plugins (on_command) before the client sends it or runs a client cheat:
 *  true when one consumed it. */
bool app_plugin_command(struct App* app, char const* body);
/** A plugin owned MODEL control's scene model for (enum UIIfHeadKind, id), or
 *  -1 while it is not composited; `request` queues the load. */
int App_PluginModelScene(struct App* app, int kind, int id, bool request);

int
app_plugin_hover_entity(void* user, struct ToriRS_HoverTarget* out);

struct ToriRS_WidgetRef
app_widget_ref(struct UITree const* tree, int32_t index);

void
app_plugin_frame_bind(struct UITree* tree, void* user);

void
app_plugin_frame_activate(void* user, int active, int canvas, int fixed_w, int fixed_h);

void
app_plugin_menu_build(struct App* app, struct UIMinimenu* menu, int hover_pass);

/** The plugin row for one minimenu option, as on_menu_build and on_menu_select
 *  are handed it (RuneLite's identifier/param0/param1/type included). */
struct UIMinimenuOption;
struct ToriRS_MenuRow;
void
app_plugin_menu_row(
    struct App* app,
    struct UIMinimenuOption const* opt,
    struct ToriRS_MenuRow* row);

/** UITree::mount_listener: an interface group mounted or unmounted. */
void
app_plugin_mount_changed(void* user, int group_id, int open);

/** An inventory container changed (InvManager's change callback fan-out). */
void
app_plugin_inventory_changed(struct App* app, int container_id);

/** A loc change landed in `world`'s scenery (scene tile): on_loc_changed,
 *  for the root world only. `old_loc_id` -1 when none stood there. */
void
app_plugin_loc_changed(
    struct App* app,
    struct World const* world,
    int scene_x,
    int scene_z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int old_loc_id,
    int old_shape,
    int old_angle);

/** Owned item controls waiting on an obj model: bind the icons that landed.
 *  Nothing to do (one test) while none wait. */
void
app_plugin_widget_items_pump(struct App* app);

struct ToriRS_PluginEngine
app_plugin_engine(struct App* app);


/* ---- app_plugin_object.c ---- */
void
app_plugin_objects_rebuild(struct App* app);

void
app_plugin_geometry_settle(struct App* app);

int
app_plugin_model_publish(
    void* user,
    int handle,
    void const* data,
    int size);

void
app_plugin_model_release(
    void* user,
    int handle);

int
app_plugin_mesh_create(void* user);

void
app_plugin_mesh_destroy(
    void* user,
    int handle);

int
app_plugin_mesh_vertex(
    void* user,
    int handle,
    int x,
    int y,
    int z);

int
app_plugin_mesh_face(
    void* user,
    int handle,
    int a,
    int b,
    int c,
    int hsl,
    int alpha);

int
app_plugin_object_create(void* user);

void
app_plugin_object_destroy(
    void* user,
    int handle);

void
app_plugin_object_set_model(
    void* user,
    int handle,
    int source,
    int id);

void
app_plugin_object_recolor(
    void* user,
    int handle,
    int hsl_from,
    int hsl_to);

void
app_plugin_object_clear_recolors(
    void* user,
    int handle);

void
app_plugin_object_set_anim(
    void* user,
    int handle,
    int seq_id,
    int loop);

void
app_plugin_object_set_light(
    void* user,
    int handle,
    int ambient,
    int contrast);

void
app_plugin_object_set_position(
    void* user,
    int handle,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int yaw);

void
app_plugin_object_set_active(
    void* user,
    int handle,
    int active);

int
app_plugin_object_ready(
    void* user,
    int handle);

void
app_plugin_object_set_fine_position(
    void* user,
    int handle,
    int fine_x,
    int fine_z,
    int level,
    int height,
    int yaw);

int
app_plugin_object_element(
    void* user,
    int handle);

int
app_plugin_object_hit(
    void* user,
    int handle);

int
app_plugin_object_model_id(
    struct App* app,
    struct PluginObject const* obj);

int
app_plugin_object_seq_id(
    struct App* app,
    struct PluginObject const* obj);

void
app_plugin_object_materialize_now(
    struct App* app,
    int handle);


/* ---- app_plugin_panel.c ---- */
int
app_plugin_io_down(struct App const* app);

void
app_plugin_window_set_open(struct App* app, int open);

int
app_plugin_panel_overlay_visible(
    struct App const* app,
    int index,
    struct UITreeEntityOverlay* out);

void
app_plugin_panel_tick(struct App* app, struct LibToriRS_Input* input);

/** Register the plugin window's toggle key with the side-app registry.
 *  Called from app_tools.c only. */
void
app_plugin_window_register(struct App* app);

/** Is the plugin page bound to a SURFACE (ToriRSChromeExec::surface_size)
 *  rather than the canvas? Then nothing of it is drawn or hit in the canvas. */
int
app_plugin_page_on_surface(struct App const* app);

/** That surface's size in pixels. @return 0 while it has none. */
int
app_plugin_page_surface(struct App const* app, int* out_w, int* out_h);


/* ---- app_session_resume.c ---- */
/**
 * Hand the page the session a reload should ask for back.
 *
 * Called on a login the server ACCEPTED, and never anywhere else: a boot can
 * then only ever resume a session that has existed. `user` must be the name
 * that was dialled and `resume_token` the key that session authenticated on --
 * see app_session_resume.c for what the page does with them, and for how long.
 *
 * No password crosses. A reload presents the token instead, and a token the
 * server will not honour ends on the login form rather than behind a silent
 * second login attempt.
 */
void
app_session_resume_remember(
    char const* user,
    char const* resume_token);

/**
 * app_session_resume_remember with what `net` just dialled, and the token its
 * handshake left behind (app_session_resume_token). Called after EVERY
 * successful handshake, the in-process reconnect's included: each one
 * authenticates on a new seed, and a reload presenting the previous one is
 * presenting a key the server has already retired.
 *
 * A session with no token is forgotten rather than half-kept: without one
 * there is nothing a reload could ask for.
 */
void
app_session_resume_remember_net(struct ToriRS_Network const* net);

/**
 * The resume token for `net`'s current session: "s0,s1,s2,s3,index", the
 * cipher seed a GAMERECONNECT presents and the slot RECONNECT_OK will not
 * restate. "" when the revision has no seed reconnect or no session has
 * authenticated yet.
 */
void
app_session_resume_token(
    struct ToriRS_Network const* net,
    char* out,
    int out_size);

/**
 * Read a token written by app_session_resume_token. 0 for anything else --
 * it arrives on the command line, from a page that may be older than this
 * client.
 */
int
app_session_resume_parse(
    char const* token,
    int32_t out_seed[4],
    int* out_local_index);

/**
 * Drop it. The player logged out, and coming back into the session they just
 * left is the one thing they did not ask for.
 *
 * Idempotent, and legal before anything was ever remembered.
 */
void
app_session_resume_forget(void);

/** Sign the page out of its site login (the gate's /logout). The browser's
 *  only; a native client has no site login and this does nothing there. */
void
app_session_site_sign_out(void);

/** Whose session is being held, or "" for none. The page holds the token;
 *  this side keeps only the name, which is what makes the rule observable
 *  from a test on a lane that has no page. */
char const*
app_session_resume_user(void);


/* ---- app_textures.c ---- */
int
app_tex_trace_enabled(void);

void
app_sync_textures(struct App* app);

/** The textures models built since the last call want (at most 256): each
 *  already in the provider is published now, each not is one
 *  Task_TexturePublish, a failed or resident or pending one nothing. */
void
App_TexturesWant(
    struct App* app,
    int const* ids,
    int id_count);

/** Is a Task_TexturePublish pending for texture `id` (tests, the want). */
int
App_TexturePublishPending(
    struct App const* app,
    int id);


/* ---- app_tick.c ---- */
int
app_logic_tick(struct App* app);

/* The tick's music step: a request the player holds is installed by a
 * parallel task that awaits it (Task_MusicInstall), a newer song cancelling
 * the older task. */
void
app_music_tick(struct App* app);

/* Is a music install task pending? Its song in *out_song_id. (tests) */
int
app_music_install_pending(
    struct App const* app,
    int* out_song_id);


/* ---- app_title.c ---- */
char const*
app_reboot_timer_text(struct App* app);

int
app_title_caret_blink(struct App const* app);

void
app_title_flames_start(struct App* app);

void
app_title_flames_stop(struct App* app);

void
app_title_flames_tick(
    struct App* app,
    uint64_t now_ms);

void
app_title_sync_groups(struct App* app);

void
app_title_state_changed(struct App* app);

/** True when the title music button reads "on": enabled and audible. */
bool
app_title_music_on(struct App const* app);

/** The title music button was clicked. */
void
app_title_music_toggle(struct App* app);

/** "Sign out" on the accounts screen: the device forgets every account it
 *  saved, and a page signed in to a site login leaves it. */
void
app_title_sign_out(struct App* app);

struct RS_PreloadStep const*
app_preload_announce(
    struct App* app,
    char const* step_name);

void
app_title_progress(
    struct App* app,
    int percent,
    char const* string_key);

/**
 * Take the loading bar down now that the boot is finished -- or, on the boot a
 * reloaded page makes, leave it standing with the reconnect's caption so the
 * login form it covers is never uncovered for a frame.
 */
void
app_title_boot_settled(struct App* app);

int
app_title_field_line(
    struct App* app,
    struct UITreeHostRequest* req);

void
app_title_submit(struct App* app);

int
app_title_tick(struct App* app);


/* ---- app_tool_damage_test.c ---- */
/** Register the damage test. Called from app_tools.c only. */
void
app_tool_damage_test_register(struct App* app);

/* ---- app_tool_frame_time.c ---- */
/** Register the frame-time panel. Called from app_tools.c only. */
void
app_tool_frame_time_register(struct App* app);

/* ---- app_tool_free_camera.c ---- */
/** Register the free camera's keys (flight and unlock). Called from
 *  app_tools.c only. */
void
app_tool_free_camera_register(struct App* app);

/* ---- app_tool_hover_footprint.c ---- */
/** Register the hover footprint outline. Called from app_tools.c only. */
void
app_tool_hover_footprint_register(struct App* app);

/* ---- app_mapeditor_app.c ----
 *
 * The map editor as the App holds it (editor/map_editor_app.h, App.map_editor,
 * NULL in every boot that is not an editor boot). Every unit of the layer
 * that is not a map editor unit asks these; each tests for the editor once
 * and answers no when there is none. The map editor's own units read
 * App.map_editor directly. Each query keeps the exact test its sites made:
 * where two sites tested different things they are different queries.
 */

/** Make the map editor's record and the (unopened) session it holds, in App
 *  Init's editor block once ToriRSMapEd answered. The session is in the
 *  PROCESS group, the record in EDITOR. */
void
app_map_editor_app_open(struct App* app);

/** Free the session App_Shutdown just closed, then the record. */
void
app_map_editor_app_close(struct App* app);

/** Register the map editor as a side-app (its panel toggle, its frame work at
 *  SIDE_APP_PHASE_FIRST_KEYS and _POINTER_LATCHED, its menu rows and its
 *  world-load seeding). Called from app_tools.c only, in an editor boot. */
void
app_map_editor_side_app_register(struct App* app);

/** The map editor's record; asserts there is one. For the editor code still
 *  in shared units (app_overlay.c, app_camera.c), past a query that said
 *  yes. */
static inline struct MapEditorApp*
app_map_editor_of(struct App const* app)
{
    assert(app);
    assert(app->map_editor);
    return app->map_editor;
}

/** Is the editor's command panel shown (an editor and its panel visible)?
 *  The pick filter, the dev panels' keyboard holders, the touch, the
 *  selection overlay. */
int
app_map_editor_panel_shown(struct App const* app);

/** The command panel's Vis row mask, which REPLACES the draw's level mask;
 *  0 without an editor, or on "all levels". */
unsigned
app_map_editor_vis_level_mask(struct App const* app);

/** A viewer window's level cap (editor/editor_view.h), which replaces the
 *  mask after the Vis row; 0 without an editor, or for every level. */
unsigned
app_map_editor_view_level_mask(struct App const* app);

/** The loc editor's move or rotate, recorded into the map editor document
 *  (Editor_PanelRecordLocEdit); 0 when there is no editor or the document
 *  refused it. */
int
app_map_editor_record_loc_edit(
    struct App* app,
    int from_scene_x,
    int from_scene_z,
    int level,
    int loc_id,
    int shape,
    int from_angle,
    int to_scene_x,
    int to_scene_z,
    int to_angle);

/* ---- app_tool_loc_editor.c ---- */
/** Register the loc editor. Called from app_tools.c only. */
void
app_tool_loc_editor_register(struct App* app);

/* ---- app_tool_paint_stepper.c ---- */
/** Register the painter-command stepper. Called from app_tools.c only. */
void
app_tool_paint_stepper_register(struct App* app);

/* ---- app_tool_projectile.c ---- */
/** Register the projectile source-tile tool. Called from app_tools.c only. */
void
app_tool_projectile_register(struct App* app);

/* ---- app_tool_spawn.c ---- */
/** Register the spawn tool (player, npc, ground obj). Called from
 *  app_tools.c only. */
void
app_tool_spawn_register(struct App* app);

/* ---- app_tool_spotanim.c ---- */
/** Register the spotanim tool (free-standing and on every entity). Called
 *  from app_tools.c only. */
void
app_tool_spotanim_register(struct App* app);

/* ---- app_tool_world_reload.c ---- */
/** Register the world reload. Called from app_tools.c only. */
void
app_tool_world_reload_register(struct App* app);

/* ---- app_tools.c ---- */
/** Make the side-app registry from the launch bindings and register every
 *  tool, in the one list there is. App_Init, once. */
void
app_tools_register(struct App* app);

/** Free every tool, the last registered first, then the registry. */
void
app_tools_free(struct App* app);


/* ---- app_ui_host.c ---- */
int32_t
app_displayable_component_node(
    struct App const* app,
    int component_id);

int
app_intent_targets_live(
    struct App const* app,
    struct UIIntent const* intent);

void
app_ui_host_publish_inputs(struct App* app);

void
app_inv_ui_host_change(
    void* userdata,
    int container_id);

void
app_request_cs1_eval(struct App* app);

int
app_measure_text_cb(
    void* user,
    int font_id,
    char const* text);


/* ---- app_varp_transforms.c ---- */
struct ToriRS_Task*
CreateTask_NpcMultiResolve(
    struct App* app,
    int base_npc_id,
    int* out_npc_id);

void
app_varp_change(
    void* userdata,
    int varp_id);

/* The live multiloc remorph for one varp, over the ROOT scene only: what a
 * background rebuild's commit runs for each varp that moved while its staging
 * scene was built against the REBUILD's values (app_world_job.c). Every other
 * view already heard the change when it happened. Returns how many placements
 * it queued a re-place for. */
int
app_varp_refresh_root_loc_transforms(
    struct App* app,
    int varp_id);

void
app_varp_server_update(
    void* userdata,
    int varp_id);


/* ---- app_viewport.c ---- */
void
app_update_world_viewport(struct App* app);

int
app_world_drawable(struct App* app);

/* Render skip's boot and exit halves (app_render.c): App_Init reads
 * TORIRS_RENDER_SKIP, App_Shutdown prints the frame counts and frees the
 * catch-up buffer. */
void
app_render_skip_init(struct App* app);

void
app_render_skip_shutdown(struct App* app);

void
app_draw_rebuild_loading_overlay(
    struct App* app,
    int* pixels,
    int width,
    int height);

void
app_draw_connection_lost_overlay(
    struct App* app,
    int* pixels,
    int width,
    int height);


/* ---- app_wev.c ---- */
/** TORIRS_WEV_DEBUG is set: the sailing traces print (read once). */
int
app_wev_debug_enabled(void);
int
app_wev_actor_root_fine(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int* out_fx,
    int* out_fz);

int
app_sailing_can_steer(struct App* app);

void
app_sailing_menu_context(
    struct App* app,
    struct RS_MinimenuBuildCtx* ctx,
    int mouse_x,
    int mouse_y);

int
app_sailing_send_heading(
    struct App* app,
    int heading);

int
app_wev_actor_root_frame(
    void* userdata,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int* io_fine_x,
    int* io_fine_z,
    int* out_frame_yaw);

int
app_wev_view_base(
    void* userdata,
    int view_id,
    int* out_base_tile_x,
    int* out_base_tile_z);

int
app_wev_terrain_height(
    void* userdata,
    int view_id,
    int world_x,
    int world_z,
    int level);

void
app_wev_register_pseudo_locs(
    void* userdata,
    struct World* world);

void
app_wev_bind_view_cameras(
    struct App* app,
    int pitch,
    int root_yaw,
    int root_cam_x,
    int root_cam_y,
    int root_cam_z);

void
app_wev_bind_frame_xforms(
    struct App* app,
    struct ToriRS_Frame* frame);

void
app_wev_order_parent_ground(struct App* app);

void
app_wev_deck_box(
    struct App* app,
    struct Wev const* wev,
    struct World const* parent_world,
    struct WevDeckBox* out_box);

int
app_wev_deck_level(
    struct App* app,
    int view_id);

void
app_wev_route_actors(struct App* app);

int
app_wev_claim_deck_actors(
    void* userdata,
    struct World* world,
    int* out_element_ids,
    int max);

void
app_wev_cycle_views(struct App* app);

void
app_wev_advance_bobs(struct App* app);

/* A hull's one-shot sequence (WORLDENTITY_INFO's seq): set from `seq_delay`
 * cycles away and wanted once, here, where it changes. */
void
app_wev_set_seq(
    struct App* app,
    struct Wev* wev,
    int seq_id,
    int seq_delay);


/* ---- app_world_apply.c ---- */
/* Attach `seq_id` to the loc standing on the tile's layer now (deob
 * Statics.method1508). Returns whether a loc stood there. */
int
app_world_scenery_anim_apply(
    struct App* app,
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id);

/* Rebuild the player's body when the one it wants (appearance + held-item
 * override) differs from the one its element holds and is wholly resident;
 * otherwise leave the element alone. Per frame, from
 * app_world_sync_entity_animations. */
void
app_world_reconcile_player_body(
    struct App* app,
    struct WorldEntity_Player* player);


/* ---- app_world_click.c ---- */
int
app_world_viewport_component_live(struct App const* app);

int
app_world_mouse_gate(
    struct App* app,
    int mouse_x,
    int mouse_y);

int
app_world_nearest_ground_tile(
    struct App* app,
    int click_x,
    int click_y,
    int* out_x,
    int* out_z,
    int* out_level);

int
app_try_move(
    struct App* app,
    int dst_x,
    int dst_z,
    int type,
    int click_x,
    int click_y,
    int yaw,
    int ctrl_held);

int
app_try_move_npc(
    struct App* app,
    struct WorldEntity_NPC const* npc,
    int ctrl_held);

int
app_try_move_player(
    struct App* app,
    struct WorldEntity_Player const* player,
    int ctrl_held);

int
app_try_move_loc(
    struct App* app,
    int element_id,
    int tile_x,
    int tile_z,
    int ctrl_held);

void
app_try_move_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int ctrl_held);

void
app_world_pick_finish(
    struct App* app,
    struct ToriRS_PickHits const* hits);


/* ---- app_world_edit.c ---- */
/** Queue an npc spawn on the packet runner; `args`' `id=` names it, and
 *  TORIRS_SPAWN_NPC overrides it (3106 when neither does). The spawn tool's
 *  key and the map editor's placement. */
void
app_world_spawn_npc(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);

/** Queue a ground item on the packet runner; `args`' `id=` names it, and
 *  TORIRS_SPAWN_OBJ overrides it (1265 when neither does). The spawn tool's
 *  key and the map editor's placement. */
void
app_world_spawn_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);




/* ---- app_world_frame.c ---- */
void
app_world_frame(
    struct App* app,
    int cycles,
    float frame_cycles);


/* ---- app_world_load.c ---- */
void
app_bind_configured_overlays(struct App* app);

int
app_minimap_level(
    struct App* app,
    struct WorldEntity_Player const* local);

void
app_world_map_poll(struct App* app);

void
app_world_load_begin(
    struct App* app,
    int const* chunks_xz,
    int chunk_pair_count);


/* ---- app_world_paint.c ---- */
void
app_world_paint(struct App* app);


/* ---- app_world_project.c ---- */
int
app_world_project_at(
    struct App* app,
    int fine_x,
    int fine_z,
    int world_y,
    int* out_x,
    int* out_y);

int
app_world_project(
    struct App* app,
    int fine_x,
    int fine_z,
    int height_above_ground,
    int* out_x,
    int* out_y);

int
app_world_project_actor(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int fine_x,
    int fine_z,
    int height_above_ground,
    int* out_x,
    int* out_y);

int
app_world_project_actor_footprint(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int fine_x,
    int fine_z,
    int footprint,
    int height_above_ground,
    int* out_x,
    int* out_y);

int
app_entity_model_height(
    struct App* app,
    int element_id);

int
app_actor_seq_vertical_offset(
    struct App* app,
    struct WorldEntityFacet_Animation const* animation);

int
app_entity_overlay_height(
    struct App* app,
    int element_id,
    int type_height);

/** Pose a just-mounted model to the frame its element already holds, so the
 *  overlay pass (which runs before the renderer) never measures a bind pose.
 *  Every SetModel on an animated entity element must be followed by this. */
void
app_element_pose_after_model_swap(
    struct App* app,
    int element_id);

bool
app_scene_sprite_size(
    struct App* app,
    int scene_id,
    int* out_w,
    int* out_h);


/* ---- app_world_query.c ---- */
struct WorldEntity_Player*
app_local_player(struct App* app);

int
app_world_local_plane(void* userdata);

int
app_world_height(
    void* userdata,
    int world_x,
    int world_z,
    int level);

int
app_worldview_id_of(
    struct App* app,
    const struct World* world);


/* ---- app_world_seq.c ---- */
int
app_world_scene_element_create(
    struct App* app,
    enum ToriDraw_ElementKind kind,
    struct ToriDraw_Model* model,
    int world_x,
    int world_y,
    int world_z);

void
app_world_apply_seq(
    struct App* app,
    int element_id,
    int seq_id);

void
app_world_apply_seq_at(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle);

void
app_world_load_seq(
    struct App* app,
    int seq_id);

void
app_seq_bind_pending_drop(
    struct App* app,
    int element_id);


/* ---- app_world_spawn.c ---- */
struct Task_AppSpawn*
app_spawn_task_new(
    struct App* app,
    enum AppSpawnKind kind,
    int tile_x,
    int tile_z,
    int level);

/* Add a spawn made by app_spawn_task_new (any kind but
 * APP_SPAWN_ENTITY_SPOTANIM) to the parallel queue: what it waits for is only
 * drawn, and nothing a packet or script reads waits behind it. */
void
app_spawn_task_add(
    struct App* app,
    struct Task_AppSpawn* task);

bool
app_npc_wants_zbuffer(
    int npc_id,
    struct ToriRS_Npctype const* npctype);

void
app_model_apply_import_render_flags(
    struct ToriDraw_Model* model,
    bool imported);

void
app_model_apply_rszbuf_two_pass(
    struct ToriDraw_Model* model,
    bool two_pass);

struct ToriDraw_Model*
app_world_build_model(
    struct App* app,
    const int* model_ids,
    int count,
    const struct AppModelRecolorSpec* recolors,
    int scale_xz,
    int scale_y,
    int light_actor,
    int light_contrast,
    int light_ambient);

int
app_world_npc_models_resident(
    struct App* app,
    struct ToriRS_Npctype const* npctype);

struct ToriDraw_Model*
app_world_build_npc_model(
    struct App* app,
    int npc_id,
    struct ToriRS_Npctype* npctype);

/* An npc's body under its server customisation `o` (active), cached under the
 * type and the override's serial. NULL while a model is not resident. */
/* IF_SETNPCHEAD_ACTIVE for an npc (pool index `world_idx`) that carries a
 * chathead customisation (head_custom.active, not a mirror): the head of its
 * drawn type under that override (deob class37.method627: the npc's
 * transformed type and method2919's override, built by class405.method9172),
 * composited by the head task from the override as it was when the packet
 * came; the override's serial keys it. */
void
App_SetInterfaceNpcHeadCustom(
    struct App* app,
    int component_id,
    int world_idx);

/* IF_SETNPCHEAD_ACTIVE for the npc at pool index `world_idx`: its head as the
 * npc stands now -- the local player's when its customisation mirrors, the
 * override's when it has one, else its type's -- and the head follows the npc
 * from then on (app_if_head_poll), as the reference resolves it every draw. */
void
App_SetInterfaceNpcHeadActive(
    struct App* app,
    int component_id,
    int world_idx);

/* The local player's body changed: every npc whose body customisation mirrors
 * the local player (0x10) is remounted from it. */
void
App_WorldNpcMirrorsRemount(struct App* app);

/* The first selectable kit of design part `design_part` (0..6) for body type
 * `body_type`, in config order, of the resident kit table (IdkType.method8644:
 * body type 1 is the female band); -1 for none. */
int
app_if_player_model_find_kit(
    struct App* app,
    int design_part,
    int body_type);

struct ToriDraw_Model*
app_world_build_npc_override_model(
    struct App* app,
    int npc_id,
    struct ToriRS_Npctype* npctype,
    struct WorldNpcOverride const* o);

struct ToriDraw_Model*
app_world_build_spotanim_model(
    struct App* app,
    const struct ToriRS_Spotanimtype* spot);

int
app_world_spawn_player_now(
    struct App* app,
    int tile_x,
    int tile_z,
    int level);

/* PLAYER_INFO's add: the player exists at once, drawing nothing until its
 * appearance's body is resident. */
int
app_world_spawn_synced_player_now(
    struct App* app,
    int tile_x,
    int tile_z,
    int level);

void
app_npc_entity_facts(
    struct App* app,
    int base_npc_id,
    struct ToriRS_Npctype const* drawn,
    struct ToriRS_NpcEntityFacts* out);

struct RS_EntityNpcType;

/* The shared rule's record of a resident type (App_WorldNpcTypeRecord without
 * the lookup): `facts` from app_npc_entity_facts for `npctype`. */
void
app_npc_type_record(
    struct ToriRS_NpcEntityFacts const* facts,
    struct ToriRS_Npctype const* npctype,
    struct RS_EntityNpcType* out);

int
app_world_spawn_npc_now(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int tile_x,
    int tile_z,
    int level);

/* app_world_spawn_npc_now, then App_WorldNpcSpawnAnnounce: an npc no server
 * names (the editor's previews, the hotkey). */
int
app_world_spawn_npc_announced(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int tile_x,
    int tile_z,
    int level);

/* ---- app_world_sync.c ---- */
int
app_world_sync_placement(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int element_id,
    int yaw,
    int actor_level,
    int lift);

void
app_world_sync_positions(struct App* app);

void
app_world_anim_frame_sound(
    void* userdata,
    int seq_id,
    int frame,
    int world_x,
    int world_z);

void
app_world_tick_animations(struct App* app);


/* ---- app_worldmap.c ---- */
int
app_mapfunction_scene_id(
    struct App* app,
    int element_id,
    struct ToriRS_MapElement** out_element);

int
app_worldmap_build_tiles(
    struct App* app,
    struct UITreeHostRequest* req);

int
app_worldmap_build_overview(
    struct App* app,
    struct UITreeHostRequest* req);

int
app_worldmap_surface_live(struct App* app);

void
app_worldmap_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed);

#endif
