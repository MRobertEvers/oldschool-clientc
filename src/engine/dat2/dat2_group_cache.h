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

/* Member index for a member id, or -1. */
int
Dat2Group_IndexOf(
    struct Dat2Group const* group,
    int file_id);

/* Drop everything. The parses are pure derived data, so this is always safe —
 * the next lookup re-reads the group through the IO layer. */
void
Dat2GroupCache_Clear(struct Dat2GroupCache* cache);

size_t
Dat2GroupCache_Bytes(struct Dat2GroupCache const* cache);

#endif
