#ifndef TORIRS_MODEL_CHAIN_FORMAT_H
#define TORIRS_MODEL_CHAIN_FORMAT_H
#include "render/torirs_render.h"
#include <stdint.h>

/* Little-endian, fixed-width, no pointers. Arrays follow each header:
 * xyz int16 vertices; abc int16 faces; optional packed priorities and uint16
 * colour-C; then (visible only) xyz int32 projections and int32 face order.
 * Geometry is captured AFTER the requested pose has been applied. */
#define MODEL_CHAIN_MAGIC 0x4b434831u
#define MODEL_CHAIN_VERSION 2u
#define MODEL_CHAIN_END 0x4b434845u
#define MODEL_CHAIN_PRIORITIES 1u
#define MODEL_CHAIN_COLOUR_C 2u
#define MODEL_CHAIN_BOUNDS 4u
#define MODEL_CHAIN_ANIMATED 8u
#define MODEL_CHAIN_DYNAMIC 16u
#define MODEL_CHAIN_KIND_SHIFT 24u
#define MODEL_CHAIN_PICKABLE 1u
#define MODEL_CHAIN_PICK_AABB 2u
#define MODEL_CHAIN_PICK_TERRAIN 4u
#define MODEL_CHAIN_PICK_ONLY 8u

struct ModelChainHeader
{
    uint32_t magic, version, pass, ordinal;
    int32_t vertices, faces, textures, tile_kernel, model_flags;
    uint32_t options;
    int32_t max_vertices, max_faces, depth_levels;
    int32_t element_id, model_token, pick_enabled, pick_x, pick_y;
    uint32_t pick_flags;
    int32_t cull, sorted_count, pick_hit, projected_depth, near_clipped;
    int32_t projected_box[4];
    struct ToriDraw_Position position;
    struct ToriRS_RenderCommand_Begin3D begin;
    struct ToriDraw_BoundsCylinder bounds;
};
_Static_assert(sizeof(int) == 4, "capture integers");
_Static_assert(sizeof(struct ToriDraw_Position) == 24, "capture position");
_Static_assert(sizeof(struct ToriDraw_ViewPort) == 36, "capture viewport");
_Static_assert(sizeof(struct ToriDraw_Camera) == 36, "capture camera");
_Static_assert(sizeof(struct ModelChainHeader) == 256, "capture header layout");
#endif
