#ifndef GAME_H
#define GAME_H

#include "datastruct/vec.h"
#include "osrs/gametask.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "osrs/painters.h"
#include "osrs/scene.h"

#include <stdbool.h>
#include <stdint.h>

struct GGame
{
    bool running;

    int cc;
    bool latched;

    int cycles;

    int mouse_x;
    int mouse_y;

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
    struct GameTask* tasks_nullable;
};

#endif