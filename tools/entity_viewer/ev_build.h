#ifndef EV_BUILD_H
#define EV_BUILD_H

/*
 * Cache -> renderer, on the server side only.
 *
 * The browser half never links rscache: it receives what these functions built,
 * in ev_wire.h's format. Keeping the decode here is what lets the viewer open a
 * 216 MB cache without shipping it.
 */

#include "asset_access.h"

struct ToriDraw_Model;
struct ToriDraw_Animation;

/**
 * The npc's renderable model: every part in `models[]`, merged, recoloured and
 * retextured as the record asks, then lit.
 *
 * Lighting happens here rather than in the browser because a lit model carries
 * per-corner colours and needs neither normals nor the light model downstream —
 * it halves what the wire format has to describe.
 *
 * Returns NULL when the npc has no models or none of them decode.
 */
struct ToriDraw_Model*
ev_build_npc_model(
    struct Tool_Dat2Cache* c,
    int npc_id);

/**
 * A sequence as an animation: its rig, and each frame in playback order.
 *
 * `out_framemap_id` receives the rig id, which is what the catalog matches on.
 * Returns NULL when the sequence has no frames or its rig cannot be loaded.
 */
struct ToriDraw_Animation*
ev_build_seq_anim(
    struct Tool_Dat2Cache* c,
    int seq_id,
    int* out_framemap_id);

/* Release one with ToriDraw_AnimationFree — it is a plain ToriDraw_Animation,
 * assembled by the client's own ToriDraw_AnimationFromRSCache. */

/**
 * The same npc, built as an HD model so its mapped textures can be drawn.
 *
 * Returns NULL — not an error — when the npc does not need one: a model whose
 * textured faces are all plane-projected is drawn correctly by the classic
 * raster, and promoting it would spend a second pointer per model for nothing.
 * NULL is also returned when the parts carry mapped faces but no usable
 * parameters, because a mapping built from filler draws a confident wrong
 * answer. Free with ToriDraw_ModelHDFree.
 */
struct ToriDraw_ModelHD*
ev_build_npc_model_hd(
    struct Tool_Dat2Cache* c,
    int npc_id);

/**
 * Install the "does the client have this texture's pixels?" predicate.
 *
 * Model building strips any texture id this says no to, because the browser
 * renderer *skips* a face whose texture is missing rather than falling back to
 * its colour. Unset means "none", which is the behaviour from before textures
 * were loadable.
 */
void
ev_build_set_texture_available(int (*fn)(int id, void* user), void* user);

#endif
