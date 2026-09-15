#ifndef TORIDRAW_RSCACHE_H
#define TORIDRAW_RSCACHE_H

/*
 * ToriDraw <- RSCache: the adaptor between the two libraries.
 *
 * RSCache decodes a cache blob into a struct that mirrors the WIRE FORMAT:
 * 32-bit vertex coordinates, one byte per face priority, texture coordinates
 * that mean different things depending on a render type, a version byte that
 * says the geometry is stored at four times its real scale. ToriDraw draws a
 * struct built for the RASTER: 16-bit coordinates, packed priority nibbles,
 * per-corner colours that only exist after lighting.
 *
 * Neither library should know the other. RSCache's bar is byte-exact
 * round-trip, so it must not apply the reference's scale-down; ToriDraw draws
 * models from a dozen sources and must not grow a cache dependency. Everything
 * that has to happen between them lives here, in a third library that depends
 * on both and that nothing depends on.
 *
 * ## The whole of it
 *
 *     struct RSCache_Model* raw = RSCache_ModelNewDecode(blob, blob_size);
 *     struct ToriDraw_Model* model = ToriDraw_RSCacheModelSteal(raw);
 *     RSCache_ModelFree(raw);
 *
 *     ToriDraw_RSCacheModelLight(model, NULL);   // NULL = reference lighting
 *
 *     // ... now draw it with toridraw_mini.h, or place it in a scene.
 *
 * ToriDraw_RSCacheModelSteal MOVES the arrays it can and converts the rest, so
 * a 250 KB client never holds two copies of the same geometry. The plain
 * `New` variant copies, for a caller that keeps the decoded model.
 *
 * ## Building it
 *
 * Compile `toridraw_rscache_unity.c` with the include paths of all three:
 *
 *     cc -I3rd/toridraw_rscache/include -I3rd/toridraw -I3rd/rscache/include \
 *        -c 3rd/toridraw_rscache/toridraw_rscache_unity.c
 *
 * It needs the same -DTORIDRAW_PIXEL_FORMAT as the ToriDraw build it links
 * against, because a texture's texels are shaded in ARGB8888 but a sprite it
 * is baked from is not, and the sizes of the two structs it hands across the
 * boundary depend on the format.
 */

#include <rscache.h>
#include <toridraw.h>

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ models */

/**
 * Build a ToriDraw model from a decoded cache model, COPYING every array.
 *
 * `src` is untouched and remains the caller's. Use this when the decoded model
 * is kept -- a re-encode, a diff, a second conversion at another scale.
 *
 * The result carries geometry, face colours (flat, un-lit), textures and
 * bones, plus a bounds cylinder. It does NOT carry per-corner colours: those
 * come from ToriDraw_RSCacheModelLight, and until it runs the model draws
 * black. See that function for why the two are separate calls.
 */
struct ToriDraw_Model*
ToriDraw_RSCacheModelNew(const struct RSCache_Model* src);

/**
 * As above, but MOVING what it can out of `src`, which is left hollow.
 *
 * The arrays whose element type is unchanged between the two libraries --
 * face colours, alphas, the p/m/n triples, the render types, the texture ids --
 * cross by pointer. The rest are converted, because ToriDraw stores a vertex
 * in 16 bits where the cache stores 32, and packs two face priorities into a
 * byte where the cache spends one each.
 *
 * `src` is still the caller's to free: RSCache_ModelFree on a hollow model is
 * correct and is what releases the shell.
 */
struct ToriDraw_Model*
ToriDraw_RSCacheModelSteal(struct RSCache_Model* src);

/**
 * Decode a blob and convert it, in one call.
 *
 * The common case, and the one a small client should reach for: it never holds
 * both representations, because the intermediate is freed before this returns.
 * `data` is not retained. Returns NULL when the blob does not decode.
 *
 * Does not light the model -- see ToriDraw_RSCacheModelLight.
 */
struct ToriDraw_Model*
ToriDraw_RSCacheModelFromBlob(
    const uint8_t* data,
    int data_size);

/* ---------------------------------------------------------------- lighting */

/**
 * A light rig, in the reference client's units.
 *
 * The defaults below are the values every reference client hard-codes for
 * model previews and world geometry alike (Model.calculateNormals(64, 768,
 * -50, -10, -50)). A caller with no opinion passes NULL.
 */
struct ToriDraw_RSCacheLight
{
    int ambient;
    int attenuation;
    int x;
    int y;
    int z;
};

#define TORIDRAW_RSCACHE_LIGHT_DEFAULT                                                             \
    ((struct ToriDraw_RSCacheLight){                                                               \
        .ambient = 64, .attenuation = 768, .x = -50, .y = -10, .z = -50 })

/**
 * Compute vertex normals and turn the model's flat face colours into the
 * per-corner colours the raster interpolates. `light` may be NULL for the
 * reference rig above.
 *
 * ## Why this is not part of the conversion
 *
 * Because lighting is not a property of the cache data, it is a property of
 * the SCENE, and it has to run against the geometry in the pose it will be
 * drawn in. A model that is resized, mirrored or merged with another before it
 * is drawn must be lit after all of that, not before -- the reference client
 * merges a player's dozen body parts and lights the result once. Folding this
 * into the conversion would light every part separately and then throw the
 * work away.
 *
 * It is also the expensive half. A client baking icons offline lights once and
 * caches; one that recolours a model at runtime relights only then.
 *
 * The normals are released afterwards: they are ten bytes a vertex and nothing
 * downstream of lighting reads them. A caller that will relight the same model
 * repeatedly should keep them -- use ToriDraw_RSCacheModelLightKeepNormals.
 */
void
ToriDraw_RSCacheModelLight(
    struct ToriDraw_Model* model,
    const struct ToriDraw_RSCacheLight* light);

/** As above, but leaves model->normals allocated so a later relight skips the
 *  normal pass. Free them with ToriDraw_ModelFreeNormals. */
void
ToriDraw_RSCacheModelLightKeepNormals(
    struct ToriDraw_Model* model,
    const struct ToriDraw_RSCacheLight* light);

/* ---------------------------------------------------------------- textures */

/**
 * Bake one cache sprite into a ToriDraw texture, at `dest_size` square.
 *
 * `dest_size` must be 64 or 128 -- the two sizes the reference client's
 * texture atlas uses, and the two the resampling below implements. A sprite of
 * the other size is point-sampled up or down exactly as the reference does it,
 * including its transposed walk order.
 *
 * `def` supplies the animation direction and speed and the opaque flag; pass
 * NULL for a still, opaque texture. The palette is gamma-corrected by the same
 * 0.8 curve ToriDraw's own HSL palette uses, so a model's textured faces and
 * its solid ones come out at the same brightness.
 *
 * ## What this does not do
 *
 * Multi-sprite textures (v1 defs with more than one layer and a blend type per
 * layer) and procedural materials are the world client's business: they need a
 * sprite loader, a material table and a Perlin generator, none of which belong
 * in an adaptor. This handles the single-sprite case, which is every v2 def by
 * construction and the large majority of v1 ones.
 *
 * The returned texture owns its texels; free it with ToriDraw_TextureFree.
 */
struct ToriDraw_Texture*
ToriDraw_RSCacheTextureFromSprite(
    const struct RSCache_Dat2SpritePack* pack,
    int sprite_index,
    const struct RSCache_Dat2Texture* def,
    int dest_size);

/* --------------------------------------------------------------- animation */

/**
 * Build the rig -- ToriDraw's AnimBase -- from a decoded framemap.
 *
 * Transform type 6 is folded to 2 (ROTATE) here, which is what the reference
 * does at framemap load: it is not a distinct transform, and collapsing it in
 * the render-ready base rather than at apply keeps the apply switch to one
 * rotate arm while leaving the framemap's wire type intact for a re-encode.
 *
 * Free with ToriDraw_RSCacheAnimBaseFree.
 */
struct ToriDraw_AnimBase*
ToriDraw_RSCacheAnimBaseNew(const struct RSCache_Dat2Framemap* framemap);

void
ToriDraw_RSCacheAnimBaseFree(struct ToriDraw_AnimBase* base);

/**
 * Convert one decoded frame into ToriDraw's AnimFrame, in place: `out` is
 * filled and owns its arrays, which ToriDraw_RSCacheAnimFrameCleanup releases.
 *
 * Frames are converted into an array rather than allocated one by one because
 * an animation is a run of them and a client holding twenty is holding twenty
 * allocations it did not need.
 */
void
ToriDraw_RSCacheAnimFrameInit(
    struct ToriDraw_AnimFrame* out,
    const struct RSCache_Dat2Frame* frame);

void
ToriDraw_RSCacheAnimFrameCleanup(struct ToriDraw_AnimFrame* frame);

/**
 * Assemble a complete animation from a framemap and a run of frames.
 *
 * The frames must all belong to `framemap` -- that is what makes them one
 * animation. Free with ToriDraw_AnimationFree, which releases the base and the
 * frames with it.
 */
struct ToriDraw_Animation*
ToriDraw_RSCacheAnimationNew(
    const struct RSCache_Dat2Framemap* framemap,
    const struct RSCache_Dat2Frame* const* frames,
    int frame_count);

#endif /* TORIDRAW_RSCACHE_H */
