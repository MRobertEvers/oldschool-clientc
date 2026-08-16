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
    /**
     * The texel value that leaves the face colour unchanged, per texture.
     *
     * A detail map should be neutral on AVERAGE, and these textures average
     * anywhere from 99 to 233 — so one global constant either clips the bright
     * ones or darkens the dark ones. Using each texture's own mean makes an
     * average texel reproduce the face colour exactly, which is the property
     * that makes it a detail map rather than a multiplier.
     *
     * 0 falls back to the tuning value.
     */
    int texture_neutral;
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
/*
 * The modulate tint, from the face's own colour.
 *
 * Hue and saturation only — the authored lightness is NOT folded in. For a
 * textured face the lighting pass has already overwritten colors_a/b/c with
 * plain lightness, and that lightness arrives at the kernel as the shade; using
 * it here as well counts it twice and collapses every lightness-0 face to black.
 * That exact bug is on record (docs/rs2012_materials_backport/
 * HISTORICAL_alpha_kernels.md §2: material 2164 vanished). So the hsl16 is
 * rebuilt at the midpoint of the lightness range, where the palette gives the
 * pure hue, and only the chroma survives.
 */
#define TORIDRAW_HD_MODULATE_LIGHTNESS 64

/*
 * The texel value that leaves the face colour unchanged.
 *
 * These textures are DETAIL MAPS, not pictures: they average around 200 and
 * their job is to vary a surface, not to define it. Multiplying from black
 * (neutral 256) therefore darkens every textured face by ~22% and, worse,
 * compresses the range — the reference render's deep black shadows and bright
 * lit faces both collapse toward the middle, which is the "muddy, flat" look.
 *
 * Normalising at mid-grey instead makes a 128 texel the exact identity, so the
 * texture brightens as often as it darkens and the authored colour survives.
 * Measured on TzTok-Jad this doubles luminance contrast (28.0 -> 56.2 stddev)
 * while leaving mean brightness alone.
 */
#define TORIDRAW_HD_TEXTURE_NEUTRAL 128

/*
 * Tuning knobs for matching a reference render.
 *
 * These exist because the HD compositing rules are being recovered by
 * comparison, not read off a specification: the shipped defaults are the
 * current best understanding and the harness sweeps alternatives. Everything
 * here defaults to the behaviour that would happen without it, so a caller
 * that never touches this sees no difference.
 *
 * Not per material on purpose — a knob under investigation should be one value
 * for the whole frame, or a sweep cannot attribute what changed.
 */
struct ToriDraw_HDTuning
{
    /** Lightness the modulate tint is keyed at, 0..127. Negative — the default
     *  — means use the face's own authored lightness. */
    int tint_lightness;
    /** Percent applied to the tint. 100 is the identity. */
    int tint_scale;
    /** Forces one global neutral, overriding each material's own mean. 0 — the
     *  default — leaves the per-texture rule in place. */
    int texture_neutral;
    /**
     * How much of the face colour's saturation survives, 0..100.
     *
     * A pure multiply preserves the authored hue's saturation exactly, because
     * a grey texel scales all three channels alike. That is right for a mildly
     * coloured face and wrong for a strongly saturated one: the rs643 Jad is
     * authored 4E0903, and reproducing that ratio faithfully gives a render
     * whose green and blue are near zero — far more saturated than the
     * reference, which shows the rock's grey through the red.
     *
     * Below 100 the tint is blended toward white, which lets the texture's own
     * neutrality desaturate the surface.
     */
    int tint_saturation;
};

#define TORIDRAW_HD_TUNING_DEFAULT_INIT                                        \
    {                                                                          \
        -1, 100, 0, 100                                                        \
    }

/** NULL restores the defaults. */
void
ToriDraw_HDSetTuning(const struct ToriDraw_HDTuning* tuning);

void
ToriDraw_HDGetTuning(struct ToriDraw_HDTuning* out);

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
 * The same render, resolved per pixel instead of per face.
 *
 * Draws through the depth-tested twin of every kernel above, and — the part that
 * is not just "z-buffered ToriDraw_RenderHD" — **does not sort the faces at
 * all**. No depth buckets, no `face_priorities`, no `model_priority`: faces are
 * drawn in the order the model stores them and the z-buffer decides what is
 * visible. Back-facing faces are still culled, because the face sort is what
 * used to do that and there is no face sort here.
 *
 * ## When this is the right entry point
 *
 * A model whose parts interpenetrate cannot be drawn correctly by ANY ordering
 * of whole faces, and one imported from a client whose priority bytes mean
 * something else is worse off still — the sort then actively enforces a wrong
 * order. Both are exactly the cases where throwing the order away is an
 * improvement rather than a loss.
 *
 * It is not free and it is not a drop-in for the game path: the reference's
 * layering rules live in the sort, so a model that was authored against them
 * (a cape over a body, a face over a hood) will lose them here. Between MODELS
 * nothing changes — the scene's painter order still applies, and the buffer is
 * reset per model.
 *
 * The scene needs no TORIDRAW_SCENE_MODEL_ZBUFFER flag and the model needs no
 * TORIDRAW_MODEL_FLAG_ZBUFFER: calling this is the opt-in, and the depth scratch
 * is sized on the first call. Same arguments, same return value and the same
 * per-face stats as ToriDraw_RenderHD.
 */
int
ToriDraw_RenderHDZBuffered(
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
