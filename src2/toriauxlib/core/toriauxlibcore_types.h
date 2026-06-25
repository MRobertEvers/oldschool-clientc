#ifndef TORIAUXLIBCORE_TYPES_H
#define TORIAUXLIBCORE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Sprite;
struct ToriDraw_Font;

#define TORIAUXLIBCORE_MAP_TERRAIN_X 64
#define TORIAUXLIBCORE_MAP_TERRAIN_Z 64
#define TORIAUXLIBCORE_MAP_TERRAIN_LEVELS 4

typedef int16_t gc_faceint_t;
typedef int16_t gc_vertexint_t;
typedef uint16_t gc_hsl16_t;
typedef uint8_t gc_alphaint_t;
typedef uint16_t gc_boneint_t;

struct ToriAuxLibCore_BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;
    int min_z_depth_any_rotation;
};

struct ToriAuxLibCore_Bones
{
    int bones_count;
    gc_boneint_t** bones;
    gc_boneint_t* bones_sizes;
};

struct ToriAuxLibCore_Model
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
    struct ToriAuxLibCore_Bones* vertex_bones;
    struct ToriAuxLibCore_Bones* face_bones;
    struct ToriAuxLibCore_BoundsCylinder* bounds_cylinder;
};

struct ToriAuxLibCore_AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** bone_groups;
    uint16_t* bone_group_lengths;
};

struct ToriAuxLibCore_AnimFrame
{
    int id;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct ToriAuxLibCore_Animation
{
    struct ToriAuxLibCore_AnimBase* base;
    struct ToriAuxLibCore_AnimFrame* frames;
    int frame_count;
};

struct ToriAuxLibCore_Texture
{
    int* texels;
    int width;
    int height;
    bool opaque;
    int animation_direction;
    int animation_speed;
};

struct ToriAuxLibCore_MapFloor
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    uint8_t settings;
    uint8_t shape;
    uint8_t rotation;
};

struct ToriAuxLibCore_MapTerrain
{
    int map_x;
    int map_z;
    struct ToriAuxLibCore_MapFloor
        tiles_xyz[TORIAUXLIBCORE_MAP_TERRAIN_X * TORIAUXLIBCORE_MAP_TERRAIN_Z * TORIAUXLIBCORE_MAP_TERRAIN_LEVELS];
};

struct ToriAuxLibCore_MapLoc
{
    int loc_id;
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

struct ToriAuxLibCore_MapLocs
{
    int chunk_mapx;
    int chunk_mapz;
    struct ToriAuxLibCore_MapLoc* locs;
    int locs_count;
};

struct ToriAuxLibCore_Flotype
{
    int id;
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
};

struct ToriAuxLibCore_Location
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
    int transform_varbit;
    int transform_varp;
    int* transforms;
    int transform_count;
};

struct ToriAuxLibCore_Sequence
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
    struct ToriAuxLibCore_Animation* resolved;
};

struct ToriAuxLibCore_Sprite
{
    char name[64];
    struct ToriDraw_Sprite** sprites;
    int count;
};

struct ToriAuxLibCore_Font
{
    char name[16];
    struct ToriDraw_Font* font;
};

struct ToriAuxLibCore_Component
{
    int id;
    uint8_t hide;
    int button_type;
    int client_code;
    int over_color;
    int active_color;
    int active_over_color;
    int scripts_count;
    int** scripts;
    int* scripts_lengths;
    int* script_comparator;
    int* script_operand;
};

void
ToriAuxLibCore_MapTerrainFree(struct ToriAuxLibCore_MapTerrain* terrain);

void
ToriAuxLibCore_MapLocsFree(struct ToriAuxLibCore_MapLocs* locs);

void
ToriAuxLibCore_FlotypeFree(struct ToriAuxLibCore_Flotype* flotype);

void
ToriAuxLibCore_LocationFree(struct ToriAuxLibCore_Location* loc);

void
ToriAuxLibCore_SequenceFree(struct ToriAuxLibCore_Sequence* seq);

void
ToriAuxLibCore_ModelFree(struct ToriAuxLibCore_Model* model);

void
ToriAuxLibCore_AnimationFree(struct ToriAuxLibCore_Animation* anim);

void
ToriAuxLibCore_TextureFree(struct ToriAuxLibCore_Texture* texture);

void
ToriAuxLibCore_SpriteFree(struct ToriAuxLibCore_Sprite* sprite);

void
ToriAuxLibCore_FontFree(struct ToriAuxLibCore_Font* font);

void
ToriAuxLibCore_ComponentFree(struct ToriAuxLibCore_Component* component);

#endif
