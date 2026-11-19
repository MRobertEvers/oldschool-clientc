#ifndef SRC_ENGINE_SEQ_LOAD_TRACKER_H
#define SRC_ENGINE_SEQ_LOAD_TRACKER_H

/*
 * Which sequence loads have already been asked for.
 *
 * A sequence becomes resident only when its load lands, so between the
 * request and the landing a "is it in the scene?" test answers "missing" on
 * every pass. A tracker remembers the ids a caller has already queued a load
 * for, so the next pass does not queue another.
 *
 * The table is fixed at SEQ_LOAD_TRACKER_MAX ids and is never pruned. What a
 * full table does is the caller's choice of function, because the two users
 * want different things:
 *
 *  - SeqLoadTracker_Add records nothing once the table is full. A full table
 *    then stops deduping for new ids.
 *  - SeqLoadTracker_ClaimEvicting overwrites the oldest id instead, so the
 *    table stays bounded and keeps deduping. An evicted id that is still
 *    loading costs at most one more request.
 *
 * This does not know whether a sequence is resident, does not queue or create
 * a load, and does not know what a negative id means. The caller tests
 * residency first, passes only real ids, and queues the load itself.
 */

#include <stdbool.h>

#define SEQ_LOAD_TRACKER_MAX 64

struct SeqLoadTracker
{
    int seq_ids[SEQ_LOAD_TRACKER_MAX];
    int count;
};

/** True when `seq_id` is one of the ids recorded. */
bool
SeqLoadTracker_Has(
    struct SeqLoadTracker const* tracker,
    int seq_id);

/** Record `seq_id`; a full table records nothing. Does not check whether it
 *  is already recorded. */
void
SeqLoadTracker_Add(
    struct SeqLoadTracker* tracker,
    int seq_id);

/**
 * True when `seq_id` was not recorded, and records it: appended while there
 * is room, otherwise the oldest id is dropped and `seq_id` becomes the
 * newest. False, and nothing changes, when it is already recorded.
 */
bool
SeqLoadTracker_ClaimEvicting(
    struct SeqLoadTracker* tracker,
    int seq_id);

#endif /* SRC_ENGINE_SEQ_LOAD_TRACKER_H */
