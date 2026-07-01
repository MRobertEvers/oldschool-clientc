#ifndef TRSPK_TORIDRAW_H
#define TRSPK_TORIDRAW_H

#include "graphics/uv_pnm.h"
#include "platformkit/core/trspk_vbo.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

bool
trspk_toridraw_texture_is_animated(
    struct ToriDraw_Scene* ctx,
    int tex_id);

void
trspk_toridraw_vbo_set(
    struct TRSPK_VBO* vbo,
    struct ToriDraw_ModelHandle model_handle);

void
trspk_toridraw_world_vertex(
    const struct ToriDraw_Position* world_position,
    int vx,
    int vy,
    int vz,
    float* out_x,
    float* out_y,
    float* out_z);

void
trspk_toridraw_uv_pnm_face(
    struct UVFaceCoords* out,
    struct ToriDraw_Model* model,
    uint32_t face_index);

void
trspk_toridraw_hsl16_to_rgba(
    uint16_t hsl16,
    uint8_t alpha,
    float rgba[4]);

float
trspk_pack_gpu_uv_mode(
    int animation_direction,
    int animation_speed);

float
trspk_encode_vertex_tex_id(
    int tex_id,
    const struct ToriDraw_Texture* tex);

struct TRSPK_ToriDrawBakeFaceVerts
{
    float color_a[4];
    float color_b[4];
    float color_c[4];
    struct UVFaceCoords uv;
    float wx_a;
    float wy_a;
    float wz_a;
    float wx_b;
    float wy_b;
    float wz_b;
    float wx_c;
    float wy_c;
    float wz_c;
    int tex_id;
    float tex_id_encoded;
    float uv_mode;
    bool is_animated;
};

void
trspk_toridraw_bake_face(
    struct ToriDraw_Model* model,
    uint32_t face_index,
    const struct ToriDraw_Position* world_position,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    struct TRSPK_ToriDrawBakeFaceVerts* out);

#endif
