#ifndef RUNESCAPE_H
#define RUNESCAPE_H

#include "../input/libtorirs_input.h"
#include "../render/libtorirs_render.h"
#include "../scripting/libtorirs_scripting.h"
#include "../world/world.h"
#include "../world/world_pickset.h"
#include "osrs/painters.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_types.h"
#include "ui/ui_input.h"
#include "ui/ui_scroll.h"
#include "ui/ui_scroll_runtime.h"
#include "ui/ui_cross_cursor.h"
#include "ui/ui_hover_routing.h"
#include "ui/ui_inv_selection.h"
#include "ui/chat_state.h"
#include "ui/interaction_state.h"
#include "ui/ui_minimenu.h"
#include "ui/uitree.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "ui/ui_inv_data_service.h"
#include "vm/cs2vm.h"
#include "world/entity_registry.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriAuxLibCore;
struct ToriDraw_Scene;
struct ToriAuxLibTD;
struct LibToriRS_IOContext;
struct CS2VM;
struct CS2Host;

/** Click cross overlay (walk/interact cursor). Prefer UI_CROSS_MODE_* from ui_cross_cursor.h. */
#define RUNESCAPE_CROSS_MODE_OFF UI_CROSS_MODE_OFF
#define RUNESCAPE_CROSS_MODE_WALK UI_CROSS_MODE_WALK
#define RUNESCAPE_CROSS_MODE_INTERACT UI_CROSS_MODE_INTERACT

/* OSRS rebuild-normal zone coords (zonex, zonez). */
// Waterfall
#define RUNESCAPE_ZONE_CENTER_X 313
#define RUNESCAPE_ZONE_CENTER_Z 437
// Inferno
// #define RUNESCAPE_ZONE_CENTER_X 280
// #define RUNESCAPE_ZONE_CENTER_Z 664

#define RUNESCAPE_PROJECTILE_MODEL_ID 3081
#define RUNESCAPE_PROJECTILE_SEQ_ID 659

#include "../runescape/appearance.h"

/* Magic / Fire Strike preset from docs/projectiles.md */
#define RUNESCAPE_PROJECTILE_STARTHEIGHT 43
#define RUNESCAPE_PROJECTILE_ENDHEIGHT 31
#define RUNESCAPE_PROJECTILE_DELAY 51
#define RUNESCAPE_PROJECTILE_ANGLE 16
#define RUNESCAPE_PROJECTILE_LENGTH -5
#define RUNESCAPE_PROJECTILE_OFFSET 64
#define RUNESCAPE_PROJECTILE_STEP 10

#define RUNESCAPE_EXAMPLE_NPC_ID_DAT1 12
// Zuk 7706
// Whisperer 12204
// #define RUNESCAPE_EXAMPLE_NPC_ID_DAT2 7706
#define RUNESCAPE_EXAMPLE_NPC_ID_DAT2 12204

/** Reserved ToriDraw sprite element id for the baked world-map minimap (revconfig ids start at 1).
 */
#define RUNESCAPE_WORLD_MAP_SCENE_ID 0

enum GameRunescape_FramePhase
{
    RS_FRAME_PHASE_GC_EVENTS = 0,
    RS_FRAME_PHASE_BEGIN_3D,
    RS_FRAME_PHASE_MODELS,
    RS_FRAME_PHASE_END_3D,
    RS_FRAME_PHASE_UI_2D_BEGIN,
    RS_FRAME_PHASE_UI_2D,
    RS_FRAME_PHASE_UI_2D_END,
    RS_FRAME_PHASE_DONE,
};

#define RUNESCAPE_UI_TRAVERSAL_STACK_MAX 64
#define RUNESCAPE_UI_TEXT_SCRATCH_MAX 512
#define RUNESCAPE_ENTITY_REGISTRY_INITIAL_CAP ENTITY_REGISTRY_INITIAL_CAP
#define RUNESCAPE_CHAT_LINE_MAX CHAT_STATE_LINE_MAX
#define RUNESCAPE_FRIEND_MAX CHAT_STATE_FRIEND_MAX

typedef struct ChatLine GameRunescape_ChatLine;
typedef struct EntityRecord GameRunescape_EntityRecord;
typedef enum EntityKind GameRunescape_EntityKind;

struct GameRunescape_UITraversalFrame
{
    int32_t parent_index;
};

/** Zone center, build flag, and baked minimap sprite dimensions. */
struct GameRunescape_WorldMapState
{
    /** OSRS zone X coordinate used when building the local scene. */
    int zone_center_x;
    /** OSRS zone Z coordinate used when building the local scene. */
    int zone_center_z;
    /** True after BuildWorldCenterzone / BuildWorldChunkList succeeds. */
    bool world_built;
    /** ToriDraw scene sprite id for the baked world-map minimap texture. */
    int world_map_scene_id;
    int world_map_w;
    int world_map_h;
};

/** Mouse position, world viewport pickset, and last ground tile under cursor. */
struct GameRunescape_WorldPickState
{
    int mouse_x;
    int mouse_y;
    /** True when mouse is inside world_view_port clip rect. */
    bool mouse_in_viewport;
    struct WorldPickSet pickset;
    int last_tile_sx;
    int last_tile_sz;
    int last_tile_level;
    bool last_tile_valid;
};

#define RS_UI_VARP_CHANGE_MAX 64

/** Per-frame render traversal state (phase machine for GameRunescape_FrameNextCommand). */
struct GameRunescape_FrameState
{
    enum GameRunescape_FramePhase phase;
    int event_index;
    int element_index;
    int painter_command_index;
    bool world_emitted;
    bool painter_paint_done;
    bool ui_2d_begun;
    /** Current UITree node index during UI_2D traversal (-1 = none). */
    int32_t ui_current;
    /** Inventory slot index within the current inv-grid draw step. */
    int ui_inv_slot;
    int ui_minimenu_step;
    int ui_chat_button_step;
    int ui_scrollbar_step;
    /** Multi-step UIELEM_RS_MODEL chat-head draw (END_2D/BEGIN_3D/DRAW/END_3D/BEGIN_2D). */
    int ui_model_step;
    int32_t ui_scrollbar_layer;
    /** Client.ts scrollCycle: frames LMB held this tick (for arrow repeat). */
    int ui_scroll_cycle;
    struct GameRunescape_UITraversalFrame ui_stack[RUNESCAPE_UI_TRAVERSAL_STACK_MAX];
    int ui_stack_top;
};

struct GameRunescape
{
    struct LibToriRS_ScriptQueue* script_queue;
    struct ToriAuxLibCore* core;
    struct World* world;
    struct ToriDraw_Scene* scene;
    struct ToriAuxLibTD* td;
    struct ToriDraw_Position* camera_position;
    struct ToriDraw_Camera* camera;
    struct ToriDraw_ViewPort* view_port;
    /** Clip rect for the 3D world viewport (main game area, not full UI canvas). */
    struct ToriDraw_ViewPort world_view_port;
    struct PaintersBuffer* painter_buffer;
    /** Baked static UI component tree (revconfig / interface load). */
    struct UITree* ui_tree;
    /** True once revconfig UI tree is loaded and safe to traverse. */
    bool ui_tree_ready;
    /** True after SyncUISpritesFromScene has wired scene sprite ids into the tree. */
    bool ui_sprites_synced;
    /** True after scene fonts are registered for UI text draw. */
    bool ui_fonts_synced;
    /** Bake-time inventory pool (legacy seed data for inv_data). */
    struct UIInventoryPool* ui_inv_pool;
    struct UIInvDataService inv_data;
    /** UITreeHost callbacks (CS2 active state, selected tab, inv slot lookup). */
    struct UITreeHost ui_host;
    struct UIInputState ui_input;
    /** Active sidebar tab index (Client.ts selectedTab). */
    int selected_tab;
    struct ToriAuxLibVM* vm;
    struct CS2VM* cs2vm;
    struct CS2Host cs2host;
    struct UIHoverRouting ui_hover;
    struct UIScrollRuntime ui_scroll;
    /** Scratch buffer for per-frame UI text emission (avoids stack large strings). */
    char ui_text_scratch[RUNESCAPE_UI_TEXT_SCRATCH_MAX];

    struct ChatState chat;

    /** Current committed interaction target (walk, use, etc.). */
    struct InteractionState interaction;
    struct UIMinimenuState minimenu;
    /** Staging interaction copied from a minimenu pick before commit. */
    struct InteractionState click_target;

    struct UICrossCursorState cross;

    struct UIInvSelection inv_selection;

    struct GameRunescape_WorldMapState world_map;
    struct GameRunescape_WorldPickState world_pick;

    struct EntityRegistry entities;

    int varp_change_ids[RS_UI_VARP_CHANGE_MAX];
    int varp_change_count;

    struct GameRunescape_FrameState frame;
};

struct GameRunescape*
GameRunescape_New(
    struct LibToriRS_ScriptQueue* script_queue,
    struct ToriDraw_Scene* scene);

void
GameRunescape_Free(struct GameRunescape* game);

void
GameRunescape_SetCore(
    struct GameRunescape* game,
    struct ToriAuxLibCore* core);

void
GameRunescape_SetTD(
    struct GameRunescape* game,
    struct ToriAuxLibTD* td);

void
GameRunescape_SetVM(
    struct GameRunescape* game,
    struct ToriAuxLibVM* vm);

void
GameRunescape_SetUITree(
    struct GameRunescape* game,
    struct UITree* ui_tree);

void
GameRunescape_SetUIInvPool(
    struct GameRunescape* game,
    struct UIInventoryPool* pool);

void
GameRunescape_IF3InvSetContainerSlot(
    struct GameRunescape* game,
    int container_id,
    int slot,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index);

void
GameRunescape_IF3InvApplyFull(
    struct GameRunescape* game,
    int container_id,
    int const* obj_ids,
    int const* obj_counts,
    int count);

void
GameRunescape_IF3InvApplyPartial(
    struct GameRunescape* game,
    int container_id,
    int const* slots,
    int const* obj_ids,
    int const* obj_counts,
    int count);

/** Fire onInvTransmit hooks for components watching container_id. */
void
GameRunescape_DispatchInvTransmit(
    struct GameRunescape* game,
    int container_id);

void
GameRunescape_SyncUISpritesFromScene(struct GameRunescape* game);

void
GameRunescape_SetUITreeReady(
    struct GameRunescape* game,
    bool ready);

void
GameRunescape_BuildWorldCenterzone(
    struct GameRunescape* game,
    int centerzone_x,
    int centerzone_z,
    int scene_size);

void
GameRunescape_BuildWorldChunkList(
    struct GameRunescape* game,
    int* chunks_xz,
    int count);

void
GameRunescape_RebuildWorldMap(struct GameRunescape* game);

void
GameRunescape_ProcessInput(
    struct GameRunescape* game,
    struct LibToriRS_Input* input);

void
GameRunescape_UpdateWorldViewport(struct GameRunescape* game);

void
GameRunescape_RefreshPicksetAtMouse(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y);

int32_t
GameRunescape_UIHitTest(
    struct GameRunescape* game,
    int px,
    int py);

bool
GameRunescape_PointInMainHoverRegion(
    struct GameRunescape const* game,
    int px,
    int py);

bool
GameRunescape_PointInSidebarHoverRegion(
    int px,
    int py);

bool
GameRunescape_PointInChatHoverRegion(
    int px,
    int py);

int32_t
GameRunescape_UISelectedSidebarIndex(struct GameRunescape const* game);

int32_t
GameRunescape_UITreeIndexForComponentId(
    struct GameRunescape const* game,
    int component_id);

void
GameRunescape_GetScrollPos(
    struct GameRunescape const* game,
    int component_id,
    int* sx,
    int* sy);

void
GameRunescape_ClampScroll(
    struct GameRunescape* game,
    struct StaticUIComponent const* layer);

bool
GameRunescape_MinimenuPrepareShow(
    struct GameRunescape* game,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width);

void
GameRunescape_ApplyChatFilterSettings(
    struct GameRunescape* game,
    int public_mode,
    int private_mode,
    int trade_mode);

void
GameRunescape_SendChatSetMode(struct GameRunescape* game);

void
GameRunescape_FrameBegin(
    struct GameRunescape* game,
    int cycles_elapsed);

bool
GameRunescape_FrameNextCommand(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command);

void
GameRunescape_FrameEnd(struct GameRunescape* game);

int
GameRunescape_CameraTerrainLevel(const struct GameRunescape* game);

int
GameRunescape_WorldEntityAddPlayer(
    struct GameRunescape* game,
    int entity_id,
    const int appearance[12],
    int x,
    int z,
    int level,
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l);

int
GameRunescape_WorldEntityAddNPC(
    struct GameRunescape* game,
    int entity_id,
    int npc_id,
    int x,
    int z,
    int level);

bool
GameRunescape_WorldEntityAnimate(
    struct GameRunescape* game,
    int entity_id,
    int anim_id,
    int primary_secondary);

int
GameRunescape_WorldEntityAddProjectile(
    struct GameRunescape* game,
    int entity_id,
    int projectile_id,
    int anim_id,
    int src_sx,
    int src_sz,
    int dst_sx,
    int dst_sz,
    int level,
    int startheight,
    int endheight,
    int delay,
    int angle,
    int length,
    int offset,
    int step);

struct Task_GameRunescape_WorldEntityAddPlayer;

struct Task_GameRunescape_WorldEntityAddPlayer*
Task_GameRunescape_WorldEntityAddPlayer_New(
    struct GameRunescape* game,
    int entity_id,
    const int appearance[12],
    int x,
    int z,
    int level,
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l);

void
Task_GameRunescape_WorldEntityAddPlayer_Free(struct Task_GameRunescape_WorldEntityAddPlayer* task);

int
Task_GameRunescape_WorldEntityAddPlayer_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_GameRunescape_WorldEntityAddNPC;

struct Task_GameRunescape_WorldEntityAddNPC*
Task_GameRunescape_WorldEntityAddNPC_New(
    struct GameRunescape* game,
    int entity_id,
    int npc_id,
    int x,
    int z,
    int level);

void
Task_GameRunescape_WorldEntityAddNPC_Free(struct Task_GameRunescape_WorldEntityAddNPC* task);

int
Task_GameRunescape_WorldEntityAddNPC_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_GameRunescape_WorldEntityAnimate;

struct Task_GameRunescape_WorldEntityAnimate*
Task_GameRunescape_WorldEntityAnimate_New(
    struct GameRunescape* game,
    int entity_id,
    int anim_id,
    int primary_secondary);

void
Task_GameRunescape_WorldEntityAnimate_Free(struct Task_GameRunescape_WorldEntityAnimate* task);

int
Task_GameRunescape_WorldEntityAnimate_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

struct Task_GameRunescape_WorldEntityAddProjectile;

struct Task_GameRunescape_WorldEntityAddProjectile*
Task_GameRunescape_WorldEntityAddProjectile_New(
    struct GameRunescape* game,
    int entity_id,
    int projectile_id,
    int anim_id,
    int src_sx,
    int src_sz,
    int dst_sx,
    int dst_sz,
    int level,
    int startheight,
    int endheight,
    int delay,
    int angle,
    int length,
    int offset,
    int step);

void
Task_GameRunescape_WorldEntityAddProjectile_Free(
    struct Task_GameRunescape_WorldEntityAddProjectile* task);

int
Task_GameRunescape_WorldEntityAddProjectile_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
