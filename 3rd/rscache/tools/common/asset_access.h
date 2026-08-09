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

/*
 * Create an empty dat2 cache at cache_dir, so a pack with no --base has
 * somewhere to write.
 *
 * Everything needed to grow a cache from nothing was already here:
 * RSCache_Dat2DiskWriteArchive creates main_file_cache.dat2 and each
 * main_file_cache.idxN on demand, and Dat2EditCommit builds and writes a
 * reference table for every table it touches (dat2_edit_ensure_table). The one
 * gap was the front door -- RSCache_Dat2DiskNewFromDirectory opens the .dat2
 * with "rb+" and fails when it does not exist, so a from-scratch pack could
 * never get past the open.
 *
 * This writes the reserved sector 0 that WriteArchive would have written, for
 * the same reason: the index reader treats sector 0 as "absent", so an archive
 * placed there would be invisible.
 *
 * Returns 1 on success, 0 on failure. Succeeds if a cache is already there.
 */
int
tool_dat2_create(const char* cache_dir);

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

/**
 * Decode one dat2 NPC and report whether the record decoded byte-exactly.
 *
 * `out_exact` is 1 only when the decoder consumed the whole record. It is 0
 * when the decoder stopped early on an opcode it does not know, which leaves
 * every field after that opcode either unset or filled from payload bytes. A
 * revision with a known decode gap still has records that decode exactly, so
 * this is the per-record question worth asking; refusing a whole revision
 * refuses those too.
 */
struct RSCache_Dat2ConfigNpc*
tool_dat2_npc_load_checked(
    struct Tool_Dat2Cache* c,
    int npc_id,
    int* out_exact);

/** Decode one dat1 NPC. Caller frees with RSCache_Dat1ConfigNpcFree. */
struct RSCache_Dat1ConfigNpc*
tool_dat1_npc_load(
    struct Tool_Dat1Cache* c,
    int npc_id);

/**
 * Every id present for a dat2 config type.
 *
 * Ids come from the archive's own file table rather than a 0..max walk: config
 * groups are sparse once a revision has removed content, and walking would
 * decode a lot of absent records and report their misses as failures.
 */
int
tool_dat2_config_ids(
    struct Tool_Dat2Cache* c,
    enum RSCache_Type type,
    int config_kind,
    int** out_ids,
    int* out_count);

/**
 * Model ids of every identikit body part.
 *
 * One kit per body part: these are the player body itself in its rest pose, and
 * between them they cover every joint an animation can address, which makes them
 * the sample to use when comparing one era's rig against another's. Taking every
 * kit instead would pile every hairstyle and beard in the game into one mesh.
 */
int
tool_dat2_idk_models(
    struct Tool_Dat2Cache* c,
    int** out_ids,
    int* out_count);

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
 * Decode one animation frame, by the packed id a sequence config carries.
 *
 * `packed_frame_id` is `archive << 16 | file`, which is how `frame_ids` stores
 * it. `framemap` is the rig the frame is read against — the decoder needs it to
 * know how many transforms each entry has — and is usually
 * `tool_dat2_framemap_load(tool_dat2_seq_framemap_id(...))`.
 *
 * Caller frees with RSCache_Dat2FrameFree. Returns NULL when the archive, the
 * file or the decode fails, which callers should treat as "this frame holds"
 * rather than dropping it: dropping shortens the sequence and moves every later
 * frame's timing.
 */
struct RSCache_Dat2Frame*
tool_dat2_frame_load(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2Framemap* framemap,
    int packed_frame_id);

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
