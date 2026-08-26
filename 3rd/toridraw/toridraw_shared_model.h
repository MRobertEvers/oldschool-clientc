#ifndef TORIDRAW_SHARED_MODEL_H
#define TORIDRAW_SHARED_MODEL_H

#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * One built model standing in for every placement of the same thing.
 *
 * A scene puts the same fence, tree or crate down hundreds of times, and for a
 * loc whose geometry does not depend on where it stands the built, lit result
 * is identical at every one of them. Giving each placement its own copy made
 * the model arrays the largest pool in the process by a wide margin.
 *
 * `struct ToriDraw_SharedModel` is the owner of such a model and the only
 * owner it has. It is deliberately opaque and deliberately unreachable except
 * through this store: nothing outside can construct one, count its holders, or
 * free the model out from under the scene. A placement never holds a
 * ToriDraw_SharedModel either -- it holds the plain
 * `struct ToriDraw_Model*` it always held, whose `shared_owner` points back
 * here. That back-pointer is what makes the arrangement safe rather than
 * merely economical:
 *
 *   - ToriDraw_ModelFree on a borrowed model drops a holder instead of freeing
 *     geometry the rest of the scene is still drawing;
 *   - ToriDraw_ModelAssertWritable refuses an in-place write to it, so the one
 *     class of bug this design can cause -- every fence in the county moving,
 *     fading or animating together -- aborts at its cause in a debug build
 *     rather than surfacing as a rendering mystery;
 *   - ToriDraw_SceneElementModelForWrite is the single door that legitimately
 *     writes to a placed model, and it un-borrows first.
 *
 * The store holds no models of its own: an entry lives exactly as long as some
 * placement holds it, and drops out when the last one lets go. There is
 * therefore no budget to tune and nothing retained across a rebuild that the
 * rebuild does not re-place.
 */
struct ToriDraw_SharedModel;
struct ToriDraw_SharedModelStore;

struct ToriDraw_SharedModelStore*
ToriDraw_SharedModelStoreNew(void);

/** Frees the store. Every model it lends must have been released first. */
void
ToriDraw_SharedModelStoreFree(struct ToriDraw_SharedModelStore* store);

/**
 * Borrow the model published under `key`, as a handle that says it is shared.
 *
 * A handle and not a bare pointer, because "shared" is the only interesting
 * thing about what comes back and a `struct ToriDraw_Model*` cannot carry it:
 * the result reads and places exactly like a model the caller built, and the
 * one thing the caller must not do to it -- write -- looks identical in the
 * source either way. The returned handle is TORIDRAWMO_SHARED, so every
 * accessor that gates a write refuses it and
 * ToriDraw_SceneElementSetModel cannot be told otherwise.
 *
 * TORIDRAWMK_NONE when there is no such key. The caller becomes a holder and
 * must eventually ToriDraw_ModelFree the model (a scene element does this for
 * itself when the element is removed).
 */
struct ToriDraw_ModelHandle
ToriDraw_SharedModelStoreAcquire(
    struct ToriDraw_SharedModelStore* store,
    int64_t key);

/**
 * Hand `model` to the store under `key` and borrow it straight back.
 *
 * Takes ownership. The returned handle addresses the same object, now shared,
 * with the caller as its first holder -- and says TORIDRAWMO_SHARED, because
 * from this call on the caller is a reader of geometry it no longer owns.
 * `key` must not already be published; a caller publishes only after Acquire
 * came back TORIDRAWMK_NONE.
 */
struct ToriDraw_ModelHandle
ToriDraw_SharedModelStorePublish(
    struct ToriDraw_SharedModelStore* store,
    int64_t key,
    struct ToriDraw_Model* model);

/**
 * The lendable face buffers, as a thing that can be owned.
 *
 * For the locs that CANNOT share a whole model. A tree contoured to the ground
 * or a wall lit from its neighbour gets its own vertices and its own per-corner
 * colours at every placement, but the faces indexing those vertices are
 * identical -- and the faces are the larger half of a built loc model.
 *
 * This used to be a `struct ToriDraw_Model` with `vertex_count == 0` standing
 * in as a donor, which is a model that would be a corrupt answer to every
 * question a model is asked. It is its own type now, and an opaque one: the
 * arrays it owns are named in its definition and reached nowhere else. A
 * borrowing placement carries plain aliases to them, as it always did, so the
 * render path is unchanged.
 *
 * Note which arrays these are: TORIDRAW_SHARED_FACE_FIELDS, which excludes
 * face_infos on purpose. See that macro for why.
 */
struct ToriDraw_SharedFaces;
struct ToriDraw_SharedFacesStore;

struct ToriDraw_SharedFacesStore*
ToriDraw_SharedFacesStoreNew(void);

/** Frees the store. Every buffer set it lends must have been released first. */
void
ToriDraw_SharedFacesStoreFree(struct ToriDraw_SharedFacesStore* store);

/** Live buffer sets, for the world-build census. */
int
ToriDraw_SharedFacesStoreCount(const struct ToriDraw_SharedFacesStore* store);

/**
 * Point `model` at the face buffers published under `key`, or publish its own
 * and become the first lender.
 *
 * Nothing is copied either way. The first placement has its face arrays moved
 * into a buffer set that the store owns and keeps aliases to them; every later
 * one frees the arrays its own build just produced and adopts the published
 * ones. The build runs in full regardless, because it is the build that decides
 * what the topology IS -- the saving is in what is kept, not in what is done.
 *
 * Takes no ownership of `model`. Returns a handle onto it, now carrying a
 * `shared_faces` and saying TORIDRAWMO_LENT_FACES -- which is the half-shared
 * permission: write the vertices, the per-corner colours and face_infos, not
 * the twelve arrays now on loan.
 */
struct ToriDraw_ModelHandle
ToriDraw_SharedFacesStoreBorrow(
    struct ToriDraw_SharedFacesStore* store,
    int64_t key,
    struct ToriDraw_Model* model);

/**
 * Drop one lender of a buffer set, freeing it at the last.
 *
 * Callers reach this through ToriDraw_ModelFree, which routes any model
 * carrying a `shared_faces` here; it is exposed for that one use.
 */
void
ToriDraw_SharedFacesRelease(struct ToriDraw_SharedFaces* faces);

/** Live entries, for the world-build census. */
/** Live entries, for the world-build census. */
int
ToriDraw_SharedModelStoreCount(const struct ToriDraw_SharedModelStore* store);

/**
 * Drop one holder of a borrowed model, freeing it at the last.
 *
 * Callers reach this through ToriDraw_ModelFree, which routes any model
 * carrying a `shared_owner` here; it is exposed for that one use.
 */
void
ToriDraw_SharedModelRelease(struct ToriDraw_SharedModel* shared);

#endif /* TORIDRAW_SHARED_MODEL_H */
