#ifndef RSCACHE_REFERENCE_TABLE_H
#define RSCACHE_REFERENCE_TABLE_H

#include <stdbool.h>
#include <stdint.h>

/* The file id, and nothing else. The name hash that used to sit beside it is
 * now an array parallel to the ids on the archive, because only tables that
 * set FLAG_IDENTIFIERS ever had one: 97.4% of the 733k children in a shipped
 * cache are in tables that do not, and for those this second word was a zero
 * the resident tables paid megabytes to carry.
 * RSCache_ReferenceTableChildNameHash reads it back. */
struct RSCache_ReferenceTableArchiveFile
{
    int id;
};

/* One digest per archive, kept on the table now rather than here -- see
 * `whirlpools`. Inlining the 64 bytes made this struct 100 bytes; the pointer
 * that replaced them was still a word on every archive for a field no shipped
 * cache sets, which came to 10% of the resident archive bytes and every one of
 * them NULL. RSCache_ReferenceTableWhirlpool reads a digest back. */
#define RSCACHE_REFTABLE_WHIRLPOOL_BYTES 64

/*
 * Four of this struct's six ints have moved to id-indexed arrays on the table,
 * for the same reason the whirlpool did: they are answers about an archive that
 * most archives do not have, and a resident table pays for them on every slot
 * including its holes.
 *
 * `index` was a presence flag written as an int -- it held either the archive's
 * own id or -1, and every reader either tested the sign or used it as the id it
 * already had. It is a bit in `present` now. `identifier` is meaningful only
 * under FLAG_IDENTIFIERS and `compressed`/`uncompressed` only under FLAG_SIZES,
 * so each gets an array that stays NULL for a table whose flags do not ask for
 * it. Reaching them through the accessors is also the faster way to scan for a
 * name hash, which several loaders do over the whole table: a packed int array
 * walks one cache line per sixteen archives instead of one per two.
 */
struct RSCache_ReferenceTableArchive
{
    int crc;
    int version;
    struct
    {
        /* NULL with a positive `count` is an identity run -- child j has id j.
         * 45% of the children in a shipped cache are numbered that way and
         * store nothing at all; RSCache_ReferenceTableChildId answers for both
         * shapes, and is the only thing that may index this. */
        struct RSCache_ReferenceTableArchiveFile* files;
        /* One name hash per child, parallel to the ids. NULL unless the table
         * sets FLAG_IDENTIFIERS, and a NULL reads as all-zero -- which is what
         * the pool used to hold for those tables anyway.
         *
         * Owned independently of `files`, which it used to track: an identity
         * run has no `files` at all and still has these. Each array answers to
         * its own pooled test, and the free paths ask separately. */
        int* name_hashes;
        int count;
    } children;
};

struct RSCache_ReferenceTable
{
    int format;
    int version;
    int flags;
    int id_count;
    int* ids;
    struct RSCache_ReferenceTableArchive* archives;
    int archive_count;

    /*
     * Allocation bookkeeping. Not part of the encoded form — the encoder walks
     * id_count/archive_count and never sees these.
     *
     * They exist because growing a table one archive at a time reallocated both
     * arrays to exactly count+1 on every insert. Packing a tree with no base
     * cache adds every archive that way, and `models` alone is tens of
     * thousands, so the copying was quadratic in bytes. A decoded table leaves
     * these at 0, which the growth code reads as "capacity is whatever count
     * says" and takes over from there.
     */
    int id_capacity;
    int archive_capacity;

    /*
     * Decode pools every archive's stored children into this one block —
     * `archives[].children.files` are slices of it. Identity runs are not in
     * here: they get no slice, and no bytes. One allocation instead of
     * one per archive: the resident tables carry ~139k child arrays averaging a
     * few dozen bytes, so per-array headers rivalled the payload. A tool that
     * replaces an archive's children mallocs a fresh array and leaves the dead
     * slice in the pool; RSCache_ReferenceTableChildrenPooled tells the two
     * ownerships apart, and the free paths consult it. NULL for a table built
     * by hand (those allocate per archive, as ever).
     */
    struct RSCache_ReferenceTableArchiveFile* children_pool;
    size_t children_pool_count;

    /* Name hashes, one per child, allocated only for a table that sets
     * FLAG_IDENTIFIERS. NULL otherwise, which every reader takes as "no child
     * here is named".
     *
     * Sized to every child and offset independently of children_pool, which the
     * two no longer share: an identity run stores no ids but still names each
     * of its files, so the id pool skips it and this one does not. */
    int* children_name_hash_pool;
    size_t children_name_hash_pool_count;

    /*
     * Whirlpool digests, RSCACHE_REFTABLE_WHIRLPOOL_BYTES each, indexed by
     * archive id -- not by position in `ids`, so a writer that holds an archive
     * id can reach its slot without searching. NULL until something asks for a
     * slot, which nothing does unless the table sets FLAG_WHIRLPOOL.
     *
     * `whirlpool_count` is the pool's own length, tracked apart from
     * archive_count so that growing the table does not have to remember this
     * array exists: the next RSCache_ReferenceTableWhirlpoolSlot notices it is
     * short and extends it.
     */
    unsigned char* whirlpools;
    int whirlpool_count;

    /*
     * One bit per archive id: set when the table lists that archive, clear for
     * a hole. Replaces the per-archive `index` int, which cost 4 bytes to carry
     * the same one bit. Sized like the whirlpool pool -- lazily, against
     * archive_count -- so table growth needs no bookkeeping here.
     */
    unsigned char* present;
    int present_count;

    /*
     * Archive name hashes, indexed by archive id. NULL unless the table sets
     * FLAG_IDENTIFIERS, and a NULL reads as "nothing here is named", which is
     * the zero those tables stored before.
     */
    int* identifiers;
    int identifier_count;

    /*
     * Compressed and uncompressed container lengths, indexed by archive id.
     * Both NULL unless the table sets FLAG_SIZES; they are decoded, encoded and
     * grown as a pair, so one count covers them.
     */
    int* compressed_sizes;
    int* uncompressed_sizes;
    int size_count;
};

#define RSCACHE_REFTABLE_FLAG_IDENTIFIERS 0x1
#define RSCACHE_REFTABLE_FLAG_WHIRLPOOL 0x2
#define RSCACHE_REFTABLE_FLAG_SIZES 0x4
#define RSCACHE_REFTABLE_FLAG_HASH 0x8

/**
 * Whether the table lists `archive_id` — the replacement for the old
 * `archives[id].index >= 0` test. Out-of-range ids read as absent, which is
 * what a bounds check followed by that test used to mean.
 */
bool
RSCache_ReferenceTableHasArchive(
    const struct RSCache_ReferenceTable* table,
    int archive_id);

/** Mark `archive_id` present or absent. Allocates the bitmap on first use. */
void
RSCache_ReferenceTableSetHasArchive(
    struct RSCache_ReferenceTable* table,
    int archive_id,
    bool present);

/** Archive name hash, or 0 when the table carries none (no FLAG_IDENTIFIERS). */
int
RSCache_ReferenceTableIdentifier(
    const struct RSCache_ReferenceTable* table,
    int archive_id);

/** Set the archive name hash. Allocates the array on first use. */
void
RSCache_ReferenceTableSetIdentifier(
    struct RSCache_ReferenceTable* table,
    int archive_id,
    int identifier);

/** Container lengths, 0 when the table carries none (no FLAG_SIZES). */
int
RSCache_ReferenceTableCompressed(
    const struct RSCache_ReferenceTable* table,
    int archive_id);

int
RSCache_ReferenceTableUncompressed(
    const struct RSCache_ReferenceTable* table,
    int archive_id);

/** Set both container lengths. Allocates the arrays on first use. */
void
RSCache_ReferenceTableSetSizes(
    struct RSCache_ReferenceTable* table,
    int archive_id,
    int compressed,
    int uncompressed);

struct RSCache_ReferenceTable*
RSCache_ReferenceTableNewDecode(
    char* data,
    int data_size);

/**
 * Encode a reference table back to its on-disk bytes.
 *
 * Exact inverse of the decoder, including the field *order*, which is not the
 * order the struct declares: identifiers, crcs, whirlpools, sizes, versions,
 * child counts, child ids, child identifiers. Only the ids listed in
 * `table->ids` are written, in that order, since that is what the decoder walks.
 *
 * Formats 5, 6 and 7 are all supported — the local caches use all three (modern
 * OSRS is 7, cache.643 is 6, kronos mixes 5 and 6). Format 7 replaces every
 * count and delta with a usmart.
 *
 * Returns bytes written to `out`, or 0 on failure (bad format, or `out` too
 * small). Ask RSCache_ReferenceTableEncodeBound for a safe size.
 */
uint32_t
RSCache_ReferenceTableEncode(
    const struct RSCache_ReferenceTable* table,
    uint8_t* out,
    uint32_t out_capacity);

/** Safe `out_capacity` for RSCache_ReferenceTableEncode. */
uint32_t
RSCache_ReferenceTableEncodeBound(const struct RSCache_ReferenceTable* table);

void
RSCache_ReferenceTableFree(struct RSCache_ReferenceTable* table);

/** True when `files` is a slice of `table`'s pooled children block. A pooled
 *  slice must never be freed or realloc'd — replace it and let the table free
 *  the pool. `files` NULL answers false. */
bool
RSCache_ReferenceTableChildrenPooled(
    const struct RSCache_ReferenceTable* table,
    const struct RSCache_ReferenceTableArchiveFile* files);

/** True when `name_hashes` is a slice of `table`'s pooled name-hash block. The
 *  same rule as RSCache_ReferenceTableChildrenPooled, asked separately because
 *  the two pools no longer share offsets. `name_hashes` NULL answers false. */
bool
RSCache_ReferenceTableChildNameHashesPooled(
    const struct RSCache_ReferenceTable* table,
    const int* name_hashes);

/** The name hash of `archive`'s child `child_index`, or 0 when the archive
 *  carries no names -- which is every archive in a table without
 *  FLAG_IDENTIFIERS. */
int
RSCache_ReferenceTableChildNameHash(
    const struct RSCache_ReferenceTableArchive* archive,
    int child_index);

/** The id of `archive`'s child `child_index`.
 *
 *  The only correct way to read a child id: an identity run stores no array at
 *  all, and answering `child_index` for it is the whole point. Indexing
 *  `children.files` directly reads NULL for 45% of the children in a shipped
 *  cache. */
int
RSCache_ReferenceTableChildId(
    const struct RSCache_ReferenceTableArchive* archive,
    int child_index);

/** `archive_id`'s whirlpool digest, RSCACHE_REFTABLE_WHIRLPOOL_BYTES bytes, or
 *  NULL when the table carries none for it -- which is every archive of every
 *  cache in this repo, none of which set FLAG_WHIRLPOOL. */
const unsigned char*
RSCache_ReferenceTableWhirlpool(
    const struct RSCache_ReferenceTable* table,
    int archive_id);

/** `archive_id`'s whirlpool slot, writable, allocating or extending the table's
 *  pool to fit. RSCACHE_REFTABLE_WHIRLPOOL_BYTES bytes, zeroed when first
 *  created. For the encoders that compute digests; readers want
 *  RSCache_ReferenceTableWhirlpool. */
unsigned char*
RSCache_ReferenceTableWhirlpoolSlot(
    struct RSCache_ReferenceTable* table,
    int archive_id);

#endif
