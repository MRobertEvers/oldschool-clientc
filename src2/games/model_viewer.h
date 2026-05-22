#ifndef MODEL_VIEWER_H
#define MODEL_VIEWER_H

#include "../input/libtorirs_input.h"
#include "../scripting/libtorirs_scripting.h"
#include "toripix/toridraw_types.h"

struct ToriDraw_Context;

struct GameModelViewer
{
    struct LibToriRS_ScriptQueue* script_queue;

    int current_model_id;

    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Context* context;
    struct ToriDraw_Position* camera_position;
    struct ToriDraw_Camera* camera;
    struct ToriDraw_ViewPort* view_port;
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

#endif