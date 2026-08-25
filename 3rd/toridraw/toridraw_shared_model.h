#ifndef TORIDRAW_SHARED_MODEL_H
#define TORIDRAW_SHARED_MODEL_H

#include "toridraw_types.h"

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
 * Borrow the model published under `key`, or NULL if there is none.
 *
 * The caller becomes a holder and must eventually ToriDraw_ModelFree what it
 * gets back (a scene element does this for itself when the element is removed).
 * The result is shared: read it, place it, but do not write to it.
 */
struct ToriDraw_Model*
ToriDraw_SharedModelStoreAcquire(
    struct ToriDraw_SharedModelStore* store,
    int64_t key);

/**
 * Hand `model` to the store under `key` and borrow it straight back.
 *
 * Takes ownership; the returned pointer is the same one, now shared, with the
 * caller as its first holder. `key` must not already be published -- a caller
 * publishes only after Acquire came back empty.
 */
struct ToriDraw_Model*
ToriDraw_SharedModelStorePublish(
    struct ToriDraw_SharedModelStore* store,
    int64_t key,
    struct ToriDraw_Model* model);

/**
 * Lend `model` the face arrays that placements of the same loc have in common,
 * or make it the donor if it is the first to ask for `key`.
 *
 * For the locs that CANNOT share a whole model. A tree contoured to the ground
 * or a wall lit from its neighbour gets its own vertices and its own per-corner
 * colours at every placement, but the faces indexing those vertices are
 * identical -- and the faces are the larger half of a built loc model.
 *
 * Nothing is copied either way. The first placement has its face arrays cut out
 * into a donor shell that the store publishes and lends straight back; every
 * later one frees the arrays its own build just produced and points at the
 * donor's instead. The build runs in full regardless, because it is the build
 * that decides what the topology IS -- the saving is in what is kept, not in
 * what is done.
 *
 * `key` must live in its own namespace: a donor holds faces without vertices
 * and would be a corrupt answer to a whole-model Acquire.
 *
 * Takes no ownership. Returns `model`, now carrying a `borrowed_topology`.
 */
struct ToriDraw_Model*
ToriDraw_SharedModelStoreBorrowTopology(
    struct ToriDraw_SharedModelStore* store,
    int64_t key,
    struct ToriDraw_Model* model);

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
