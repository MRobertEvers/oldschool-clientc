#ifndef RENDER_SCENE2_H
#define RENDER_SCENE2_H

#include "scene2.h"

enum Element2Step
{
    // Do not draw ground until adjacent tiles are done,
    // unless we are spanned by that tile.
    E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED,
    E_STEP_GROUND,
    E_STEP_WAIT_ADJACENT_GROUND,
    E_STEP_LOCS,
    E_STEP_NOTIFY_ADJACENT_TILES,
    E_STEP_NOTIFY_SPANNED_TILES,
    E_STEP_DONE,
};

char* element2_step_str(enum Element2Step step);

struct Scene2Element
{
    enum Element2Step step;

    int remaining_locs;
};

enum Scene2OpType
{
    SCENE2_OP_TYPE_NONE,
    SCENE2_OP_TYPE_DRAW_GROUND,
    SCENE2_OP_TYPE_DRAW_LOC,
};

struct Scene2Op
{
    enum Scene2OpType op;

    int x;
    int z;
    int level;

    union
    {
        struct
        {
            int loc_index;
        } _loc;

        struct
        {
            int override_color;
            int color_hsl16;
        } _ground;
    };
};

struct Scene2Op* render_scene2_compute_ops(
    struct Scene2* scene2, int width, int height, int scene_x, int scene_y, int scene_z);

void render_scene2_ops(
    struct Scene2Op* ops,
    int op_count,
    int offset,
    int number_to_render,
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct Scene2* scene2);
#endif