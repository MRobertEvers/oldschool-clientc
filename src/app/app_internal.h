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
#include "graphics/convex_hull.h"
#include "torirsmaped/torirs_maped.h"

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
#include "engine/async_pending.h"
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
#include "net/rev/gameproto_parse.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "painters/painters.h"
#include "painters/painters_cull_project.h"
#include "painters/scene_occluders.h"
#include "perf/torirs_perf.h"
#include "platform/platform_memory.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
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
struct AppEntitySpotanim;
struct AppModelRecolorSpec;
struct AppPluginAssetModel;
struct AppPluginObject;
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
struct ToriRS_PluginMesh;
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
     * The async pipeline's runaway tripwire. NOT a frame budget.
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
            uint8_t _nsbuf[512];                                                                   \
            int _nslen = builder_call;                                                             \
            if( _nslen > 0 )                                                                       \
            {                                                                                      \
                ToriRS_Network_SendRaw((app)->net, _nsbuf, _nslen);                                \
                (app)->net_last_send_ms = (app)->last_frame_ms;                                    \
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
 * `OSRS-Content/osrs239-content/pack/8_sprites.pack` line 441, where it is the
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
};


enum AppSpawnKind
{
    APP_SPAWN_PLAYER = 0,
    APP_SPAWN_NPC,
    APP_SPAWN_PROJECTILE,
    APP_SPAWN_PROJECTILE_SPOT,
    APP_SPAWN_OBJ,
    APP_SPAWN_SPOTANIM,
    APP_SPAWN_ENTITY_SPOTANIM,
    APP_SPAWN_LOC_CHANGE,
    APP_SPAWN_LOC_ANIM,
    APP_SPAWN_PLUGIN_OBJECT,
};


struct Task_AppSpawn
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    /* The worldview cursor AS OF ENQUEUE (app->active_world). Spawn tasks run
     * at the END of the exec FIFO, behind later packets — including the
     * SERVER_TICK_END that resets the live cursor — so the apply cannot read
     * app->active_world when it finally runs; it must carry its own copy. The
     * view can legitimately die while the task is parked (a boat despawns
     * mid-flight), so applies guard on WorldviewRegistry_IsLive rather than
     * asserting. */
    int view;
    enum AppSpawnKind kind;
    int tile_x;
    int tile_z;
    int level;
    int npc_id;
    int obj_id;
    int model_id;
    int seq_id;
    int src_tile_x;
    int src_tile_z;
    int src_level;
    int model_i;
    int spotanim_id;
    int spotanim_height;
    int spotanim_delay;
    /* APP_SPAWN_ENTITY_SPOTANIM: the body scene element of the player/npc the
     * attached graphic belongs to (stable, scene-unique key to re-find the live
     * entity when the async load lands). */
    int entity_element_id;
    /* APP_SPAWN_LOC_CHANGE: the placement's own right-click menu, carried
     * across the async model wait because the scenery entity it lands on does
     * not exist until then. `loc_op_flags` is the 5-bit shown mask and
     * `loc_ops[i]` the replacement label for slot i ("" = keep the loctype's).
     * See App_WorldLocChangeOps. */
    int loc_op_flags;
    char loc_ops[5][32];
    /* MAP_PROJANIM (spotanim-based projectile) trajectory params. Source and
     * destination tiles reuse src_tile_x/z and tile_x/z; src_level and level
     * carry the source and destination levels. */
    int proj_src_height;
    int proj_dst_height;
    int proj_start_delay;
    int proj_end_delay;
    int proj_peak;
    int proj_arc;
    int proj_target;
    /* APP_SPAWN_LOC_CHANGE (zone LOC_ADD_CHANGE / LOC_DEL): the replacement loc
     * (-1 = pure delete), its map shape/angle, and the nested model-list cursor
     * (loc models are [shape_entry][model]; both indices must survive awaits). */
    int loc_id;
    int loc_shape;
    int loc_angle;
    int loc_model_j;
    int loc_resolved_id;
    int loc_resolve_depth;
    int loc_base_seq;
    /** APP_SPAWN_LOC_CHANGE: the loc's models and sequence, fanned out and not yet ended. */
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

void
app_world_camera_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out);

int
app_world_camera_zooms(struct App const* app);

void
app_world_camera_mouse(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out);

int
app_cinema_level(struct App* app);

void
app_world_camera_cinema(struct App* app);

void
app_world_camera_follow(struct App* app);


/* ---- app_canvas_layout.c ---- */
int
app_ui_scaled_axis(
    struct App const* app,
    int window_px);

int
app_wants_text_input(struct App const* app);


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
    int* out_font_id);

int
app_iface_text_input_focused(struct App const* app);

int
app_text_input_focused(struct App const* app);

int
app_chrome_holds_keyboard(struct App const* app);

int
app_chat_focus_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed,
    int* out_submit);

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


/* ---- app_client_trigger.c ---- */
void
app_client_trigger_loc(
    struct App* app,
    struct WorldEntity_Scenery* loc,
    int trigger);

void
app_client_triggers_world_loaded(struct App* app);

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


/* ---- app_cs2_scene.c ---- */
int
app_cs2_loc_at_coord(
    void* user,
    int coord,
    int loc_type,
    int* out_layer,
    char* out_name,
    int name_cap);

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

void
app_world_apply_entity_anim_tracks(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Animation const* anim,
    struct WorldEntityFacet_IdleAnimations const* idle);

void
app_world_sync_entity_animations(struct App* app);

struct AppEntitySpotanim*
app_entity_spotanim_find(
    struct App* app,
    int body_element_id,
    int owner_entity_id);

void
app_entity_spotanim_drop(
    struct App* app,
    int body_element_id);

void
app_world_sync_entity_spotanims(struct App* app);


/* ---- app_host_request.c ---- */
int
app_host_request(
    void* user,
    struct UITreeHostRequest* req);


/* ---- app_hotkeys.c ---- */
int
app_debug_key_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target);

int
app_debug_key_held(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target);

void
app_ui_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input);

void
app_world_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out);


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


/* ---- app_if_models.c ---- */
void
app_if_head_poll(struct App* app);

void
app_if_player_model_poll(struct App* app);

void
app_player_model_poll(struct App* app);


/* ---- app_loc_editor.c ---- */
void
app_loc_editor_select_element(
    struct App* app,
    int element_id);

void
app_loc_editor_select_terrain(
    struct App* app,
    int scene_x,
    int scene_z,
    int cache_level);

int
app_chrome_wants_pointer(
    struct App const* app,
    int x,
    int y);

void
app_chrome_route_input(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input);

void
app_chrome_route_keys(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input);

void
app_loc_editor_tick(
    struct App* app,
    struct LibToriRS_Input* input);


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
bool
app_mapedit_select_active(struct App const* app);

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

void
app_map_editor_world_click(
    struct App* app,
    struct LibToriRS_Input* input);

void
app_map_editor_ghost_update(struct App* app);

void
app_map_editor_preview_update(struct App* app);


/* ---- app_minimap.c ---- */
int
app_minimap_click(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int ctrl_held);


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

void
app_minimenu_ctx_ground_fallback(
    struct App* app,
    struct RS_MinimenuBuildCtx* mctx,
    int click_x,
    int click_y);

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

int
app_minimenu_run_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y);

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
 * empties it and the per-frame placement against the painter buffer.
 * @see app_world_tile_marks_place. */
int
app_world_tile_mark_key(
    int scene_x,
    int scene_z,
    int level);
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

void
app_overlay_build_chat(
    struct App* app,
    int element_id,
    struct WorldEntityFacet_Chat const* chat,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int font_id);

void
app_overlay_build_player_headicons(
    struct App* app,
    int element_id,
    int headicons,
    struct WorldEntityFacet_DrawPosition const* draw_position,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int actor_level,
    int headicons_scene);

void
app_overlay_build_hint_arrow(struct App* app);

void
app_overlay_build_npc_headicon(
    struct App* app,
    int element_id,
    struct ToriRS_Npctype const* npctype,
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

void
app_overlay_build_hover_footprint(struct App* app);

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
app_plugin_screenshot(
    void* user,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size);


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

struct AppPluginObject*
app_plugin_object_at(
    struct App* app,
    int handle);

int
app_plugin_object_model_id(
    struct App* app,
    struct AppPluginObject const* obj);

int
app_plugin_object_seq_id(
    struct App* app,
    struct AppPluginObject const* obj);

void
app_plugin_object_materialize_now(
    struct App* app,
    int handle);


/* ---- app_plugin_panel.c ---- */
int
app_plugin_io_down(struct App const* app);

void
app_plugin_button_sync(struct App* app);

void
app_plugin_window_set_open(struct App* app, int open);

int
app_plugin_button_click(struct App* app, int component_id);

int
app_plugin_panel_overlay_visible(
    struct App const* app,
    int index,
    struct UITreeEntityOverlay* out);

void
app_plugin_panel_tick(struct App* app, struct LibToriRS_Input* input);


/* ---- app_textures.c ---- */
int
app_tex_trace_enabled(void);

void
app_sync_textures(struct App* app);

void
app_sync_textures_poll(struct App* app);


/* ---- app_tick.c ---- */
int
app_logic_tick(struct App* app);


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

struct RS_PreloadStep const*
app_preload_announce(
    struct App* app,
    char const* step_name);

void
app_title_progress(
    struct App* app,
    int percent,
    char const* string_key);

int
app_title_field_line(
    struct App* app,
    struct UITreeHostRequest* req);

void
app_title_submit(struct App* app);

int
app_title_tick(struct App* app);


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
app_inv_icon_reconcile_tick(struct App* app);

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

void
app_varp_server_update(
    void* userdata,
    int varp_id);


/* ---- app_viewport.c ---- */
void
app_update_world_viewport(struct App* app);

int
app_world_drawable(struct App* app);

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


/* ---- app_world_apply.c ---- */
void
app_world_scenery_anim_apply(
    struct App* app,
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id);

void
app_set_player_element_model(
    struct App* app,
    int element_id,
    int const slots[12],
    int const colors[5],
    int gender);


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
void
app_world_spawn_player(
    struct App* app,
    int tile_x,
    int tile_z,
    int level);

void
app_world_spawn_npc(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);

void
app_world_spawn_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);

void
app_loc_change_apply_cb(
    void* user,
    int level,
    int x,
    int z,
    int loc_id,
    int shape,
    int angle);

void
app_world_spawn_spotanim(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);

void
app_world_spawn_projectile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);

void
app_world_damage_test(struct App* app);

void
app_world_entity_spotanim_test(
    struct App* app,
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
app_entity_model_height(
    struct App* app,
    int element_id);

int
app_entity_overlay_height(
    struct App* app,
    int element_id,
    int type_height);

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
app_seq_bind_pending_drop(
    struct App* app,
    int element_id);

void
app_world_bind_pending_seqs(struct App* app);


/* ---- app_world_spawn.c ---- */
struct Task_AppSpawn*
app_spawn_task_new(
    struct App* app,
    enum AppSpawnKind kind,
    int tile_x,
    int tile_z,
    int level);

bool
app_npc_wants_zbuffer(
    int npc_id,
    struct ToriRS_Npctype const* npctype);

void
app_model_apply_import_render_flags(
    struct ToriDraw_Model* model,
    bool imported);

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

struct ToriDraw_Model*
app_world_build_npc_model(
    struct App* app,
    int npc_id,
    struct ToriRS_Npctype* npctype);

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

void
app_npc_entity_facts(
    struct App* app,
    int base_npc_id,
    struct ToriRS_Npctype const* drawn,
    struct ToriRS_NpcEntityFacts* out);

int
app_world_spawn_npc_now(
    struct App* app,
    int npc_id,
    int base_npc_id,
    int tile_x,
    int tile_z,
    int level);

void
app_world_spawn_spotanim_now(
    struct App* app,
    struct World* world,
    int spotanim_id,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int delay);

struct AppPluginAssetModel*
app_plugin_asset_model_at(
    struct App* app,
    int handle);

struct ToriRS_PluginMesh*
app_plugin_mesh_at(
    struct App* app,
    int handle);


/* ---- app_world_sync.c ---- */
int
app_world_sync_placement(
    struct App* app,
    struct WorldEntityFacet_ViewPlacement const* placement,
    int element_id,
    int yaw,
    int actor_level);

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
