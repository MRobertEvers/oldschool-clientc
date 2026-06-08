#ifndef RUNESCAPE_H
#define RUNESCAPE_H

#include "../input/libtorirs_input.h"
#include "../render/libtorirs_render.h"
#include "../scripting/libtorirs_scripting.h"
#include "../world/world.h"
#include "../world/world_scene.h"
#include "osrs/painters.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

struct GameCache;
struct ToriDraw_Context;

/* OSRS rebuild-normal zone coords: map_chunk * 8 (zone 400 -> map chunk 50). */
#define RUNESCAPE_MAP_CHUNK_CENTER 50
#define RUNESCAPE_ZONE_CENTER_X (RUNESCAPE_MAP_CHUNK_CENTER * 8)
#define RUNESCAPE_ZONE_CENTER_Z (RUNESCAPE_MAP_CHUNK_CENTER * 8)

enum GameRunescape_FramePhase
{
    RS_FRAME_PHASE_SCENE_EVENTS = 0,
    RS_FRAME_PHASE_TEXTURE_EVENTS,
    RS_FRAME_PHASE_BEGIN_3D,
    RS_FRAME_PHASE_MODELS,
    RS_FRAME_PHASE_END_3D,
    RS_FRAME_PHASE_DONE,
};

struct GameRunescape
{
    struct LibToriRS_ScriptQueue* script_queue;
    struct GameCache* gamecache;
    struct World* world;
    struct WorldScene* world_scene;
    struct ToriDraw_Context* context;
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
game_runescape_new(struct LibToriRS_ScriptQueue* script_queue);

void
game_runescape_free(struct GameRunescape* game);

void
game_runescape_set_gamecache(
    struct GameRunescape* game,
    struct GameCache* gamecache);

void
game_runescape_build_world(struct GameRunescape* game);

void
game_runescape_process_input(
    struct GameRunescape* game,
    struct LibToriRS_Input* input);

void
game_runescape_frame_begin(struct GameRunescape* game);

bool
game_runescape_frame_next_command(
    struct GameRunescape* game,
    struct LibToriRS_RenderCommand* command);

void
game_runescape_frame_end(struct GameRunescape* game);

#endif
