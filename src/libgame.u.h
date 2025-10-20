#ifndef LIBGAME_U_H
#define LIBGAME_U_H

#include "cache.h"
#include "osrs/frustrum_cullmap.h"
#include "osrs/scene.h"
#include "osrs/scene_cache.h"

enum GameGfxOpKind
{
    GAME_GFX_OP_SCENE_MODEL_LOAD,
    GAME_GFX_OP_SCENE_MODEL_UNLOAD,
    GAME_GFX_OP_SCENE_MODEL_DRAW,
    GAME_GFX_OP_SCENE_DRAW,
};

struct GameGfxOp
{
    enum GameGfxOpKind kind;
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

        struct
        {
            int scene_model_idx;
            int frame_id;
        } _scene_model_draw;
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

    struct Scene* scene;

    struct TexturesCache* textures_cache;
    struct FrustrumCullmap* frustrum_cullmap;
};

#endif