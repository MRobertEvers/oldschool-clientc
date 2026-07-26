#ifndef RSCACHE_TOOLS_ASSET_ACCESS_H
#define RSCACHE_TOOLS_ASSET_ACCESS_H

#include "rscache.h"

#include <stdbool.h>

/* ---- Owned blob helpers ------------------------------------------------- */

struct Tool_Bytes
{
    uint8_t* data;
    int size;
};

void
tool_bytes_free(struct Tool_Bytes* b);

/* ---- Dat2 open helpers -------------------------------------------------- */

struct Tool_Dat2Cache
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
};

int
tool_dat2_open(
    const char* cache_dir,
    const struct RSCache* profile,
    struct Tool_Dat2Cache* out);

void
tool_dat2_close(struct Tool_Dat2Cache* c);

/* ---- Dat1 open helpers -------------------------------------------------- */

struct Tool_Dat1Cache
{
    struct RSCache profile;
    struct RSCache_Dat1Disk* disk;
    struct RSCache_FileListDat* configs; /* configs jagfile (archive 2), nullable */
};

int
tool_dat1_open(
    const char* cache_dir,
    const struct RSCache* profile,
    struct Tool_Dat1Cache* out);

void
tool_dat1_close(struct Tool_Dat1Cache* c);

/* ---- NPC ---------------------------------------------------------------- */

/** Decode one dat2 NPC. Caller frees with RSCache_Dat2ConfigNpcFree. */
struct RSCache_Dat2ConfigNpc*
tool_dat2_npc_load(
    struct Tool_Dat2Cache* c,
    int npc_id);

/** Decode one dat1 NPC. Caller frees with RSCache_Dat1ConfigNpcFree. */
struct RSCache_Dat1ConfigNpc*
tool_dat1_npc_load(
    struct Tool_Dat1Cache* c,
    int npc_id);

/* ---- Sequence ----------------------------------------------------------- */

struct RSCache_Dat2ConfigSequence*
tool_dat2_seq_load(
    struct Tool_Dat2Cache* c,
    int seq_id);

/** Load all sequences from a dat2 cache. Caller frees each entry and the array. */
int
tool_dat2_seq_load_all(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2ConfigSequence*** out_seqs,
    int** out_ids,
    int* out_count);

struct RSCache_Dat1ConfigSeqList*
tool_dat1_seq_list_load(struct Tool_Dat1Cache* c);

/* ---- BasType ------------------------------------------------------------ */

struct RSCache_Dat2ConfigBas*
tool_dat2_bas_load(
    struct Tool_Dat2Cache* c,
    int bas_id);

/* ---- Frames / framemap -------------------------------------------------- */

/**
 * Resolve the framemap id of a sequence's first usable frame.
 * Returns -1 on failure. Optional warnings go to stderr when verbose!=0.
 */
int
tool_dat2_seq_framemap_id(
    struct Tool_Dat2Cache* c,
    const struct RSCache_Dat2ConfigSequence* seq,
    int verbose);

/** Load a framemap by id. Caller frees with RSCache_Dat2FramemapFree. */
struct RSCache_Dat2Framemap*
tool_dat2_framemap_load(
    struct Tool_Dat2Cache* c,
    int framemap_id);

/**
 * Find the file position of `file_id` inside an archive's file_ids[].
 * Returns -1 if not found. When file_ids is NULL, returns file_id if in range.
 */
int
tool_archive_file_position(
    const struct RSCache_Dat2DiskArchive* archive,
    int file_id);

/* ---- Model -------------------------------------------------------------- */

struct RSCache_Model*
tool_dat2_model_load(
    struct Tool_Dat2Cache* c,
    int model_id);

struct RSCache_Model*
tool_dat1_model_load(
    struct Tool_Dat1Cache* c,
    int model_id);

/* ---- Raw archive bytes -------------------------------------------------- */

/** Load decompressed archive bytes (ownership transferred). */
int
tool_dat2_archive_bytes(
    struct Tool_Dat2Cache* c,
    int table_id,
    int archive_id,
    struct Tool_Bytes* out);

#endif
