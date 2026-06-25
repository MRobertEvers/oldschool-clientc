#ifndef RUNESCAPE_H
#define RUNESCAPE_H

#include "../input/libtorirs_input.h"
#include "../render/libtorirs_render.h"
#include "../scripting/libtorirs_scripting.h"
#include "../world/world.h"
#include "../world/world_pickset.h"
#include "osrs/painters.h"
#include "ui/uitree.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriAuxLibCore;
struct ToriDraw_Scene;
struct ToriAuxLibTD;
struct LibToriRS_IOContext;

/* OSRS rebuild-normal zone coords (zonex, zonez). */
#define RUNESCAPE_ZONE_CENTER_X 313
#define RUNESCAPE_ZONE_CENTER_Z 437

#define RUNESCAPE_PROJECTILE_MODEL_ID 3081
#define RUNESCAPE_PROJECTILE_SEQ_ID 659

/* Magic / Fire Strike preset from docs/projectiles.md */
#define RUNESCAPE_PROJECTILE_STARTHEIGHT 43
#define RUNESCAPE_PROJECTILE_ENDHEIGHT 31
#define RUNESCAPE_PROJECTILE_DELAY 51
#define RUNESCAPE_PROJECTILE_ANGLE 16
#define RUNESCAPE_PROJECTILE_LENGTH -5
#define RUNESCAPE_PROJECTILE_OFFSET 64
#define RUNESCAPE_PROJECTILE_STEP 10

/** Reserved ToriDraw sprite element id for the baked world-map minimap (revconfig ids start at 1). */
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

struct GameRunescape_UITraversalFrame
{
    int32_t parent_index;
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
game_runescape_new(
    struct LibToriRS_ScriptQueue* script_queue,
    struct ToriDraw_Scene* scene);

void
game_runescape_free(struct GameRunescape* game);

void
game_runescape_set_core(
    struct GameRunescape* game,
    struct ToriAuxLibCore* core);

void
game_runescape_set_td(
    struct GameRunescape* game,
    struct ToriAuxLibTD* td);

void
game_runescape_set_vm(
    struct GameRunescape* game,
    struct ToriAuxLibVM* vm);

void
game_runescape_set_ui_tree(
    struct GameRunescape* game,
    struct UITree* ui_tree);

void
game_runescape_set_ui_tree_ready(
    struct GameRunescape* game,
    bool ready);

void
game_runescape_build_world(struct GameRunescape* game);

void
game_runescape_rebuild_world_map(struct GameRunescape* game);

void
game_runescape_process_input(
    struct GameRunescape* game,
    struct LibToriRS_Input* input);

int32_t
game_runescape_ui_hit_test(
    struct GameRunescape* game,
    int px,
    int py);

void
game_runescape_frame_begin(
    struct GameRunescape* game,
    int cycles_elapsed);

bool
game_runescape_frame_next_command(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command);

void
game_runescape_frame_end(struct GameRunescape* game);

int
game_runescape_camera_terrain_level(const struct GameRunescape* game);

int
game_runescape_spawn_projectile(
    struct GameRunescape* game,
    int model_id,
    int seq_id,
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

struct ToriRunescape_Task_AddProjectile;

struct ToriRunescape_Task_AddProjectile*
ToriRunescape_Task_AddProjectile_New(
    struct GameRunescape* game,
    int model_id,
    int seq_id,
    int src_sx,
    int src_sz,
    int dst_sx,
    int dst_sz,
    int level);

void
ToriRunescape_Task_AddProjectile_Free(struct ToriRunescape_Task_AddProjectile* task);

int
ToriRunescape_Task_AddProjectile_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
