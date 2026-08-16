#ifndef EV_TEXTURES_H
#define EV_TEXTURES_H

/*
 * Texture loading for the viewer's HD render path.
 *
 * ## Why this is not just a call into the client
 *
 * The client already loads textures, in src/engine/dat2/task_dat2_texture_load.c
 * — correctly, for both systems. But it does so as a *protothread*: every cache
 * read is a `PT_YIELD` awaiting an IO pump, and the dependency walk for a
 * procedural texture is a resumable cursor over a worklist that grows while it
 * is being walked. Driving that from the viewer would mean bringing the whole
 * task/IO/buildcache machinery along for what is, here, a one-shot batch at
 * startup.
 *
 * So this is a synchronous port of the same two flows, reading through
 * RSCache_Dat2DiskArchiveNewLoad directly. The colour rules are not
 * reimplemented — gamma, the transparency test and the average all come from
 * src/engine/texture_palette_bake.c, and the procedural evaluator is
 * src/engine/proctex verbatim — because a texture that is subtly the wrong
 * brightness in the viewer and right in the client is worse than no viewer.
 *
 * ## The two systems
 *
 * **Sprite-backed** (OldSchool, and RS2 before materials): one archive holds
 * every texture definition as a file; each definition names sprites, which are
 * palette-indexed images in the sprite table. Baking is layering those
 * palettes into 128x128 ARGB.
 *
 * **Procedural** (RS2 from ~2010, which is what the QBD source cache is): the
 * texture *is* a program — a graph of operations over noise, gradients and
 * other textures — that has to be evaluated per texel. Its inputs may be other
 * procedural textures and sprites, so the dependency closure must be resident
 * before evaluation starts.
 *
 * Which system a cache uses is probed, not inferred from the revision: the gate
 * is whether the materials table exists, and a cache can be new enough and
 * still not ship one.
 */

#include "asset_access.h"

#include <stdbool.h>
#include <stdint.h>

struct EV_Texture
{
    int id;
    /** Owned, `size * size` ARGB. NULL for an id that failed to bake. */
    int32_t* texels;
    int size;
    bool opaque;
    /** True when this came from the procedural evaluator. */
    bool procedural;

    /*
     * How the material says to composite this texture, NOT what its texels
     * happen to contain.
     *
     * A procedural texture routinely carries partial alpha as an intermediate
     * of its own graph while the material declares it opaque — in the RS727
     * QBD set, 10 of 15 materials are alpha_mode 0 and only 5 are blend. Deriving
     * the gate by scanning the baked texels therefore puts almost everything on
     * the blend path, and blending an opaque face against the background is how
     * a rock texture turns into a dark, half-transparent smear.
     *
     * 0 = opaque, 1 = cutout, 2 = blend. Absent a material table (the
     * sprite-backed caches), this is derived from the texels, which is right
     * there: those palettes are strictly 0 or 255.
     */
    int alpha_mode;
    /** From the material's repeat_s/repeat_t; false means clamp. */
    bool repeat_s;
    bool repeat_t;

    /*
     * Whether the texel modulates the face's authored colour.
     *
     * This era's textures are frequently luminance MASKS, not pictures: of the
     * seven TzTok-Jad uses in RS727, three have a maximum chroma of exactly 0
     * and the faces carrying them are authored deep red. Drawn untinted they
     * come out grey, which is a plausible-looking wrong answer rather than a
     * missing one.
     *
     * On for the material system and off for the sprite-backed one, where the
     * texel IS the colour and the reference does not consult the face colour.
     */
    bool modulate;

    /**
     * Mean luminance of the baked texels, 1..255.
     *
     * The neutral point of the detail map: an average texel should reproduce
     * the face colour unchanged. These range from about 99 to 233 across one
     * cache, so any single global neutral either clips the bright textures or
     * darkens the dark ones.
     */
    int mean_luma;

    /**
     * MEAN (max channel - min channel) across the texels, 0..255.
     *
     * Decides whether the texture carries a hue of its own. A mask averages
     * near 0 and needs the face colour to mean anything; a picture is already
     * coloured, and tinting it applies its hue twice — the RS727 lava/eye
     * texture modulated by its gold face colour clamps to white, which is how
     * TzTok-Jad lost its eyes.
     *
     * The mean and not the maximum: a mostly-grey rock with a few coloured
     * specks has a max chroma in the seventies and is still a mask. Judging by
     * the maximum excluded the texture covering a third of Jad's faces and
     * turned the whole model beige.
     */
    int mean_chroma;
};

struct EV_TextureSet
{
    struct EV_Texture* items;
    int count;
    int capacity;
    /** Highest id + 1; `by_id` is indexed by texture id directly. */
    int* by_id;
    int by_id_len;

    /* What the load found, for the UI to report. */
    bool procedural_system;
    int loaded;
    int failed;
    /** Materials decoded, 0 when the cache has no table. */
    int material_count;
};

/**
 * Load every texture in an open cache.
 *
 * Batch rather than on-demand: the whole point is that a model's face ids can
 * then be answered without a cache round-trip, and for a procedural cache the
 * dependency closures overlap heavily enough that doing them together is much
 * cheaper than one at a time.
 *
 * Failures are per-texture and non-fatal. A procedural texture whose graph uses
 * an operation with no evaluator yet is *refused* rather than approximated,
 * because an unported operation contributes flat grey — which composites into
 * something that looks like a real texture and is not.
 */
bool
ev_textures_load(
    struct Tool_Dat2Cache* cache,
    struct EV_TextureSet* out);

/** NULL when the id is absent or failed to bake. Never asserts on a bad id:
 *  a model can name a texture its own cache does not have. */
const struct EV_Texture*
ev_textures_get(const struct EV_TextureSet* set, int id);

void
ev_textures_free(struct EV_TextureSet* set);

#endif
