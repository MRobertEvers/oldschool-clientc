#ifndef RSCACHE_TOOLS_ANIM_AFFINITY_H
#define RSCACHE_TOOLS_ANIM_AFFINITY_H

#include "asset_access.h"

#include <stdint.h>

/* ---- Seed animation set from an NPC ------------------------------------- */

struct Tool_AnimSeeds
{
    int* seq_ids;
    int count;
    int capacity;
    int bas_type_id; /* -1 if none */
};

void
tool_anim_seeds_init(struct Tool_AnimSeeds* s);

void
tool_anim_seeds_free(struct Tool_AnimSeeds* s);

int
tool_anim_seeds_add(
    struct Tool_AnimSeeds* s,
    int seq_id);

/** Collect seed sequence ids from a dat2 NPC (+ BasType when present). */
int
tool_dat2_npc_anim_seeds(
    struct Tool_Dat2Cache* c,
    const struct RSCache_Dat2ConfigNpc* npc,
    struct Tool_AnimSeeds* out);

/** Collect seed sequence ids from a dat1 NPC. */
int
tool_dat1_npc_anim_seeds(
    const struct RSCache_Dat1ConfigNpc* npc,
    struct Tool_AnimSeeds* out);

/* ---- Framemap reverse index (dat2) -------------------------------------- */

struct Tool_FramemapIndexEntry
{
    int seq_id;
    int framemap_id;
    int frame_count;
};

struct Tool_FramemapIndex
{
    struct Tool_FramemapIndexEntry* entries;
    int count;
    /* Memo: archive_id -> framemap_id of first file (or -1). */
    int* archive_fm;
    int archive_fm_cap;
};

void
tool_framemap_index_free(struct Tool_FramemapIndex* idx);

/**
 * Build a reverse index of every sequence -> its framemap.
 * Memoises per animations-archive id.
 */
int
tool_dat2_build_framemap_index(
    struct Tool_Dat2Cache* c,
    struct Tool_FramemapIndex* out);

/** Collect every seq_id whose framemap is in `seed_fms`. */
int
tool_framemap_index_query(
    const struct Tool_FramemapIndex* idx,
    const int* seed_fms,
    int seed_fm_count,
    int** out_seq_ids,
    int* out_count);

/* ---- Bone-label coverage ------------------------------------------------ */

/**
 * Return 1 if every label used by the model's vertex_bone_map appears in the
 * framemap's bone_groups universe. Empty model bone maps return 1.
 */
int
tool_framemap_covers_model(
    const struct RSCache_Dat2Framemap* fm,
    const struct RSCache_Model* model);

/** Collect the union of labels referenced by a framemap into a 256-bit set. */
void
tool_framemap_label_set(
    const struct RSCache_Dat2Framemap* fm,
    uint8_t present[256]);

/* ---- Dat1 AnimBase content hash ---------------------------------------- */

uint64_t
tool_dat1_animbase_hash(const struct RSCache_Dat1AnimBase* base);

struct Tool_Dat1AnimAffinity
{
    /* Per sequence: base hash of its first frame's archive. */
    uint64_t* seq_base_hash;
    int seq_count;
    /* frame_id -> archive_id (sparse table; -1 absent). */
    int* frame_archive;
    int frame_archive_cap;
    /* archive_id -> base hash. */
    uint64_t* archive_hash;
    int archive_hash_cap;
};

void
tool_dat1_anim_affinity_free(struct Tool_Dat1AnimAffinity* a);

/**
 * Sweep animations table, hash each archive's AnimBase, map frame ids, and
 * associate each sequence with its base hash.
 */
int
tool_dat1_build_anim_affinity(
    struct Tool_Dat1Cache* c,
    const struct RSCache_Dat1ConfigSeqList* seqs,
    struct Tool_Dat1AnimAffinity* out);

int
tool_dat1_affinity_query(
    const struct Tool_Dat1AnimAffinity* a,
    const uint64_t* seed_hashes,
    int seed_hash_count,
    int** out_seq_ids,
    int* out_count);

#endif
