#ifndef GAME_MODEL_H
#define GAME_MODEL_H

#include "osrs/tables/model.h"

enum TransformFlag
{
    F_TRANSFORM_MIRROR = 1 << 0,
    F_TRANSFORM_HILSKWEW = 1 << 1,
    F_TRANSFORM_RECOLOR = 1 << 2,
    F_TRANSFORM_RETEXTURE = 1 << 3,
    F_TRANSFORM_SCALE = 1 << 4,
    F_TRANSFORM_ROTATE = 1 << 5,
};

void model_transform_mirror(struct CacheModel* model);

void model_transform_hillskew(
    struct CacheModel* model, int sw_height, int se_height, int ne_height, int nw_height);

void model_transform_recolor(struct CacheModel* model, int color_src, int color_dst);

void model_transform_retexture(struct CacheModel* model, int texture_src, int texture_dst);

void model_transform_scale(struct CacheModel* model, int x, int y, int z);

void model_transform_orient(struct CacheModel* model, int orientation);

void model_transform_translate(struct CacheModel* model, int x, int y, int z);

// enum LightingMode
// {
//     LM_TEXTURED_VERTEX,
//     LM_TEXTURED_FACE,
//     LM_TEXTURED_FLAT_BLACK,
//     LM_VERTEX,
//     LM_FACE,
//     LM_HIDDEN_FACE,
// };

// enum LightingMode model_face_lighting_mode(struct CacheModel* model, int face);

enum FaceDrawMode
{
    DRAW_HIDDEN,
    DRAW_FLAT,
    DRAW_GOURAUD,
    DRAW_TEXTURED,
    DRAW_TEXTURED_PMN,
    DRAW_TEXTURED_FLAT,
    DRAW_TEXTURED_PMN_FLAT
};

// OSRS uses face_colorsC as a control buffer for drawing faces.
enum FaceDrawMode model_face_draw_mode(struct CacheModel* model, int face);

void model_apply_lighting(
    struct CacheModel* model, int face, int* face_colors_a, int* face_colors_b, int* face_colors_c);

// Hidden
// Textured: PNM or Not
// Shade Mode: Flat or Not
// Material: Color or Texture

enum VisibilityMode
{
    VM_VISIBLE = 0,
    VM_HIDDEN = 1,
};

enum ShadeMode
{
    SM_FLAT = 0,
    SM_BLEND = 1,
};

enum MaterialMode
{
    MAT_COLOR = 0,
    MAT_TEXTURE = 1,
};

enum TextureMode
{
    TM_PNM = 0,
    TM_MODEL = 1,
};

enum FaceFlagShifts
{
    FF_SHIFT_HIDDEN = 0,
    FF_SHIFT_BLEND = 1,
    FF_SHIFT_PNM = 2,
    FF_SHIFT_TEXTURE = 3,
};

enum FaceFlags
{
    // Denoted by faceColorC == -2 in original code.
    FF_HIDDEN = 1 << FF_SHIFT_HIDDEN,
    // Denoted by faceColorC == -1 in original code.
    // Only use faceColorA in drawing face.
    FF_BLEND = 1 << FF_SHIFT_BLEND,

    // If texture_coords is populated and NOT -1
    FF_PNM = 1 << FF_SHIFT_PNM,

    // If face_textures is populated and NOT -1
    FF_TEXTURE = 1 << FF_SHIFT_TEXTURE,
};

#define FF_HIDDEN(face_flags) ((face_flags & FF_HIDDEN) >> FF_SHIFT_HIDDEN)
#define FF_BLEND(face_flags) ((face_flags & FF_BLEND) >> FF_SHIFT_BLEND)
#define FF_PNM(face_flags) ((face_flags & FF_PNM) >> FF_SHIFT_PNM)
#define FF_TEXTURE(face_flags) ((face_flags & FF_TEXTURE) >> FF_SHIFT_TEXTURE)

#endif