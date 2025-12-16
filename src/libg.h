#ifndef LIBG_H
#define LIBG_H

#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "osrs/gtask.h"

#include <stdbool.h>
#include <stdint.h>

struct DashGraphics;
struct DashModel;
struct DashPosition;
struct DashViewPort;
struct DashCamera;

struct GGame
{
    bool running;

    int camera_world_x;
    int camera_world_y;
    int camera_world_z;
    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_movement_speed;
    int camera_rotation_speed;

    struct DashGraphics* dash;
    struct DashModel* model;
    struct DashPosition* position;
    struct DashViewPort* view_port;
    struct DashCamera* camera;
    struct GIOQueue* io;
    struct GTask* tasks_nullable;
};

struct GGame* libg_game_new(struct GIOQueue* io);

void libg_game_process_input(struct GGame* game, struct GInput* input);
void libg_game_step_tasks(
    struct GGame* game,
    struct GIOQueue* queue,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

void libg_game_free(struct GGame* game);
void libg_game_step(
    struct GGame* game,
    struct GIOQueue* queue,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

bool libg_game_is_running(struct GGame* game);

#endif