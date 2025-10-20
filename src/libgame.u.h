#ifndef LIBGAME_U_H
#define LIBGAME_U_H

#include "cache.h"
#include "osrs/scene.h"
#include "osrs/scene_cache.h"

enum GameGfxOpType
{
    GAME_GFX_OP_TYPE_LOAD_SCENE_MODEL,
    GAME_GFX_OP_TYPE_UNLOAD_SCENE_MODEL,
};

struct GameGfxOp
{
    enum GameGfxOpType type;
    union
    {
        struct
        {
            int scene_model_idx;
            int frame_id;
        } _scene_model_load;
        struct
        {
            int scene_model_idx;
            int frame_id;
        } _scene_model_unload;
    };
};

struct GameGfxOpList
{
    struct GameGfxOp* ops;
    int op_count;
    int op_capacity;
};

struct Game
{
    bool running;

    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_world_x;
    int camera_world_y;
    int camera_world_z;

    int camera_movement_speed;
    int camera_rotation_speed;

    struct Cache* cache;

    struct SceneModel* scene_model;

    struct TexturesCache* textures_cache;
};

#endif