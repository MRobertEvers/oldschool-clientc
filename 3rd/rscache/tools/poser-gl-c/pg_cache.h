#ifndef POSER_GL_C_PG_CACHE_H
#define POSER_GL_C_PG_CACHE_H

/*
 * poser-gl's CacheService, over rscache instead of a JAR plugin.
 *
 * poser-gl reaches the cache through a per-revision plugin implementing
 * ICacheLoader; rscache already is that layer, with a revision profile in place
 * of a plugin, so this file is the adaptor between rscache's structs and the
 * shapes the editor's model and animation code expects.
 *
 * Everything the lists show is loaded up front, as the reference does — the
 * search boxes filter over the whole set — while models and frame archives load
 * on demand and are cached by id.
 */

#include "pg_model.h"

#include <stdbool.h>
#include <stdint.h>

/** The synthetic composed-player entry, which no cache record backs. */
#define PG_PLAYER_NPC_ID (-1)

struct PG_NpcDef
{
    int id;
    char* name;
    int* models;
    int model_count;
    int size; /* tiles occupied; poser-gl calls this `spaces` */
    uint16_t* recolor_from;
    uint16_t* recolor_to;
    int recolor_count;
};

struct PG_ItemDef
{
    int id;
    char* name;
    int model0;
    int model1;
    int model2;
    uint16_t* recolor_from;
    uint16_t* recolor_to;
    int recolor_count;
};

struct PG_IdkDef
{
    int id;
    int body_part_id;
    int* models;
    int model_count;
};

struct PG_SeqDef
{
    int id;
    int* frame_ids;
    int* frame_lengths;
    int frame_count;
    int loop_offset;
    int left_hand_item;
    int right_hand_item;
    char* debug_name; /* nullable; the content team's own name where present */
};

struct PG_FrameMapDef
{
    int id;
    int length;
    int* types;
    int** maps;
    int* map_lengths;
    /** Set when this framemap was synthesised by a .pgl import rather than read
     *  from the cache, in which case it is owned by the animation. */
    bool owned;
};

struct PG_FrameDef
{
    int id;
    struct PG_FrameMapDef* framemap;
    int count;
    int* indices;
    int* delta_x;
    int* delta_y;
    int* delta_z;
};

struct PG_FrameArchive
{
    int archive_id;
    struct PG_FrameMapDef* framemap;
    struct PG_FrameDef* frames;
    int frame_count;
};

struct PG_Cache;

/**
 * Open a cache. `rev` is an rscache revision name (osrs230, rs643, …).
 * Returns NULL after printing the reason to stderr.
 */
struct PG_Cache*
pg_cache_open(const char* directory, const char* rev);

void
pg_cache_close(struct PG_Cache* c);

const char*
pg_cache_path(const struct PG_Cache* c);
const char*
pg_cache_rev(const struct PG_Cache* c);

/**
 * True when the open cache stores models at the newer, larger vertex scale, so
 * the viewer downscales the entity and the node positions to match. poser-gl
 * calls this `isHigherRev`.
 */
bool
pg_cache_is_higher_rev(const struct PG_Cache* c);

int
pg_cache_npc_count(const struct PG_Cache* c);
const struct PG_NpcDef*
pg_cache_npc_at(const struct PG_Cache* c, int index);
const struct PG_NpcDef*
pg_cache_npc(const struct PG_Cache* c, int id);

int
pg_cache_item_count(const struct PG_Cache* c);
const struct PG_ItemDef*
pg_cache_item_at(const struct PG_Cache* c, int index);
const struct PG_ItemDef*
pg_cache_item(const struct PG_Cache* c, int id);

int
pg_cache_seq_count(const struct PG_Cache* c);
const struct PG_SeqDef*
pg_cache_seq_at(const struct PG_Cache* c, int index);
const struct PG_SeqDef*
pg_cache_seq(const struct PG_Cache* c, int id);
/** Highest sequence id present, or -1. New animations are numbered above it. */
int
pg_cache_max_seq_id(const struct PG_Cache* c);

/** Decode a model, applying `recolor_from[i] -> recolor_to[i]`. Caller frees. */
struct PG_ModelDef*
pg_cache_load_model(
    struct PG_Cache* c,
    int model_id,
    const uint16_t* recolor_from,
    const uint16_t* recolor_to,
    int recolor_count);

/**
 * Every frame in one animation archive, with the framemap they share. Owned by
 * the cache and memoised, so the result stays valid until pg_cache_close.
 * Returns NULL when the archive is absent or holds no decodable frame.
 */
const struct PG_FrameArchive*
pg_cache_frame_archive(struct PG_Cache* c, int archive_id);

/* ---- write-back ---------------------------------------------------------- */

/**
 * Write an animation's modified frames into a fresh animation archive and its
 * sequence record into the seq config, then commit to `out_directory`.
 *
 * `frame_count` frames are written as files 0..frame_count-1 of one new archive
 * whose id is returned through `out_archive_id`; the sequence's frame ids are
 * rebuilt against it by the caller before this is called.
 *
 * Returns 0 on success, non-zero on failure with the reason on stderr.
 */
struct PG_PackFrame
{
    /** Bone flag stream and per-entry provenance are needed for a byte-exact
     *  encode; a frame the editor built from scratch supplies its own. */
    int count;
    int* indices;
    int* delta_x;
    int* delta_y;
    int* delta_z;
};

/**
 * The archive id a pack would allocate: one past the highest the animations
 * table lists. Callers need it up front because a sequence's frame ids embed the
 * archive id, so the sequence record has to be built before the write.
 */
int
pg_cache_next_anim_archive_id(struct PG_Cache* c);

int
pg_cache_pack_animation(
    struct PG_Cache* c,
    const char* out_directory,
    const struct PG_FrameMapDef* framemap,
    const struct PG_PackFrame* frames,
    int frame_count,
    const struct PG_SeqDef* sequence,
    int* out_archive_id);

#endif
