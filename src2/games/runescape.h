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
#include "ui/uitree.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriAuxLibCore;
struct ToriDraw_Scene;
struct ToriAuxLibTD;
struct LibToriRS_IOContext;

/* OSRS rebuild-normal zone coords (zonex, zonez). */
// Waterfall
// #define RUNESCAPE_ZONE_CENTER_X 313
// #define RUNESCAPE_ZONE_CENTER_Z 437
// Inferno
#define RUNESCAPE_ZONE_CENTER_X 280
#define RUNESCAPE_ZONE_CENTER_Z 664

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

enum GameRunescape_EntityKind
{
    RS_ENTITY_KIND_NONE = 0,
    RS_ENTITY_KIND_PLAYER = 1,
    RS_ENTITY_KIND_PROJECTILE = 2,
    RS_ENTITY_KIND_NPC = 3,
};

#define RS_ENTITY_KIND_SHIFT 28
#define RS_ENTITY_KIND_MASK 0xF
#define RS_ENTITY_INDEX_MASK ((1 << RS_ENTITY_KIND_SHIFT) - 1)
#define RS_ENTITY_ID(kind, index)                                                                  \
    (((int)(kind) << RS_ENTITY_KIND_SHIFT) | ((index) & RS_ENTITY_INDEX_MASK))
#define RS_ENTITY_KIND_OF(id) (((id) >> RS_ENTITY_KIND_SHIFT) & RS_ENTITY_KIND_MASK)
#define RS_ENTITY_INDEX_OF(id) ((id) & RS_ENTITY_INDEX_MASK)

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
#define RUNESCAPE_ENTITY_REGISTRY_INITIAL_CAP 32

struct GameRunescape_UITraversalFrame
{
    int32_t parent_index;
};

struct GameRunescape_EntityRecord
{
    int entity_id;
    int element_id;
    int world_index;
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
    struct ToriDraw_ViewPort world_view_port;
    struct PaintersBuffer* painter_buffer;
    struct UITree* ui_tree;
    bool ui_tree_ready;
    struct ToriAuxLibVM* vm;
    int32_t ui_hovered_node;
    char ui_text_scratch[RUNESCAPE_UI_TEXT_SCRATCH_MAX];

    int zone_center_x;
    int zone_center_z;
    bool world_built;
    int world_map_scene_id;
    int world_map_w;
    int world_map_h;

    int mouse_x;
    int mouse_y;
    bool mouse_in_viewport;
    struct WorldPickSet pickset;
    int last_tile_sx;
    int last_tile_sz;
    int last_tile_level;
    bool last_tile_valid;

    struct GameRunescape_EntityRecord* entity_registry;
    int entity_registry_count;
    int entity_registry_cap;
    int next_projectile_entity_index;
    int next_player_entity_index;
    int next_npc_entity_index;

    struct
    {
        enum GameRunescape_FramePhase phase;
        int event_index;
        int element_index;
        int painter_command_index;
        bool world_emitted;
        bool painter_paint_done;
        bool ui_2d_begun;
        int32_t ui_current;
        struct GameRunescape_UITraversalFrame ui_stack[RUNESCAPE_UI_TRAVERSAL_STACK_MAX];
        int ui_stack_top;
    } frame;
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

int32_t
GameRunescape_UIHitTest(
    struct GameRunescape* game,
    int px,
    int py);

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
