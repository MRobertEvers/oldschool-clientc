#ifndef RSCACHE_REFERENCE_TABLE_H
#define RSCACHE_REFERENCE_TABLE_H

#include <stdbool.h>
#include <stdint.h>

struct RSCache_ReferenceTableArchiveFile
{
    int name_hash;
    int id;
};

struct RSCache_ReferenceTableArchive
{
    int index;
    int identifier;
    int crc;
    int hash;
    unsigned char whirlpool[64];
    int compressed;
    int uncompressed;
    int version;
    struct
    {
        struct RSCache_ReferenceTableArchiveFile* files;
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
};

#define RSCACHE_REFTABLE_FLAG_IDENTIFIERS 0x1
#define RSCACHE_REFTABLE_FLAG_WHIRLPOOL 0x2
#define RSCACHE_REFTABLE_FLAG_SIZES 0x4
#define RSCACHE_REFTABLE_FLAG_HASH 0x8

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

#endif
