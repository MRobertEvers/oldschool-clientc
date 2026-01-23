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

    int average_hsl;
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

// We have to use UTF16 here because '£' is gets compiled to 0x00A3, which is 2 bytes wide, even in
// a char array.
static const uint16_t DASH_FONT_CHARSET[] = {
    'A', 'B',  'C', 'D', 'E',  'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O',  'P', 'Q', 'R',  'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b',  'c', 'd', 'e',  'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o',  'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1',  '2', '3', '4',  '5', '6', '7', '8', '9', '!', '"', 0x00A3 /*'£'*/,
    '$', '%',  '^', '&', '*',  '(', ')', '-', '_', '=', '+', '[', '{',
    ']', '}',  ';', ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/',
    '?', '\\', '|', ' '
};

// #define CHAR_COUNT (sizeof(CHARSET) - 1)
#define DASH_FONT_CHAR_COUNT 94

struct DashPixFont
{
    int* charcode_set;
    int* char_mask[DASH_FONT_CHAR_COUNT];
    int char_mask_count;

    int char_mask_width[DASH_FONT_CHAR_COUNT];
    int char_mask_height[DASH_FONT_CHAR_COUNT];
    int char_offset_x[DASH_FONT_CHAR_COUNT];
    int char_offset_y[DASH_FONT_CHAR_COUNT];
    int char_advance[DASH_FONT_CHAR_COUNT + 1];
    int draw_width[256];
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

int
dash_hsl16_to_rgb(int hsl16);

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

void
dashfont_draw_text(
    struct DashPixFont* pixfont,
    // 163 is '£'
    uint8_t* text,
    int x,
    int y,
    int color_rgb,
    int* pixels,
    int stride);

int
dash_texture_average_hsl(struct DashTexture* texture);

void
dash2d_fill_rect(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb);

#endif