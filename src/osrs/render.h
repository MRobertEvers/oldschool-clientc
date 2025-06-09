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
    struct SpritePack* sprite_packs,
    int* sprite_ids,
    int sprite_count,
    struct TextureDefinition* textures,
    int* texture_ids,
    int texture_count);

#endif