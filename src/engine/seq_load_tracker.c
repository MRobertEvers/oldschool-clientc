#include "engine/seq_load_tracker.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

bool
SeqLoadTracker_Has(
    struct SeqLoadTracker const* tracker,
    int seq_id)
{
    assert(tracker);
    for( int i = 0; i < tracker->count; i++ )
        if( tracker->seq_ids[i] == seq_id )
            return true;
    return false;
}

void
SeqLoadTracker_Add(
    struct SeqLoadTracker* tracker,
    int seq_id)
{
    assert(tracker);
    if( tracker->count >= SEQ_LOAD_TRACKER_MAX )
        return;
    tracker->seq_ids[tracker->count++] = seq_id;
}

bool
SeqLoadTracker_ClaimEvicting(
    struct SeqLoadTracker* tracker,
    int seq_id)
{
    assert(tracker);
    if( SeqLoadTracker_Has(tracker, seq_id) )
        return false;
    if( tracker->count < SEQ_LOAD_TRACKER_MAX )
    {
        tracker->seq_ids[tracker->count++] = seq_id;
    }
    else
    {
        memmove(
            &tracker->seq_ids[0],
            &tracker->seq_ids[1],
            (size_t)(SEQ_LOAD_TRACKER_MAX - 1) * sizeof(tracker->seq_ids[0]));
        tracker->seq_ids[SEQ_LOAD_TRACKER_MAX - 1] = seq_id;
    }
    return true;
}
