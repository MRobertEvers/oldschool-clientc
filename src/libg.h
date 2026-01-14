#ifndef LIBG_H
#define LIBG_H

#include "datastruct/vec.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "osrs/gtask.h"
#include "osrs/painters.h"
#include "osrs/scene.h"

#include <stdbool.h>
#include <stdint.h>

struct DashGraphics;
struct DashModel;
struct DashPosition;
struct DashViewPort;
struct DashCamera;

enum EntityKind
{
    ENTITY_SCENERY_STATIC,
};

struct Entity
{
    int id;
    enum EntityKind kind;

    int model_one;
    int model_two;
    int position;
    int view_port;
};

struct GGame
{
    bool running;

    int cc;

    int camera_world_x;
    int camera_world_y;
    int camera_world_z;
    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_movement_speed;
    int camera_rotation_speed;

    struct Vec* entity_dashmodels;
    struct Vec* entity_painters;

    struct Vec* scene_elements;
    struct Scene* scene;

    struct DashGraphics* sys_dash;
    struct Painter* sys_painter;
    struct PaintersBuffer* sys_painter_buffer;

    struct DashModel* model;
    struct DashPosition* position;
    struct DashViewPort* view_port;
    struct DashCamera* camera;
    struct GIOQueue* io;
    struct GTask* tasks_nullable;
};

struct GGame*
libg_game_new(struct GIOQueue* io);

void
libg_game_process_input(
    struct GGame* game,
    struct GInput* input);
void
libg_game_step_tasks(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

void
libg_game_free(struct GGame* game);
void
libg_game_step(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

bool
libg_game_is_running(struct GGame* game);

#endif