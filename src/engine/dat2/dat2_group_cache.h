#ifndef DAT2_GROUP_CACHE_H
#define DAT2_GROUP_CACHE_H

#include <stddef.h>

struct RSCache_Dat2DiskArchive;
struct RSCache_FileList;

/*
 * An LRU of *parsed* dat2 groups.
 *
 * The IO layer already keeps decompressed group archives in an LRU
 * (platform_x_io.c), which spares the bzip2 pass. What it does not spare is the
 * split: RSCache_FileListNewFromDecode walks the chunk table and mallocs one
 * buffer per member, so asking a config group for a single record costs one
 * allocation per record in the whole group. A boot that looks up sequences and
 * enums one id at a time paid that ~290,000 times.
 *
 * Caching the split turns "read one record out of a group" into a hash-free
 * array index, which is what makes lazy per-record decode affordable — without
 * it, the only way to keep group reads cheap was to decode every record in the
 * group up front and keep them all resident.
 *
 * Bounded by bytes, not entries: groups differ by two orders of magnitude in
 * size (a handful of KB for underlays, megabytes for locs), so a slot count
 * would either thrash on the big tables or hoard on the small ones.
 */

struct Dat2Group
{
    int table;
    int group;
    struct RSCache_FileList* filelist;
    /** Backing store for every member's bytes — `filelist->files[i]` point into
     *  it (RSCache_FileListNewFromDecodeShared). One block instead of one per
     *  member: the resident config groups hold ~160k members averaging tens of
     *  bytes, so per-member headers cost megabytes against the same payload. */
    char* blob;
    /* The archive's member ids, copied — a sharded group numbers its members
     * locally and they are not necessarily dense, so the id->index map cannot
     * be recovered from the file list alone. NULL when the archive had none,
     * which means index == id. */
    int* file_ids;
    int file_count;
    /** The archive's revision. Some record decoders pick their opcode set from
     *  it (spotanims), and the archive it came from is gone by then. */
    int revision;
    size_t bytes;
};

struct Dat2GroupCache;

struct Dat2GroupCache*
Dat2GroupCache_New(size_t budget_bytes);

void
Dat2GroupCache_Free(struct Dat2GroupCache* cache);

/* The parsed group, or NULL if it is not resident. A hit refreshes recency. */
struct Dat2Group const*
Dat2GroupCache_Get(
    struct Dat2GroupCache* cache,
    int table,
    int group);

/*
 * Split `archive` and keep the result. The archive stays the caller's to free —
 * the split copies every member's bytes out of it.
 *
 * Returns the resident parse, or NULL if the split failed. Re-putting a group
 * that is already resident returns the existing entry and re-parses nothing.
 */
struct Dat2Group const*
Dat2GroupCache_Put(
    struct Dat2GroupCache* cache,
    int table,
    int group,
    struct RSCache_Dat2DiskArchive* archive);

/*
 * A split made a bounded amount at a time, for a task on the time-sliced
 * background queue: the loc config group is 56,000 members, and its split in
 * one call was one 5.7 ms unit under UBSan (the editor session's
 * 'background task Dat2LocLoad ran 5.71 ms between two slice checks').
 *
 * One split per group is in flight, whoever started it: every task that
 * needs the group steps the same one (DAT2_GROUP_AWAIT_SLICED), so a
 * thousand loc loads that all missed the group split it once, together.
 * When it ends the group is resident, exactly as Dat2GroupCache_Put leaves
 * it (the split is RSCache_FileListSplit, of which the one-call split is the
 * run to its end).
 */

/** Entries (rscache_filelist.h, RSCache_FileListSplitStep) one step does. */
#define DAT2_GROUP_SPLIT_ENTRIES 4096

/* Is a split of (table, group) in flight? */
int
Dat2GroupCache_Splitting(
    struct Dat2GroupCache const* cache,
    int table,
    int group);

/*
 * The archive of (table, group), read by a task that found it neither
 * resident nor being split. The cache takes `archive` either way: it starts
 * the split from it, or frees it when the group became resident or a split
 * began while the read was out. A malformed group starts nothing.
 */
void
Dat2GroupCache_SplitOffer(
    struct Dat2GroupCache* cache,
    int table,
    int group,
    struct RSCache_Dat2DiskArchive* archive);

/*
 * One step (DAT2_GROUP_SPLIT_ENTRIES) of the split of (table, group): the
 * resident group once it has ended, else NULL -- with the split still in
 * flight, or gone (the group was malformed, or a Clear or Evict dropped it;
 * Dat2GroupCache_Splitting says which).
 */
struct Dat2Group const*
Dat2GroupCache_SplitStep(
    struct Dat2GroupCache* cache,
    int table,
    int group);

/* Member index for a member id, or -1. */
int
Dat2Group_IndexOf(
    struct Dat2Group const* group,
    int file_id);

/* Drop everything. The parses are pure derived data, so this is always safe —
 * the next lookup re-reads the group through the IO layer. */
void
Dat2GroupCache_Clear(struct Dat2GroupCache* cache);

/* Drop one group (a live reload rewrote its archive). Safe for the same reason
 * Clear is: no task keeps a Dat2Group across a suspension. Returns 1 when it
 * was resident. */
int
Dat2GroupCache_Evict(
    struct Dat2GroupCache* cache,
    int table,
    int group);

size_t
Dat2GroupCache_Bytes(struct Dat2GroupCache const* cache);

#endif
