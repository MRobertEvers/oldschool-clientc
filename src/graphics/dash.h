#ifndef DASH_H
#define DASH_H

#include "dash_alphaint.h"
#include "dash_anim.h"
#include "dash_boneint.h"
#include "dash_faceint.h"
#include "dash_hsl16.h"
#include "dash_math.h"
#include "dash_restrict.h"
#include "dash_vertexint.h"
#include "dashmap.h"
#include "lighting.h"

#include <stdbool.h>
#include <stddef.h>
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

/** Vertex positions only; lifetime owned outside DashModel (VA models hold a weak pointer).
 * Face colors, indices, and textures live on DashModelVAGround. */
struct DashVertexArray
{
    int vertex_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    /** Assigned in scene2_vertex_array_register; used in render command payloads. */
    int scene2_gpu_id;
};

/** One triangle face: staging-only type used when pushing into DashFaceArray. */
struct DashFace
{
    faceint_t indices[3];
    hsl16_t colors[3];
    faceint_t texture_id;
    uint16_t _pad;
};

/** SoA face storage shared per terrain level. DashModelVAGround holds a weak ref + first_face_index. */
struct DashFaceArray
{
    faceint_t* indices_a;
    faceint_t* indices_b;
    faceint_t* indices_c;
    hsl16_t* colors_a;
    hsl16_t* colors_b;
    hsl16_t* colors_c;
    faceint_t* texture_ids;
    int count;
    int capacity;
    /** Assigned in scene2_face_array_register; used in render command payloads. */
    int scene2_gpu_id;
};

struct DashModel;

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
    boneint_t** bones;
    boneint_t* bones_sizes;
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

#define DASH_TEXTURE_MAP_CAPACITY 51

struct DashTextureMap
{
    struct DashTexture* textures[DASH_TEXTURE_MAP_CAPACITY];
    int count; /* number of non-NULL entries */
};

void
dashtexturemap_init(struct DashTextureMap* map);

void
dashtexturemap_set(struct DashTextureMap* map, int id, struct DashTexture* texture);

struct DashTexture*
dashtexturemap_get(const struct DashTextureMap* map, int id);

struct DashTexture*
dashtexturemap_iter_next(const struct DashTextureMap* map, int* cursor);

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
    int num_vertices, vertexint_t* vertex_x, vertexint_t* vertex_y, vertexint_t* vertex_z);

void //
dashmodel_calculate_vertex_normals(struct DashModel* model);

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
dashmodel_fast_new(void);

/** Weak reference to vertex_array; caller frees the array separately. face_count starts at 0. */
struct DashModel*
dashmodel_va_new(struct DashVertexArray* vertex_array);

/** Allocates array (faces may be NULL if capacity 0). */
struct DashFaceArray*
dashfacearray_new(int capacity);

void
dashfacearray_free(struct DashFaceArray* fa);

/** Reset count to 0; does not shrink capacity. */
void
dashfacearray_clear(struct DashFaceArray* fa);

/** Grow capacity to at least need_capacity. Returns false on OOM. */
bool
dashfacearray_reserve(
    struct DashFaceArray* fa,
    int need_capacity);

/** Reallocates buffers to fit exactly `count` faces (capacity := count). No-op if already tight. */
void
dashfacearray_shrink_to_fit(struct DashFaceArray* fa);

/** Appends one face; grows as needed. Returns new index or -1 on failure. */
int
dashfacearray_push(
    struct DashFaceArray* fa,
    const struct DashFace* face);

/** VA only: weak ref into shared face_array; first_face_index is offset into faces[]. */
void
dashmodel_va_set_face_array_ref(
    struct DashModel* m,
    struct DashFaceArray* face_array,
    uint32_t first_face_index,
    int face_count);

const struct DashFaceArray*
dashmodel_va_face_array_const(const struct DashModel* m);

uint32_t
dashmodel_va_first_face_index(const struct DashModel* m);

/** VA only: per-tile terrain culling — tile SW corner in world (vertices remain absolute). */
void
dashmodel_va_set_tile_cull_center(
    struct DashModel* m,
    int tile_sw_x,
    int tile_sw_z);

/** VA only: replace bounds cylinder from tile-local (or model-local) vertex positions. */
void
dashmodel_va_set_bounds_cylinder_from_local(
    struct DashModel* m,
    int num_vertices,
    const vertexint_t* vx,
    const vertexint_t* vy,
    const vertexint_t* vz);

struct DashModel*
dashmodelfull_new(void);

struct DashModel*
dashmodel_new(void);

void
dashmodel_free(struct DashModel* model);

/** Frees all heap fields of `va` and `va` itself (caller-owned geometry; not called from
 * dashmodel_free). */
void
dashvertexarray_free(struct DashVertexArray* va);

bool
dashmodel_is_loaded(const struct DashModel* m);

/** Full models participate in CPU lighting / sharelight; Fast terrain does not. */
bool
dashmodel_is_lightable(const struct DashModel* m);

void
dashmodel_set_loaded(
    struct DashModel* m,
    bool v);

bool
dashmodel_has_textures(const struct DashModel* m);

void
dashmodel_set_has_textures(
    struct DashModel* m,
    bool v);

int
dashmodel_vertex_count(const struct DashModel* m);

int
dashmodel_face_count(const struct DashModel* m);

const struct DashVertexArray*
dashmodel_vertex_array_const(const struct DashModel* m);

vertexint_t*
dashmodel_vertices_x(struct DashModel* m);

vertexint_t*
dashmodel_vertices_y(struct DashModel* m);

vertexint_t*
dashmodel_vertices_z(struct DashModel* m);

const vertexint_t*
dashmodel_vertices_x_const(const struct DashModel* m);

const vertexint_t*
dashmodel_vertices_y_const(const struct DashModel* m);

const vertexint_t*
dashmodel_vertices_z_const(const struct DashModel* m);

hsl16_t*
dashmodel_face_colors_a(struct DashModel* m);

hsl16_t*
dashmodel_face_colors_b(struct DashModel* m);

hsl16_t*
dashmodel_face_colors_c(struct DashModel* m);

const hsl16_t*
dashmodel_face_colors_a_const(const struct DashModel* m);

const hsl16_t*
dashmodel_face_colors_b_const(const struct DashModel* m);

const hsl16_t*
dashmodel_face_colors_c_const(const struct DashModel* m);

faceint_t*
dashmodel_face_indices_a(struct DashModel* m);

faceint_t*
dashmodel_face_indices_b(struct DashModel* m);

faceint_t*
dashmodel_face_indices_c(struct DashModel* m);

const faceint_t*
dashmodel_face_indices_a_const(const struct DashModel* m);

const faceint_t*
dashmodel_face_indices_b_const(const struct DashModel* m);

const faceint_t*
dashmodel_face_indices_c_const(const struct DashModel* m);

faceint_t*
dashmodel_face_textures(struct DashModel* m);

const faceint_t*
dashmodel_face_textures_const(const struct DashModel* m);

struct DashBoundsCylinder*
dashmodel_bounds_cylinder(struct DashModel* m);

const struct DashBoundsCylinder*
dashmodel_bounds_cylinder_const(const struct DashModel* m);

alphaint_t*
dashmodel_face_alphas(struct DashModel* m);

const alphaint_t*
dashmodel_face_alphas_const(const struct DashModel* m);

alphaint_t*
dashmodel_original_face_alphas(struct DashModel* m);

int*
dashmodel_face_infos(struct DashModel* m);

const int*
dashmodel_face_infos_const(const struct DashModel* m);

/** Full-only: allocates face_infos (zeroed) when missing (e.g. sharelight face hiding). */
int*
dashmodel_face_infos_ensure_zero(struct DashModel* m);

const uint8_t*
dashmodel_face_priorities(const struct DashModel* m);

hsl16_t*
dashmodel_face_colors_flat(struct DashModel* m);

vertexint_t*
dashmodel_original_vertices_x(struct DashModel* m);

vertexint_t*
dashmodel_original_vertices_y(struct DashModel* m);

vertexint_t*
dashmodel_original_vertices_z(struct DashModel* m);

int
dashmodel_textured_face_count(const struct DashModel* m);

faceint_t*
dashmodel_textured_p_coordinate(struct DashModel* m);

faceint_t*
dashmodel_textured_m_coordinate(struct DashModel* m);

faceint_t*
dashmodel_textured_n_coordinate(struct DashModel* m);

const faceint_t*
dashmodel_textured_p_coordinate_const(const struct DashModel* m);

const faceint_t*
dashmodel_textured_m_coordinate_const(const struct DashModel* m);

const faceint_t*
dashmodel_textured_n_coordinate_const(const struct DashModel* m);

faceint_t*
dashmodel_face_texture_coords(struct DashModel* m);

const faceint_t*
dashmodel_face_texture_coords_const(const struct DashModel* m);

struct DashModelNormals*
dashmodel_normals(struct DashModel* m);

struct DashModelNormals*
dashmodel_merged_normals(struct DashModel* m);

struct DashModelBones*
dashmodel_vertex_bones(struct DashModel* m);

struct DashModelBones*
dashmodel_face_bones(struct DashModel* m);

void
dashmodel_set_vertices_i16(
    struct DashModel* m,
    int count,
    const int16_t* src_x,
    const int16_t* src_y,
    const int16_t* src_z);

void
dashmodel_set_vertices_i32(
    struct DashModel* m,
    int count,
    const int32_t* src_x,
    const int32_t* src_y,
    const int32_t* src_z);

void
dashmodel_set_face_indices_i16(
    struct DashModel* m,
    int count,
    const int16_t* src_a,
    const int16_t* src_b,
    const int16_t* src_c);

void
dashmodel_set_face_indices_i32(
    struct DashModel* m,
    int count,
    const int32_t* src_a,
    const int32_t* src_b,
    const int32_t* src_c);

void
dashmodel_set_face_colors_i16(
    struct DashModel* m,
    const uint16_t* src_a,
    const uint16_t* src_b,
    const uint16_t* src_c);

void
dashmodel_set_face_colors_i32(
    struct DashModel* m,
    const int32_t* src_a,
    const int32_t* src_b,
    const int32_t* src_c);

void
dashmodel_set_face_textures_i16(
    struct DashModel* m,
    const int16_t* src_textures,
    int count);

void
dashmodel_set_face_textures_i32(
    struct DashModel* m,
    const int32_t* src_textures,
    int count);

void
dashmodel_set_face_alphas(
    struct DashModel* m,
    const alphaint_t* src,
    int count);

void
dashmodel_set_face_infos(
    struct DashModel* m,
    const int* infos,
    int count);

void
dashmodel_set_face_priorities(
    struct DashModel* m,
    const int* priorities,
    int count);

void
dashmodel_set_face_colors_flat(
    struct DashModel* m,
    const hsl16_t* src,
    int count);

void
dashmodel_set_texture_coords(
    struct DashModel* m,
    int textured_face_count,
    const faceint_t* p,
    const faceint_t* m_coord,
    const faceint_t* n,
    const faceint_t* face_texture_coords,
    int face_count);

void
dashmodel_set_bounds_cylinder(struct DashModel* m);

void
dashmodel_alloc_lit_face_colors_zero(
    struct DashModel* m,
    int face_count);

/** Sum of heap bytes owned by the model (struct, arrays, normals, bones, bounds). */
size_t
dashmodel_heap_bytes(const struct DashModel* model);

void
dashmodel_alloc_normals(struct DashModel* model);

/** Sharelight only: vertex normals buffer merged across adjacent locs (no face array). */
void
dashmodel_alloc_merged_normals(struct DashModel* model);

void
dashmodel_free_normals(struct DashModel* model);

struct DashModelNormals* //
dashmodel_normals_new(
    int vertex_count,
    int face_count);

void
dashmodel_normals_free(struct DashModelNormals* normals);

struct DashModelNormals* //
dashmodel_normals_new_copy(struct DashModelNormals* normals);

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
    struct DashGraphics* RESTRICT dash,
    struct DashSprite* RESTRICT sprite,
    struct DashViewPort* RESTRICT view_port,
    int x,
    int y,
    int* RESTRICT pixel_buffer);

/** Blit sprite sub-rectangle [src_x,src_y)+[src_w,src_h) to (x,y); still applies sprite
 * crop_x/crop_y to dst. */
void
dash2d_blit_sprite_subrect(
    struct DashGraphics* RESTRICT dash,
    struct DashSprite* RESTRICT sprite,
    struct DashViewPort* RESTRICT view_port,
    int x,
    int y,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int* RESTRICT pixel_buffer);

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
    int* RESTRICT pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb);

void
dash2d_fill_rect_clipped(
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb);

void
dash2d_fill_rect_alpha(
    int* RESTRICT pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int alpha);

void
dash2d_fill_polygon_alpha(
    int* RESTRICT pixel_buffer,
    int stride,
    const int* RESTRICT x,
    const int* RESTRICT y,
    int n,
    int color_rgb,
    int alpha,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom);

void
dash2d_draw_rect_alpha(
    int* RESTRICT pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int alpha);

void
dash2d_draw_line_alpha(
    int* RESTRICT pixel_buffer,
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
    struct DashGraphics* RESTRICT dash,
    struct DashSprite* RESTRICT sprite,
    struct DashViewPort* RESTRICT view_port,
    int x,
    int y,
    int alpha,
    int* RESTRICT pixel_buffer);

void
dash2d_set_bounds(
    struct DashViewPort* RESTRICT view_port,
    int left,
    int top,
    int right,
    int bottom);

int
dash_texture_average_hsl(struct DashTexture* texture);

void
dash2d_fill_rect(
    int* RESTRICT pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb);

void
dash2d_fill_minimap_tile(
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT src_buffer,
    int src_stride,
    int src_crop_x,
    int src_crop_y,
    int src_width,
    int src_height,
    int src_anchor_x,
    int src_anchor_y,
    int* RESTRICT dst_buffer,
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

/** Copy `src_w`×`src_h` from `src_buffer` (row-major, pitch `src_stride`) into `dst_buffer` at
 *  (`dst_x`, `dst_y`), pitch `dst_stride`. Clips to [0, `dst_clip_w`) × [0, `dst_clip_h`). */
static inline void
dash2d_copy_argb_buffer(
    const int* RESTRICT src_buffer,
    int src_w,
    int src_h,
    int src_stride,
    int* RESTRICT dst_buffer,
    int dst_stride,
    int dst_x,
    int dst_y,
    int dst_clip_w,
    int dst_clip_h)
{
    for( int y = 0; y < src_h; y++ )
    {
        int dy = y + dst_y;
        if( dy < 0 || dy >= dst_clip_h )
            continue;
        for( int x = 0; x < src_w; x++ )
        {
            int dx = x + dst_x;
            if( dx >= 0 && dx < dst_clip_w )
                dst_buffer[dy * dst_stride + dx] = src_buffer[y * src_stride + x];
        }
    }
}

void
dash2d_blit_rotated(
    struct DashSprite* RESTRICT sprite,
    int* RESTRICT pixel_buffer,
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