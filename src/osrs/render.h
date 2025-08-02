#ifndef OSRS_RENDER_H
#define OSRS_RENDER_H

#include <stdint.h>

struct CacheModel;
struct CacheModelBones;
struct Frame;
struct Framemap;
struct TexturesCache;

void render_model_frame(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int model_yaw,
    int model_pitch,
    int model_roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_yaw,
    int camera_pitch,
    int camera_roll,
    int fov,
    int mirrored,
    struct CacheModel* model,
    struct CacheModelBones* bones_nullable,
    struct Frame* frame_nullable,
    struct Framemap* framemap_nullable,
    struct TexturesCache* textures_cache);

struct SceneTile;

struct CacheSpritePack;
struct CacheTexture;

struct SceneTextures
{
    int** texels;
    int texel_count;

    int* texel_id_to_offset_lut;
};

struct SceneTextures* scene_textures_new_from_tiles(
    struct SceneTile* tiles,
    int tile_count,
    struct CacheSpritePack* sprite_packs,
    int* sprite_ids,
    int sprite_count,
    struct CacheTexture* textures,
    int* texture_ids,
    int texture_count);

void scene_textures_free(struct SceneTextures* textures);

void render_scene_tiles(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_yaw,
    int camera_pitch,
    int camera_roll,
    int fov,
    struct SceneTile* tiles,
    int tile_count,
    struct SceneTextures* textures);

struct CacheMapTerrain;
struct SceneLocs;
void render_scene_locs(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_yaw,
    int camera_pitch,
    int camera_roll,
    int fov,
    struct SceneLocs* locs,
    struct SceneTextures* textures);

enum ElementStep
{
    // Do not draw ground until adjacent tiles are done,
    // unless we are spanned by that tile.
    E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED,
    E_STEP_GROUND,
    E_STEP_WAIT_ADJACENT_GROUND,
    E_STEP_LOCS,
    E_STEP_NOTIFY_ADJACENT_TILES,
    E_STEP_NOTIFY_SPANNED_TILES,
    E_STEP_NEAR_WALL,
    E_STEP_DONE,
};

char* element_step_str(enum ElementStep step);

struct SceneElement
{
    enum ElementStep step;

    int remaining_locs;
    int generation;
    int q_count;

    int near_wall_flags;
};

enum SceneOpType
{
    SCENE_OP_TYPE_NONE,
    SCENE_OP_TYPE_DBG_TILE,
    SCENE_OP_TYPE_DRAW_GROUND,
    SCENE_OP_TYPE_DRAW_LOC,
    SCENE_OP_TYPE_DRAW_WALL,
    SCENE_OP_TYPE_DRAW_GROUND_DECOR,
    SCENE_OP_TYPE_DRAW_WALL_DECOR,
};

struct SceneOp
{
    enum SceneOpType op;

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

        struct
        {
            int loc_index;
            int is_wall_a;
        } _wall;

        struct
        {
            int loc_index;
        } _ground_decor;

        struct
        {
            int loc_index;
            int angle;
            int is_wall_a;
            int __rotation;
        } _wall_decor;
        struct
        {
            enum ElementStep step;
            int q_count;
            int loc_count;
        } _dbg;
    };
};
struct Scene;

struct SceneOp*
render_scene_compute_ops(int scene_x, int scene_y, int scene_z, struct Scene* scene, int* len);

struct TexturesCache;
void render_scene_ops(
    struct SceneOp* ops,
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
    struct Scene* scene,
    struct TexturesCache* textures_cache);

#endif