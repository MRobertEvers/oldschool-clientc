#ifndef SRC_RENDER_TRSPK_TORIDRAW_H
#define SRC_RENDER_TRSPK_TORIDRAW_H

#include "core/trspk_uv_pnm.h"
#include "core/trspk_vbo.h"
#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Scene;

bool
trspk_toridraw_texture_is_animated(
    struct ToriDraw_Scene* ctx,
    int tex_id);

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

/** Encode an atlas slot for the fragment shader. Cutout is packed as
 *  `slot + slot_capacity` (was hardcoded 256 when ids were the slots). */
float
trspk_encode_vertex_tex_id(
    int atlas_slot,
    bool cutout,
    int slot_capacity);

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
    /** True when the texture uses alpha cutout; the GL3 path encodes this with the atlas slot. */
    bool tex_cutout;
    /** Filled by the GL3 path after allocating an atlas slot; unused by soft3d. */
    float tex_id_encoded;
    float uv_mode;
    bool is_animated;
};

/** Bake one MODEL face into GPU verts. Hidden / fully-transparent faces are
 * submitted with alpha 0 (same as TORIDRAWHSL16_HIDDEN), matching the soft
 * raster's early-out and SceneBuffer.getModelFaces cull. */
void
trspk_toridraw_bake_face(
    struct ToriDraw_Model* model,
    uint32_t face_index,
    const struct ToriDraw_Position* world_position,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    struct TRSPK_ToriDrawBakeFaceVerts* out);

/** Bake a face from a ModelHandle (MODEL or GROUND). Returns false if unsupported. */
bool
trspk_toridraw_bake_face_handle(
    struct ToriDraw_ModelHandle model_handle,
    uint32_t face_index,
    const struct ToriDraw_Position* world_position,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    struct TRSPK_ToriDrawBakeFaceVerts* out);

int
trspk_toridraw_face_count(struct ToriDraw_ModelHandle model_handle);

#endif
