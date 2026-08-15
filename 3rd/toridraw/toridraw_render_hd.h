#ifndef TORIDRAW_RENDER_HD_H
#define TORIDRAW_RENDER_HD_H

#include "toridraw_types.h"

#include <stdbool.h>

/*
 * The HD render flow: route an OB3 model's faces to the 48 textured kernels.
 *
 * ## What "HD" means here, precisely
 *
 * Not a new renderer. The same projection, the same face sort, the same
 * 16.16 walkers — but a face is routed on what the *material* and the *mapping*
 * say, instead of on the two things the stock path can express (is there a
 * texture, is it colour-keyed).
 *
 * Four decisions per face, orthogonal to each other:
 *
 *   projection   texture_render_types[coord]: 0 plane, 1 cylinder, 2 cube,
 *                3 sphere. Types 1-3 need a mapping, which is why they are only
 *                reachable from a TORIDRAWMK_MODEL_HD handle.
 *   gate         the material's: every texel / colour key / per-texel alpha.
 *   facealpha    the face's own alpha byte, when it has one.
 *   modulate     the material's: tint the shaded texel by the face colour.
 *
 * ## What it is not
 *
 * It does not light, decode, or bake anything. The caller supplies materials
 * that are already resident as ARGB texels, and a model whose mappings are
 * already built. A material with no texels is a normal state — the face falls
 * back to its flat colour rather than disappearing, which is the opposite of
 * what the stock raster does (it skips the face) and is the behaviour a viewer
 * wants.
 */

struct ToriDraw_Scene;

/** How a material's texels decide coverage. */
enum ToriDraw_HDGate
{
    /** Every texel is drawn. A diffuse map with no transparency. */
    TORIDRAW_HD_GATE_OPAQUE = 0,
    /** RGB 0 is a hole. The only transparency a stock OSRS texture carries. */
    TORIDRAW_HD_GATE_TRANS = 1,
    /** The texel's own alpha byte, 0-255. What a procedural material needs. */
    TORIDRAW_HD_GATE_ALPHA = 2,
};

/** One texture id's render-side description. */
struct ToriDraw_HDMaterial
{
    /** ARGB8888, `width * width` entries. NULL means "not resident", which is
     *  a normal state and makes the face fall back to flat colour. */
    const int* texels;
    /** 64 or 128; anything else is treated as not resident. */
    int width;
    /** enum ToriDraw_HDGate. */
    int gate;
    /** Non-zero clamps that axis instead of wrapping it. */
    int clamp_s;
    int clamp_t;
    /** Non-zero tints the shaded texel by the face's own colour — for the
     *  greyscale detail maps whose RGB is not the surface colour. */
    int modulate;
};

/** The table, indexed by texture id. */
struct ToriDraw_HDMaterials
{
    const struct ToriDraw_HDMaterial* items;
    int count;
};

/**
 * Where the faces went. Every drawn face lands in exactly one of the family
 * counters, so they sum with the skips to the model's face count.
 *
 * This exists because "it rendered" is not evidence that the routing is right:
 * a cube face drawn through the plane kernel still produces pixels. The viewer
 * reports these, and the routing test asserts on them.
 */
struct ToriDraw_HDRenderStats
{
    int faces;
    int drawn_untextured;
    int drawn_plane;
    int drawn_cylinder;
    int drawn_cube;
    int drawn_sphere;
    /** Textured faces whose material had no texels; drawn as flat colour. */
    int fallback_no_texels;
    /** Faces a mapped render type named, on a model with no mappings; drawn
     *  through the plane kernel, which is wrong but visible. */
    int fallback_no_mapping;
    int skipped_hidden;
    int skipped_alpha;
    /** Per-gate tallies over the textured faces. */
    int gate_opaque;
    int gate_trans;
    int gate_alpha;
    int with_facealpha;
    int with_modulate;
};

/**
 * Project, sort and draw `hnd` through the HD routing.
 *
 * Accepts a plain TORIDRAWMK_MODEL as well as an HD one; without the HD tail
 * every face routes to the plane family, which is exactly the stock behaviour
 * plus the material gates. `materials` may be NULL, in which case every textured
 * face falls back to flat colour. `out_stats` may be NULL.
 *
 * Returns the cull result, as ToriDraw_RenderModel1Project does;
 * TORIDRAW_CULL_ERROR for a handle that carries no model.
 */
int
ToriDraw_RenderHD(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* out_stats);

/**
 * Build the per-face-group mappings for an HD model from a decoder's raw
 * fields, and take ownership of the result on `hd->texture_mappings`.
 *
 * The raw arrays are the ones RSCache_Model carries for render types 1-3;
 * passing NULL for any of them is legal and yields zeroes for that field. Uses
 * the bind-pose vertices already on `hd->base`, so it must be called before any
 * animation is applied — the mapping centre is a property of the rest pose.
 *
 * Requires ToriDraw_InitAtanTable(). Returns false on allocation failure or if
 * the model has no textured faces.
 */
bool
ToriDraw_ModelBuildTextureMappings(
    struct ToriDraw_ModelHD* hd,
    const int32_t* scale_x,
    const int32_t* scale_y,
    const int32_t* scale_z,
    const int8_t* rotation,
    const int8_t* direction,
    const int8_t* speed,
    const int8_t* trans_u,
    const int8_t* trans_v);

#endif
