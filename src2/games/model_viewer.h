#ifndef MODEL_VIEWER_H
#define MODEL_VIEWER_H

#include "../input/libtorirs_input.h"
#include "../render/libtorirs_render.h"
#include "../scripting/libtorirs_scripting.h"
#include "../world/world_scene.h"
#include "toridraw/toridraw_types.h"

struct ToriDraw_Context;

enum GameModelViewer_FramePhase
{
    MV_FRAME_PHASE_SCENE_EVENTS = 0,
    MV_FRAME_PHASE_BEGIN_3D,
    MV_FRAME_PHASE_MODELS,
    MV_FRAME_PHASE_END_3D,
    MV_FRAME_PHASE_DONE,
};

struct GameModelViewer
{
    struct LibToriRS_ScriptQueue* script_queue;

    int current_model_id;

    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Context* context;
    struct ToriDraw_Position* camera_position;
    struct ToriDraw_Camera* camera;
    struct ToriDraw_ViewPort* view_port;

    struct WorldScene* world_scene;
    int current_element_id;

    struct
    {
        enum GameModelViewer_FramePhase phase;
        int event_index;
        int model_index;
    } frame;
};

struct GameModelViewer*
game_modelviewer_new(struct LibToriRS_ScriptQueue* script_queue);

void
game_modelviewer_free(struct GameModelViewer* game_model_viewer);

void
game_modelviewer_set_model(
    struct GameModelViewer* game_model_viewer,
    int model_id,
    struct ToriDraw_ModelHandle model);

void
game_modelviewer_process_input(
    struct GameModelViewer* game_model_viewer,
    struct LibToriRS_Input* input);

void
game_modelviewer_move_forward(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_move_backward(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_move_left(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_move_right(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_move_up(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_move_down(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_rotate_up(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_rotate_down(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_rotate_left(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_rotate_right(
    struct GameModelViewer* game_model_viewer,
    int amount);

void
game_modelviewer_next(
    struct GameModelViewer* game_model_viewer,
    int step);

void
game_modelviewer_frame_begin(struct GameModelViewer* game_model_viewer);

bool
game_modelviewer_frame_next_command(
    struct GameModelViewer* game_model_viewer,
    struct LibToriRS_RenderCommand* command);

void
game_modelviewer_frame_end(struct GameModelViewer* game_model_viewer);

#endif
