#ifndef RUNESCAPE_H
#define RUNESCAPE_H

#include "../input/libtorirs_input.h"
#include "../render/libtorirs_render.h"
#include "../scripting/libtorirs_scripting.h"
#include "../world/world.h"
#include "osrs/painters.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriAuxLibCore;
struct ToriDraw_Scene;
struct ToriAuxLibTD;
struct LibToriRS_IOContext;

/* OSRS rebuild-normal zone coords: map_chunk * 8 (zone 400 -> map chunk 50). */
#define RUNESCAPE_MAP_CHUNK_CENTER 50
#define RUNESCAPE_ZONE_CENTER_X (RUNESCAPE_MAP_CHUNK_CENTER * 8)
#define RUNESCAPE_ZONE_CENTER_Z (RUNESCAPE_MAP_CHUNK_CENTER * 8)

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

enum GameRunescape_FramePhase
{
    RS_FRAME_PHASE_GC_EVENTS = 0,
    RS_FRAME_PHASE_BEGIN_3D,
    RS_FRAME_PHASE_MODELS,
    RS_FRAME_PHASE_END_3D,
    RS_FRAME_PHASE_DONE,
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

    int zone_center_x;
    int zone_center_z;
    bool world_built;

    struct
    {
        enum GameRunescape_FramePhase phase;
        int event_index;
        int element_index;
        int painter_command_index;
        bool world_emitted;
        bool painter_paint_done;
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
game_runescape_build_world(struct GameRunescape* game);

void
game_runescape_process_input(
    struct GameRunescape* game,
    struct LibToriRS_Input* input);

void
game_runescape_frame_begin(struct GameRunescape* game, int cycles_elapsed);

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
