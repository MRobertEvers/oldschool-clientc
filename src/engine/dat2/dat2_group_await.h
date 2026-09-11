#ifndef DAT2_GROUP_AWAIT_H
#define DAT2_GROUP_AWAIT_H

#include "asyncio.h"
#include "engine/dat2/dat2_group_cache.h"

#include <rscache.h>

/*
 * The await side of the split-group LRU (dat2_group_cache.h).
 *
 * Every record load has the same shape — resolve (table, group), read the
 * group, find the member — and the only part that has to live in the task is
 * the yield, because a protothread cannot suspend inside a called function.
 * So the lookup is a macro and everything else is a plain function.
 *
 * Both addressing schemes reach the same place: OSRS keeps records in one
 * config group, addressed (RSCACHE_DAT2_TABLE_CONFIGS, config_kind); RS2
 * shards them into their own table, addressed (table, id >> shift). Those are
 * both (table, group) pairs, so one cache key covers them.
 */

static inline struct RSCache_Dat2DiskArchive*
Dat2Group_TakeArchive(
    struct ToriRS_IO* io,
    int slot)
{
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot);
    struct RSCache_Dat2DiskArchive* archive =
        (struct RSCache_Dat2DiskArchive*)item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

/*
 * Resolve a split group into `out_`, reading it through `io_` only on a miss.
 *
 * Expands a PT_YIELD, so it belongs at protothread top level like any other
 * await, and `out_` must be a field on the task rather than a local — on resume
 * control lands inside the miss branch, and anything on the stack is gone.
 * `out_` is left NULL when the group cannot be read.
 */
#define DAT2_GROUP_AWAIT(pt_, io_, slot_, cache_, table_, group_, out_)         \
    do                                                                         \
    {                                                                          \
        (out_) = Dat2GroupCache_Get((cache_), (table_), (group_));              \
        if( !(out_) )                                                          \
        {                                                                      \
            ToriRS_IO_QueueCache(                                              \
                (io_), (slot_), 0, (table_), (group_), TORIRS_IO_CACHE_DAT2);   \
            PT_YIELD(pt_);                                                     \
            {                                                                  \
                struct RSCache_Dat2DiskArchive* dat2_group_archive_ =          \
                    Dat2Group_TakeArchive((io_), (slot_));                      \
                if( dat2_group_archive_ )                                      \
                {                                                              \
                    (out_) = Dat2GroupCache_Put(                               \
                        (cache_), (table_), (group_), dat2_group_archive_);    \
                    RSCache_Dat2DiskArchiveFree(dat2_group_archive_);          \
                }                                                              \
            }                                                                  \
        }                                                                      \
    } while( 0 )

#endif
