#ifndef OSRS_RENDER_H
#define OSRS_RENDER_H

#include <stdint.h>

struct Model;
struct ModelBones;
struct Frame;
struct Framemap;

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
    struct Model* model,
    struct ModelBones* bones_nullable,
    struct Frame* frame_nullable,
    struct Framemap* framemap_nullable);

struct SceneTile;

struct SpritePack;
struct TextureDefinition;

struct SceneTextures
{
    int** texels;
    int texel_count;

    int* texel_id_to_offset_lut;
};

struct SceneTextures* scene_textures_new_from_tiles(
    struct SceneTile* tiles,
    int tile_count,
    struct SpritePack* sprite_packs,
    int* sprite_ids,
    int sprite_count,
    struct TextureDefinition* textures,
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

#endif