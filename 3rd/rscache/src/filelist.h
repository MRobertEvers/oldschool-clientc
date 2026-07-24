#ifndef RSCACHE_FILELIST_H
#define RSCACHE_FILELIST_H

#include <stdbool.h>
#include <stdint.h>

struct RSCache_FileList
{
    char** files;
    int* file_sizes;
    int file_count;
};

struct RSCache_FileList*
RSCache_FileListNewFromDecode(
    char* data,
    int data_size,
    int num_files);

/**
 * Encode a dat2 group: the member payloads followed by the size table.
 *
 * Layout mirrored from the decoder:
 *   - A single-file group is just that file's bytes, with no table and no
 *     trailer. The decoder special-cases `num_files == 1` and would otherwise
 *     read the last byte as a chunk count.
 *   - Otherwise: all payloads concatenated, then `chunks * file_count` big-endian
 *     int32 deltas, then a one-byte chunk count.
 *
 * Always writes a single chunk. Multi-chunk groups exist (Jagex uses them to
 * interleave updates) and decode correctly, but one chunk is the simplest valid
 * encoding and what a freshly packed group wants.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_FileListEncode(
    const struct RSCache_FileList* filelist,
    uint8_t* out,
    uint32_t out_capacity);

/** Safe `out_capacity` for RSCache_FileListEncode. */
uint32_t
RSCache_FileListEncodeBound(const struct RSCache_FileList* filelist);

void
RSCache_FileListFree(struct RSCache_FileList* filelist);
// Refered to as "JagFile" in many rsps codebases.
struct RSCache_FileListDat
{
    char** files;
    int* file_sizes;
    int* file_name_hashes;
    int file_count;
};

struct RSCache_FileListDat*
RSCache_FileListDatNewFromDecode(
    char* data,
    int data_size);

/**
 * Encode a dat1 jagfile.
 *
 * The format offers two compression arrangements and **no stored mode** — this is
 * why authoring a jagfile requires a bzip2 encoder at all:
 *
 *   - Whole-archive: header sizes differ, the entire body (file table included)
 *     is one bzip2 stream, and each file's bytes sit in it uncompressed.
 *   - Per-file: header sizes are equal, the file table is plain, and every file
 *     is individually bzip2 compressed.
 *
 * `whole_archive` picks between them. Whole-archive compresses better on the many
 * small files a config jagfile holds; per-file lets a reader extract one file
 * without inflating everything.
 *
 * Note the jagfile carries only name *hashes*, not names — so a round trip
 * preserves lookup by name but cannot recover the original strings.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_FileListDatEncode(
    const struct RSCache_FileListDat* filelist,
    bool whole_archive,
    uint8_t* out,
    uint32_t out_capacity);

/** Safe `out_capacity` for RSCache_FileListDatEncode. */
uint32_t
RSCache_FileListDatEncodeBound(const struct RSCache_FileListDat* filelist);

void
RSCache_FileListDatFree(struct RSCache_FileListDat* filelist);

int
RSCache_FileListDatFindFileByName(
    struct RSCache_FileListDat* filelist,
    const char* name);

/** ".dat" + ".idx" files inside the config table config archive. */
struct RSCache_FileListDatIndexed
{
    char* data;
    int data_size;

    int* offsets;
    int offset_count;
};

struct RSCache_FileListDatIndexed*
RSCache_FileListDatIndexedNewFromDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

/**
 * Encode the ".idx" side of a ".dat"/".idx" pair: a u16 entry count followed by
 * one u16 per entry.
 *
 * The ".dat" side is `filelist->data` verbatim — it has no structure of its own,
 * which is why only the index needs encoding.
 *
 * The on-disk offsets are u16 *lengths* accumulated by the decoder into absolute
 * offsets, so this writes the successive differences back out. Returns 0 if any
 * entry's length exceeds 65535, which the format cannot express.
 */
uint32_t
RSCache_FileListDatIndexedEncodeIndex(
    const struct RSCache_FileListDatIndexed* filelist,
    uint8_t* out,
    uint32_t out_capacity);

/** Safe `out_capacity` for RSCache_FileListDatIndexedEncodeIndex. */
uint32_t
RSCache_FileListDatIndexedEncodeIndexBound(const struct RSCache_FileListDatIndexed* filelist);

void
RSCache_FileListDatIndexedFree(struct RSCache_FileListDatIndexed* filelist);

#endif
