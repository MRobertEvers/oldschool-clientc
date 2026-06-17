#ifndef GAMECACHE_TYPES_H
#define GAMECACHE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define GAMECACHE_MAP_TERRAIN_X 64
#define GAMECACHE_MAP_TERRAIN_Z 64
#define GAMECACHE_MAP_TERRAIN_LEVELS 4

typedef int16_t gc_faceint_t;
typedef int16_t gc_vertexint_t;
typedef uint16_t gc_hsl16_t;
typedef uint8_t gc_alphaint_t;
typedef uint16_t gc_boneint_t;

struct GameCache_BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;
    int min_z_depth_any_rotation;
};

struct GameCache_Bones
{
    int bones_count;
    gc_boneint_t** bones;
    gc_boneint_t* bones_sizes;
};

struct GameCache_Model
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    gc_vertexint_t* vertices_x;
    gc_vertexint_t* vertices_y;
    gc_vertexint_t* vertices_z;
    gc_hsl16_t* face_colors_a;
    gc_hsl16_t* face_colors_b;
    gc_hsl16_t* face_colors_c;
    gc_faceint_t* face_indices_a;
    gc_faceint_t* face_indices_b;
    gc_faceint_t* face_indices_c;
    gc_faceint_t* face_textures;
    gc_alphaint_t* face_alphas;
    int* face_infos;
    uint8_t* face_priorities;
    gc_hsl16_t* face_colors;
    int textured_face_count;
    gc_faceint_t* textured_p_coordinate;
    gc_faceint_t* textured_m_coordinate;
    gc_faceint_t* textured_n_coordinate;
    gc_faceint_t* face_texture_coords;
    struct GameCache_Bones* vertex_bones;
    struct GameCache_Bones* face_bones;
    struct GameCache_BoundsCylinder* bounds_cylinder;
};

struct GameCache_AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** bone_groups;
    uint16_t* bone_group_lengths;
};

struct GameCache_AnimFrame
{
    int id;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct GameCache_Animation
{
    struct GameCache_AnimBase* base;
    struct GameCache_AnimFrame* frames;
    int frame_count;
};

struct GameCache_Texture
{
    int* texels;
    int width;
    int height;
    bool opaque;
    int animation_direction;
    int animation_speed;
};

struct GameCache_MapFloor
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    uint8_t settings;
    uint8_t shape;
    uint8_t rotation;
};

struct GameCache_MapTerrain
{
    int map_x;
    int map_z;
    struct GameCache_MapFloor
        tiles_xyz[GAMECACHE_MAP_TERRAIN_X * GAMECACHE_MAP_TERRAIN_Z * GAMECACHE_MAP_TERRAIN_LEVELS];
};

struct GameCache_MapLoc
{
    int loc_id;
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

struct GameCache_MapLocs
{
    int chunk_mapx;
    int chunk_mapz;
    struct GameCache_MapLoc* locs;
    int locs_count;
};

struct GameCache_Flotype
{
    int id;
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
};

struct GameCache_Location
{
    int id;
    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;
    int size_x;
    int size_z;
    int blocks_walk;
    int blocks_projectiles;
    int wall_width;
    int seq_id;
    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;
    int sharelight;
    int shadowed;
    int ambient;
    int contrast;
    int mirrored;
    int resize_x;
    int resize_height;
    int resize_z;
    int offset_x;
    int offset_y;
    int offset_z;
    int* recolors_from;
    int* recolors_to;
    int recolor_count;
    int* retextures_from;
    int* retextures_to;
    int retexture_count;
    int map_scene_id;
};

struct GameCache_Sequence
{
    int id;
    int frame_count;
    int* frames;
    int* iframes;
    int* delay;
    int loops;
    bool stretches;
    int priority;
    int replaceheldleft;
    int replaceheldright;
    int maxloops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
    /** Sequence-ordered animation resolved at world build; owned by this sequence. */
    struct GameCache_Animation* resolved;
};

void
gamecache_map_terrain_free(struct GameCache_MapTerrain* terrain);

void
gamecache_map_locs_free(struct GameCache_MapLocs* locs);

void
gamecache_flotype_free(struct GameCache_Flotype* flotype);

void
gamecache_location_free(struct GameCache_Location* loc);

void
gamecache_sequence_free(struct GameCache_Sequence* seq);

void
gamecache_model_free(struct GameCache_Model* model);

void
gamecache_animation_free(struct GameCache_Animation* anim);

void
gamecache_texture_free(struct GameCache_Texture* texture);

struct GameCache_Model*
gamecache_model_new_from_cache_model(const void* cache_model);

struct GameCache_Animation*
gamecache_animation_new_from_cache_dat_animbaseframes(const void* abf);

struct GameCache_Texture*
gamecache_texture_new_from_cache_dat_texture(
    const void* cache_texture,
    int animation_direction,
    int animation_speed);

struct GameCache_MapTerrain*
gamecache_map_terrain_new_from_cache_map_terrain(const void* cache_terrain);

struct GameCache_MapLocs*
gamecache_map_locs_new_from_cache_map_locs(const void* cache_locs);

struct GameCache_Flotype*
gamecache_flotype_new_from_cache_config_overlay(
    const void* cache_overlay,
    int id);

struct GameCache_Location*
gamecache_location_new_from_cache_config_location(const void* cache_loc);

struct GameCache_Sequence*
gamecache_sequence_new_from_cache_dat_sequence(
    const void* cache_seq,
    int id);

#endif
