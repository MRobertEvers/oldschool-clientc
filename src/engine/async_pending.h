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

/** One deferred element<->sequence binding (animation still loading). */
struct AsyncPendingSeqBind
{
    int element_id;
    int seq_id;
    /** World/client cycle on which the sequence was requested. Async loading
     *  must not reset a DynamicObject's clock when the bind lands. */
    int start_cycle;
};

/** Deferred binds. One server tick can animate a hundred locs on a sequence
 *  that is not resident yet (Xarpus' Hard ring: 98 pools on seq 8068), and
 *  every one of them waits here for the same load. */
#define ASYNC_PENDING_SEQ_BIND_MAX 1024

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
