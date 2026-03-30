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

enum DashAABBKind
{
    DASHAABB_KIND_UNKNOWN,
    DASHAABB_KIND_CYLINDER_4POINT,
    DASHAABB_KIND_CYLINDER_8POINT,
    DASHAABB_KIND_VERTICES_AABB,
};

struct DashAABB
{
    enum DashAABBKind kind;
    int min_screen_x;
    int min_screen_y;
    int max_screen_x;
    int max_screen_y;
};

struct DashModelLighting
{
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

struct DashModel
{
    bool loaded;
    bool has_textures;
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
    struct DashModelNormals* merged_normals;

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

    // Clipping bounds (Pix2D.setBounds equivalent)
    int clip_left;
    int clip_top;
    int clip_right;
    int clip_bottom;
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
    uint32_t* pixels_argb;
    int width;
    int height;
    /* Pix8 crop offset: draw(x,y) blits at (x+crop_x, y+crop_y) per Client.ts Pix8.draw */
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
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

struct DashFontAtlas
{
    uint8_t* rgba_pixels;
    int atlas_width;
    int atlas_height;
    int glyph_x[DASH_FONT_CHAR_COUNT];
    int glyph_y[DASH_FONT_CHAR_COUNT];
    int glyph_w[DASH_FONT_CHAR_COUNT];
    int glyph_h[DASH_FONT_CHAR_COUNT];
};

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
    int height2d;
    struct DashFontAtlas* atlas;
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

struct DashAABB*
dash3d_projected_model_cylinder_fast_aabb(struct DashGraphics* dash);

void
dash3d_copy_screen_vertices_float(
    struct DashGraphics* dash,
    float* out_x,
    float* out_y,
    int count);

int
dash3d_project_point(
    struct DashGraphics* dash,
    int scene_x,
    int scene_y,
    int scene_z,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* out_screen_x,
    int* out_screen_y);

int
dash3d_project_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

int
dash3d_prepare_projected_face_order(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

const int*
dash3d_projected_face_order(
    struct DashGraphics* dash,
    int* out_face_count);

int
dash3d_project_model6(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

int
dash3d_cull(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

int
dash3d_cull_fast(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

int
dash3d_cull_aabb(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

int
dash3d_project_raw(
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
    int* pixel_buffer,
    bool smooth);

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

struct DashPosition*
dashposition_new(void);

void
dashposition_free(struct DashPosition* position);

struct DashModel*
dashmodel_new(void);

void
dashmodel_free(struct DashModel* model);

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

void
dashmodel_animate_mask(
    struct DashModel* model,
    struct DashFrame* primary_frame,
    struct DashFrame* secondary_frame,
    struct DashFramemap* framemap,
    int* walkmerge);

struct DashSprite*
dashsprite_new_from_pix8(
    struct DashPix8* pix8,
    struct DashPixPalette* palette);

struct DashSprite*
dashsprite_new_from_pix32(struct DashPix32* pix32);

/** Takes ownership of pixels_argb (heap ARGB, row-major). */
struct DashSprite*
dashsprite_new_from_argb_owned(
    uint32_t* pixels_argb,
    int width,
    int height);

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

/** Blit sprite sub-rectangle [src_x,src_y)+[src_w,src_h) to (x,y); still applies sprite
 * crop_x/crop_y to dst. */
void
dash2d_blit_sprite_subrect(
    struct DashGraphics* dash,
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int x,
    int y,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int* pixel_buffer);

void
dashsprite_flip_horizontal(struct DashSprite* sprite);
void
dashsprite_flip_vertical(struct DashSprite* sprite);
void
dashsprite_free(struct DashSprite* sprite);

int
dashfont_text_width(
    struct DashPixFont* pixfont,
    uint8_t* text);

int
dashfont_text_width_taggable(
    struct DashPixFont* pixfont,
    uint8_t* text);

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

void
dashfont_draw_text_ex(
    struct DashPixFont* pixfont,
    uint8_t* text,
    int x,
    int y,
    int default_color_rgb,
    int* pixels,
    int stride);

void
dashfont_draw_text_clipped(
    struct DashPixFont* pixfont,
    uint8_t* text,
    int x,
    int y,
    int color_rgb,
    int* pixels,
    int stride,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom);

void
dashfont_draw_text_clipped_taggable(
    struct DashPixFont* pixfont,
    uint8_t* text,
    int x,
    int y,
    int default_color_rgb,
    int* pixels,
    int stride,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom,
    bool shadowed);

struct DashFontAtlas*
dashfont_build_atlas(struct DashPixFont* pixfont);

void
dashfont_free_atlas(struct DashFontAtlas* atlas);

void
dashpixfont_free(struct DashPixFont* font);

int
dashfont_charcode_to_glyph(uint8_t code_point);

int
dashfont_evaluate_color_tag(const char* tag);

void
dash2d_fill_rect(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb);

void
dash2d_fill_rect_clipped(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom);

void
dash2d_draw_rect(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb);

void
dash2d_fill_rect_alpha(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int alpha);

void
dash2d_fill_polygon_alpha(
    int* pixel_buffer,
    int stride,
    const int* x,
    const int* y,
    int n,
    int color_rgb,
    int alpha,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom);

void
dash2d_draw_rect_alpha(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int alpha);

void
dash2d_draw_line_alpha(
    int* pixel_buffer,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_rgb,
    int alpha,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom);

void
dash2d_blit_sprite_alpha(
    struct DashGraphics* dash,
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int x,
    int y,
    int alpha,
    int* pixel_buffer);

void
dash2d_set_bounds(
    struct DashViewPort* view_port,
    int left,
    int top,
    int right,
    int bottom);

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

void
dash2d_fill_minimap_tile(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int background_rgb,
    int foreground_rgb,
    int rotation,
    int shape,
    int clip_width,
    int clip_height);

void
dash2d_draw_minimap_wall(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int wall,
    int clip_width,
    int clip_height);

/** Inverse-map blit: for each destination pixel, sample source with rotation.
 *  `src_width`/`src_height` are the rotated sub-rectangle in source space; that region starts at
 *  (`src_crop_x`, `src_crop_y`) in the buffer. Row pitch is `src_stride` (full bitmap width). */
static inline void
dash2d_blit_rotated_ex(
    int* src_buffer,
    int src_stride,
    int src_crop_x,
    int src_crop_y,
    int src_width,
    int src_height,
    int src_anchor_x,
    int src_anchor_y,
    int* dst_buffer,
    int dst_stride,
    int dst_x,
    int dst_y,
    int dst_width,
    int dst_height,
    int dst_anchor_x,
    int dst_anchor_y,
    int angle_r2pi2048)
{
    int sin = dash_sin(angle_r2pi2048);
    int cos = dash_cos(angle_r2pi2048);

    int min_x = dst_x;
    int min_y = dst_y;
    int max_x = dst_x + dst_width;
    int max_y = dst_y + dst_height;

    if( min_x < 0 )
        min_x = 0;
    if( max_x > dst_stride )
        max_x = dst_stride;
    if( min_x >= max_x )
        return;

    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        for( int dst_x_abs = min_x; dst_x_abs < max_x; dst_x_abs++ )
        {
            int rel_x = dst_x_abs - dst_x - dst_anchor_x;
            int rel_y = dst_y_abs - dst_y - dst_anchor_y;

            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

            int src_x = src_anchor_x + src_rel_x;
            int src_y = src_anchor_y + src_rel_y;

            if( src_x >= 0 && src_x < src_width && src_y >= 0 && src_y < src_height )
            {
                int bx = src_crop_x + src_x;
                int by = src_crop_y + src_y;
                int src_pixel = src_buffer[by * src_stride + bx];
                if( src_pixel != 0 )
                    dst_buffer[dst_y_abs * dst_stride + dst_x_abs] = src_pixel;
            }
        }
    }
}

void
dash2d_blit_rotated(
    struct DashSprite* sprite,
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y,
    int angle_r2pi2048);

void
dashframe_free(struct DashFrame* frame);

void
dashframemap_free(struct DashFramemap* framemap);
#endif