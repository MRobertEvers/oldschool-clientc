#ifndef SRC_ENGINE_ASYNC_PENDING_H
#define SRC_ENGINE_ASYNC_PENDING_H

/**
 * The two "asked for it, waiting for it" lists the client keeps per frame.
 *
 * Both have the same shape and the same three failure modes, which is why they
 * are together: a bounded list, added to when something is requested, walked
 * once a frame, and compacted to whatever is still in flight.
 *
 *   - DUPLICATES. A model rebuilt while its first request is in flight asks
 *     again. Queueing a second decoder per rebuild is pure waste, and the old
 *     order deduplicated the publish list afterwards instead of the request.
 *   - OVERFLOW. The list is a fixed array because the ceiling is what a frame
 *     may not exceed, not what one costs. Past it, a request is dropped, and
 *     the drop has to be visible rather than silent.
 *   - STALE ENTRIES. Scene element ids are RECYCLED. An entry left behind by a
 *     despawned entity is indistinguishable from a valid one once the id is
 *     handed out again, and its sequence then binds onto whoever inherited it
 *     -- the wrong creature suddenly playing somebody else's animation.
 *     Dropping at the moment of death removes the ambiguity instead of trying
 *     to detect it afterwards.
 *
 * What is NOT here is the policy: which loads to queue, when something counts
 * as resident, and when absence becomes a terminal failure. Those need the
 * provider, the bridge and the task runner, and belong to the caller that has
 * them.
 */

#include <stdbool.h>

/** Texture ids awaited. 512 is well past what one scene build requests. */
#define ASYNC_PENDING_TEXTURE_MAX 512

struct AsyncPendingTextures
{
    int ids[ASYNC_PENDING_TEXTURE_MAX];
    int count;
};

/** Is this id already awaited? */
bool
AsyncPendingTextures_Has(
    struct AsyncPendingTextures const* pending,
    int texture_id);

/**
 * Await `texture_id`, unless it is already awaited or the list is full.
 *
 * Returns false when it was NOT added, and the caller must say so: a dropped
 * texture request is a model that renders untextured forever, with nothing
 * else in the frame to notice it.
 */
bool
AsyncPendingTextures_Add(
    struct AsyncPendingTextures* pending,
    int texture_id);

/** Keep only the first `kept` entries, which the caller has compacted into
 *  place while walking the list. */
void
AsyncPendingTextures_Keep(
    struct AsyncPendingTextures* pending,
    int kept);

/** One deferred element<->sequence binding (animation still loading). */
struct AsyncPendingSeqBind
{
    int element_id;
    int seq_id;
    /** World/client cycle on which the sequence was requested. Async loading
     *  must not reset a DynamicObject's clock when the bind lands. */
    int start_cycle;
};

/** 64 deferred binds. A scene rebuild requests far fewer at once; past this a
 *  bind is dropped and the element simply does not animate. */
#define ASYNC_PENDING_SEQ_BIND_MAX 64

struct AsyncPendingSeqBinds
{
    struct AsyncPendingSeqBind items[ASYNC_PENDING_SEQ_BIND_MAX];
    int count;
};

/**
 * Defer a bind until its sequence load lands. False when the list is full.
 *
 * Duplicates are NOT rejected here, unlike the texture list: the same element
 * can legitimately be re-bound to a different sequence, and re-bound to the
 * same one at a later cycle, and both entries mean something. The poll drops
 * whichever binds first.
 */
bool
AsyncPendingSeqBinds_Add(
    struct AsyncPendingSeqBinds* pending,
    int element_id,
    int seq_id,
    int start_cycle);

/**
 * Forget every deferred bind for an element that is going away.
 *
 * The poll can only ask whether an element is LIVE, and scene element ids are
 * recycled -- so this must be called at the moment of death, not left for the
 * poll to work out. @see the note at the top of this header.
 */
void
AsyncPendingSeqBinds_DropElement(
    struct AsyncPendingSeqBinds* pending,
    int element_id);

/** Keep only the first `kept` entries, compacted into place by the caller. */
void
AsyncPendingSeqBinds_Keep(
    struct AsyncPendingSeqBinds* pending,
    int kept);

#endif /* SRC_ENGINE_ASYNC_PENDING_H */
