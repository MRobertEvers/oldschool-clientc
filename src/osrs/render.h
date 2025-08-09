#ifndef OSRS_RENDER_H
#define OSRS_RENDER_H

#include "lighting.h"

#include <stdbool.h>
#include <stdint.h>

struct CacheModel;
struct CacheModelBones;
struct Frame;
struct Framemap;
struct TexturesCache;
struct ModelLighting;

struct ModelRenderIter
{
    int is_prio;

    int no_prio_depth;
    int no_prio_face_index;

    int prio_prio;
    int prio_face_index;

    struct CacheModel* model;
    struct ModelLighting* lighting;

    int value_face;
};

struct ModelRenderIter* model_render_iter_new(struct CacheModel* model);

void model_render_iter_next(struct ModelRenderIter* iter);

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
    struct CacheModel* model,
    struct ModelLighting* lighting,
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

void render_scene_tile(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* ortho_vertices_x,
    int* ortho_vertices_y,
    int* ortho_vertices_z,
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct SceneTile* tile,
    struct TexturesCache* textures_cache_nullable,
    int* color_override_hsl16_nullable);

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
            int color;
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

struct IterRenderSceneOps
{
    bool has_value;

    struct
    {
        int x;
        int z;
        int level;

        struct SceneModel* model_nullable_;
        struct SceneTile* tile_nullable_;
    } value;

    struct Scene* scene;

    struct SceneOp* _ops;
    int _current_op;
    int _op_count;
};

struct IterRenderSceneOps*
iter_render_scene_ops_new(struct Scene* scene, struct SceneOp* ops, int op_count);

bool iter_render_scene_ops_next(struct IterRenderSceneOps* iter);

void iter_render_scene_ops_free(struct IterRenderSceneOps* iter);

#endif