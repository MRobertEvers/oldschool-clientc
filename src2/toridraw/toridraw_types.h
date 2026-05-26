#ifndef TORIDRAW_TYPES_H
#define TORIDRAW_TYPES_H

#include "graphics/projection.h"

#include <stdbool.h>
#include <stdint.h>

typedef int16_t faceint_t;
typedef int16_t vertexint_t;
typedef uint16_t hsl16_t;
typedef uint8_t alphaint_t;
typedef uint16_t boneint_t;

/** Sentinels for channel C in lit face colors; lightness=127 is never produced by lighting. */
#define TORIDRAWHSL16_HIDDEN ((hsl16_t)0xFFFF)
#define TORIDRAWHSL16_FLAT ((hsl16_t)0xFF7F)

struct ToriDraw_BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;

    // TODO: Name?
    // - Max extent from model origin.
    // - Distance to farthest vertex?
    int min_z_depth_any_rotation;
};

#define TORIDRAW_AABB_KIND_CYLINDER_4POINT 0
#define TORIDRAW_AABB_KIND_CYLINDER_8POINT 1

struct ToriDraw_AABB
{
    int kind;
    int min_screen_x;
    int min_screen_y;
    int max_screen_x;
    int max_screen_y;
};

struct ToriDraw_Normal
{
    int16_t x;
    int16_t y;
    int16_t z;
    uint16_t face_count;
    uint8_t merged;
};

struct ToriDraw_Normals
{
    struct ToriDraw_Normal* vertex_normals;
    int vertex_normals_count;

    struct ToriDraw_Normal* face_normals;
    int face_normals_count;
};

struct ToriDraw_Bones
{
    int bones_count;
    boneint_t** bones;
    boneint_t* bones_sizes;
};

struct ToriDraw_Model
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;

    vertexint_t* original_vertices_x;
    vertexint_t* original_vertices_y;
    vertexint_t* original_vertices_z;

    alphaint_t* face_alphas;
    alphaint_t* original_face_alphas;
    int* face_infos;
    /** Two 4-bit priorities per byte (low nibble = even face index). Values 0–12. */
    uint8_t* face_priorities;
    hsl16_t* face_colors;

    int textured_face_count;
    faceint_t* textured_p_coordinate;
    faceint_t* textured_m_coordinate;
    faceint_t* textured_n_coordinate;
    faceint_t* face_texture_coords;

    struct ToriDraw_Normals* normals;
    struct ToriDraw_Normals* merged_normals;
    struct ToriDraw_Bones* vertex_bones;
    struct ToriDraw_Bones* face_bones;
    struct ToriDraw_BoundsCylinder* bounds_cylinder;
};

struct ToriDraw_ModelGround
{
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;
};

enum ToriDraw_ModelKind
{
    TORIDRAWMK_NONE = 0,
    TORIDRAWMK_MODEL = 1,
    TORIDRAWMK_GROUND = 2,
};

struct ToriDraw_ModelHandle
{
    enum ToriDraw_ModelKind kind;
    union
    {
        struct
        {
            struct ToriDraw_Model* model;
            struct ToriDraw_ModelGround* ground;
        } model;
    } u;
};

struct ToriDraw_Position
{
    int x;
    int y;
    int z;
    int pitch;
    int yaw;
    int roll;
};

struct ToriDraw_ViewPort
{
    int width;
    int height;
    int stride;

    int x_center;
    int y_center;

    int clip_left;
    int clip_top;
    int clip_right;
    int clip_bottom;
};

struct ToriDraw_Camera
{
    int fov_rpi2048;
    int near_plane_z;

    int pitch;
    int yaw;
    int roll;
};

enum ToriDraw_TextureAnimation
{
    TORIDRAW_TEXANIM_DIRECTION_NONE,
    TORIDRAW_TEXANIM_DIRECTION_V_DOWN = 1,
    TORIDRAW_TEXANIM_DIRECTION_U_DOWN = 2,
    TORIDRAW_TEXANIM_DIRECTION_V_UP = 3,
    TORIDRAW_TEXANIM_DIRECTION_U_UP = 4,
};

struct ToriDraw_Texture
{
    int* texels;
    int width;
    int height;
    bool opaque;
    int animation_direction;
    int animation_speed;
};

struct ToriDraw_TextureMap
{
    struct ToriDraw_Texture* textures[256];
    int count;
};

struct ToriDraw_Context
{
    struct ToriDraw_ModelHandle active_hnd;

    struct ProjectedVertex projected_vertex;
    struct ToriDraw_AABB aabb;
    struct ToriDraw_AABB cylinder_fast_aabb;

    struct ToriDraw_TextureMap texture_map;

    int screen_vertices_x[4096];
    int screen_vertices_y[4096];
    int screen_vertices_z[4096];
    int orthographic_vertices_x[4096];
    int orthographic_vertices_y[4096];
    int orthographic_vertices_z[4096];

    faceint_t tmp_depth_face_count[1500];
    faceint_t tmp_depth_faces[1500 * 512];
    faceint_t tmp_priority_face_count[12];
    faceint_t tmp_priority_depth_sum[12];
    faceint_t tmp_priority_faces[12 * 2000];
    int tmp_flex_prio11_face_to_depth[1024];
    int tmp_flex_prio12_face_to_depth[512];
    // Used to be 1024, but now we need to support larger models.
    int tmp_face_order[4096];
    int tmp_face_order_count;

    faceint_t sparse_a[4096];
    faceint_t sparse_b[4096];
    faceint_t sparse_c[4096];
};

#define TORIDRAW_CULL_VISIBLE 0
#define TORIDRAW_CULL_FAST 1
#define TORIDRAW_CULL_AABB 2
#define TORIDRAW_CULL_ERROR 3

static inline int*
toridraw_face_order(struct ToriDraw_Context* ctx)
{
    return ctx->tmp_face_order;
}

static inline int
toridraw_face_order_count(struct ToriDraw_Context* ctx)
{
    return ctx->tmp_face_order_count;
}

#endif