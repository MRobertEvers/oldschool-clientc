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

/** A model's placement with its rotation already resolved.
 *
 * Every vertex of a model is placed by the SAME rotation, so normalising the
 * two angles and reading the four trig table entries belongs once per model,
 * not once per corner. A face has three corners and a model has hundreds of
 * faces, so done per corner that setup ran ~6x for every vertex the mesh
 * actually has -- it was the most repeated arithmetic in the bake.
 *
 * `has_pitch` / `has_yaw` keep the reference's behaviour that a zero angle
 * skips its rotation entirely rather than multiplying through by an identity.
 */
struct TRSPK_WorldPlacement
{
    int cos_pitch;
    int sin_pitch;
    int cos_yaw;
    int sin_yaw;
    int offset_x;
    int offset_y;
    int offset_z;
    bool has_pitch;
    bool has_yaw;
};

/** A model drawn in its own frame: no rotation, no translation. Widget and
 * chathead models are baked against this. */
extern const struct TRSPK_WorldPlacement trspk_world_placement_identity;

/** Resolve `world_position` once, ahead of a face loop. A NULL position is the
 * model's own frame -- the identity placement -- not a contract violation. */
void
trspk_toridraw_placement_init(
    struct TRSPK_WorldPlacement* out,
    const struct ToriDraw_Position* world_position);

void
trspk_toridraw_world_vertex(
    const struct TRSPK_WorldPlacement* placement,
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

/** Which colour representation a bake should produce.
 *
 * There is no cheap conversion between the two, so the caller picks the one
 * its vertex format actually stores and the other is never computed. The
 * float form used to be unconditional, which meant the D3D9 lane paid
 * hsl16 -> uint32 RGB -> four normalised floats and then immediately threw
 * that away again converting back to a packed uint32 ARGB.
 */
enum TRSPK_BakeColorForm
{
    /** Fill color_a/b/c. The GL and WebGL lanes upload floats, and the D3D9
     *  widget path runs them through a float clipper. */
    TRSPK_BAKE_COLOR_FLOAT = 0,
    /** Fill argb_a/b/c. This is what a D3D9 vertex stores. */
    TRSPK_BAKE_COLOR_ARGB = 1
};

struct TRSPK_ToriDrawBakeFaceVerts
{
    /** Valid only when the bake was asked for TRSPK_BAKE_COLOR_FLOAT. */
    float color_a[4];
    float color_b[4];
    float color_c[4];
    /** Valid only when the bake was asked for TRSPK_BAKE_COLOR_ARGB. */
    uint32_t argb_a;
    uint32_t argb_b;
    uint32_t argb_c;
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
    const struct TRSPK_WorldPlacement* placement,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    enum TRSPK_BakeColorForm color_form,
    struct TRSPK_ToriDrawBakeFaceVerts* out);

/** Bake a face from a ModelHandle (MODEL or GROUND). Returns false if unsupported. */
bool
trspk_toridraw_bake_face_handle(
    struct ToriDraw_ModelHandle model_handle,
    uint32_t face_index,
    const struct TRSPK_WorldPlacement* placement,
    struct ToriDraw_Scene* ctx,
    bool invert_face_alpha,
    enum TRSPK_BakeColorForm color_form,
    struct TRSPK_ToriDrawBakeFaceVerts* out);

int
trspk_toridraw_face_count(struct ToriDraw_ModelHandle model_handle);

#endif
