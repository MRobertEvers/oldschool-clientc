#ifndef DASH_H
#define DASH_H

#include "dash_anim.h"
#include "dash_math.h"
#include "dashmap.h"
#include "lighting.h"

#include <stdbool.h>
#include <stdint.h>

struct DashBoundsCylinder
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

struct DashAABB
{
    int min_screen_x;
    int min_screen_y;
    int max_screen_x;
    int max_screen_y;
};

struct DashModelLighting
{
    // TODO: These can be shorts (hsl16)
    int* face_colors_hsl_a;

    // null if mode is LIGHTING_FLAT
    int* face_colors_hsl_b;

    // null if mode is LIGHTING_FLAT
    int* face_colors_hsl_c;
};

/**
 * Shared lighting normals are used for roofs and
 * other locs that represent a single surface.
 *
 * See sharelight.\
 *
 * Sharelight "shares" lighting info between all models of
 * a particular "bitset", the bitset is calculated based on
 * the loc rotation, index, etc.
 *
 * When sharelight is true, normals of abutting locs that
 * share vertexes
 * are merged into a single normal.
 *
 * Consider two roof locs that are next to each other.
 * They are aligned such that the inner edges have coinciding vertexes.
 * Sharelight will take the coinciding vertexes and add the normals together.
 *
 * AB AB
 * CD CD
 *
 * Normal 1 points Up and to the Right,
 * Normal 2 points Up and to the Left.
 *
 * They will be merged into a single normal that points upward only.
 *
 * This removes the influence of the face on the sides of the roof models.
 *
 * world3d_merge_locnormals looks at adjacent locs and merges normals if
 * vertexes coincide.
 *
 *
 * Sharelight forces locs to use the same lighting normals, so their lighting
 * will be the same. When the normals are mutated or merged, they will all be
 * affected.
 */

// Each model needs it's own normals, and then sharelight models
// will have shared normals.
struct DashModelNormals
{
    struct LightingNormal* lighting_vertex_normals;
    int lighting_vertex_normals_count;

    struct LightingNormal* lighting_face_normals;
    int lighting_face_normals_count;
};

struct DashModelBones
{
    int bones_count;
    // Array of arrays vertices... AKA arrays of bones.
    int** bones;
    int* bones_sizes;
};

struct DashModelAnimation
{};
struct DashModel
{
    int _dbg_ids[10];
    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;

    /**
     * Only used if animated.
     */
    int* original_vertices_x;
    int* original_vertices_y;
    int* original_vertices_z;

    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    int* face_alphas;
    int* original_face_alphas;
    int* face_infos;
    int* face_priorities;
    int* face_colors;

    int textured_face_count;

    // Used in type 2 >
    int* textured_p_coordinate;
    int* textured_m_coordinate;
    int* textured_n_coordinate;
    int* face_textures;
    int* face_texture_coords;

    struct DashModelNormals* normals;

    struct DashModelLighting* lighting;
    struct DashModelBones* vertex_bones;
    struct DashModelBones* face_bones;
    struct DashBoundsCylinder* bounds_cylinder;
};

enum DashTextureAnimation
{
    TEXANIM_DIRECTION_NONE,
    TEXANIM_DIRECTION_V_DOWN = 1,
    TEXANIM_DIRECTION_U_DOWN = 2,
    TEXANIM_DIRECTION_V_UP = 3,
    TEXANIM_DIRECTION_U_UP = 4,
};

struct DashTexture
{
    int* texels;
    int width;
    int height;

    int animation_direction;
    int animation_speed;

    bool opaque;
};

struct DashViewPort
{
    int stride;

    int width;
    int height;

    int x_center;
    int y_center;
};

struct DashCamera
{
    int fov_rpi2048;
    int near_plane_z;

    int pitch;
    int yaw;
    int roll;
};

struct DashPosition
{
    int x;
    int y;
    int z;
    int pitch;
    int yaw;
    int roll;
};

struct DashPoint2D
{
    int x;
    int y;
};

struct DashPixPalette
{
    int* palette;
    int palette_count;
};

struct DashPix8
{
    uint8_t* pixels;
    int width;
    int height;

    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

struct DashPix32
{
    int* pixels;
    int draw_width;
    int draw_height;

    int crop_x;
    int crop_y;
    int stride_x;
    int stride_y;
};

struct DashSprite
{
    int* pixels_argb;
    int width;
    int height;
};

void
dash_init(void);

struct DashGraphics;
struct DashGraphics* //
dash_new(void);

void //
dash_free(struct DashGraphics* dash);

#define DASHCULL_VISIBLE 0
#define DASHCULL_CULLED_FAST 1
#define DASHCULL_CULLED_AABB 2
#define DASHCULL_ERROR 3

int //
dash3d_render_model( //
    struct DashGraphics* dash, 
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer
);

struct DashAABB*
dash3d_projected_model_aabb(struct DashGraphics* dash);

int
dash3d_project_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

void
dash3d_raster_projected_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer);

bool
dash3d_projected_model_contains(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashViewPort* view_port,
    int screen_x,
    int screen_y);

void //
dash3d_calculate_bounds_cylinder( //
    struct DashBoundsCylinder* bounds_cylinder,
    int num_vertices, int* vertex_x, int* vertex_y, int* vertex_z);

void //
dash3d_calculate_vertex_normals(
    struct DashModelNormals* normals,
    int face_count,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int vertex_count,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z);

void //
dash3d_add_texture(
    struct DashGraphics* dash,
    int texture_id, //
    struct DashTexture* texture);

void
dash_animate_textures(
    struct DashGraphics* dash,
    int time_delta);

struct DashModelNormals* //
dashmodel_normals_new(
    int vertex_count,
    int face_count);

struct DashModelNormals* //
dashmodel_normals_new_copy(struct DashModelNormals* normals);

struct DashModelLighting* //
dashmodel_lighting_new(int face_count);

struct DashBoundsCylinder* //
dashmodel_bounds_cylinder_new(void);

bool //
dashmodel_valid(struct DashModel* model);

void
dashmodel_animate(
    struct DashModel* model,
    struct DashFrame* frame,
    struct DashFramemap* framemap);

struct DashSprite*
dashsprite_new_from_pix8(
    struct DashPix8* pix8,
    struct DashPixPalette* palette);

struct DashSprite*
dashsprite_new_from_pix32(struct DashPix32* pix32);

void
dashpix8_free(struct DashPix8* pix8);
void
dashpixpalette_free(struct DashPixPalette* palette);

void
dash2d_blit_sprite(
    struct DashGraphics* dash,
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int x,
    int y,
    int* pixel_buffer);

void
dashsprite_free(struct DashSprite* sprite);

#endif