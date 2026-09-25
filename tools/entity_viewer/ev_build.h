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
 * Which of an obj's model sets to build.
 *
 * An obj record carries several unrelated meshes, and "the item's model" means
 * a different one depending on who is asking: the thing on the ground and in
 * the inventory icon, the thing on a male or female body, and the thing on a
 * chathead. They are separate ids in the record, so the caller picks.
 */
enum EV_ObjModelVariant
{
    /** `inventory_model_id` — the ground drop and the inventory icon. */
    EV_OBJ_MODEL_ITEM = 0,
    /** male_model_0..2 merged, as the player build wears them. */
    EV_OBJ_MODEL_MALE,
    EV_OBJ_MODEL_FEMALE,
    /** The chathead pair (male_head_model, male_head_model_2). */
    EV_OBJ_MODEL_MALE_HEAD,
    EV_OBJ_MODEL_FEMALE_HEAD,
    EV_OBJ_MODEL_VARIANT_COUNT
};

/** For a picker: `"item"`, `"male"`, … Asserts on an out-of-range variant. */
const char*
ev_obj_model_variant_name(enum EV_ObjModelVariant variant);

/**
 * One of an obj's model sets, recoloured, resized and lit as the client does.
 *
 * Returns NULL — not an error — when the record names no model for that
 * variant, which is the common case: most objs have no wear models and only
 * worn equipment has heads.
 */
struct ToriDraw_Model*
ev_build_obj_model(
    struct Tool_Dat2Cache* c,
    int obj_id,
    enum EV_ObjModelVariant variant);

/**
 * A loc's models for one shape, merged and transformed the way the scene
 * builder transforms them (world_scenery.u.c: recolour, mirror, resize,
 * offset, then scene lighting).
 *
 * `shape` is a `RSCache_Dat2LocShape`, or -1 for "whichever the record lists
 * first". A loc keeps a separate mesh per shape — a wall, its corner, its
 * diagonal — and asking for one it does not carry returns NULL rather than
 * quietly substituting another, because a wall drawn where a corner was asked
 * for looks like a correct answer.
 *
 * The placed rotation is NOT applied: nothing here is placed on a tile, so the
 * model is built in the record's own frame and the viewer orbits it. `mirrored`
 * IS applied, because at orientation 0 the scene builder applies it too.
 */
struct ToriDraw_Model*
ev_build_loc_model(
    struct Tool_Dat2Cache* c,
    int loc_id,
    int shape);

/**
 * The same two, built as HD models so their mapped textures can be drawn.
 *
 * Both return NULL for a subject that does not need one, exactly as
 * ev_build_npc_model_hd does — which is every OldSchool obj and loc. RS2-era
 * scenery is where it matters: a loc whose faces are cube-mapped comes out
 * untextured or invisible through the classic raster.
 */
struct ToriDraw_ModelHD*
ev_build_obj_model_hd(
    struct Tool_Dat2Cache* c,
    int obj_id,
    enum EV_ObjModelVariant variant);

struct ToriDraw_ModelHD*
ev_build_loc_model_hd(
    struct Tool_Dat2Cache* c,
    int loc_id,
    int shape);

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
