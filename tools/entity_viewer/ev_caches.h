#ifndef EV_CACHES_H
#define EV_CACHES_H

/*
 * The viewer's cache registry and per-cache index.
 *
 * ## Why this exists
 *
 * ev_server used to take one cache on the command line and one prebuilt catalog
 * beside it, and that pair was the whole world for the process lifetime. That is
 * fine for "look at an npc's animations" and wrong for "which of my nine caches
 * has this model in it" — the question that comes up constantly when porting
 * content across eras.
 *
 * So: a list of caches the user can add, switch between and search, persisted
 * so it survives a restart.
 *
 * ## The index is not the catalog
 *
 * `ev_catalog` produces a rich cross-referenced catalog (which sequences match
 * which npc's rig, name-similarity scores) and takes about five minutes per
 * cache. Requiring one before a cache could be browsed would make "add a cache"
 * a coffee break, so this builds a much smaller index directly from the cache:
 * npc ids and names, and the id lists for sequences and models.
 *
 * That is enough to *find* things. The catalog, when one is present for the
 * active cache, still supplies the rig matching on top. The two are independent
 * and either can be absent.
 */

#include "asset_access.h"

#include <stdbool.h>
#include <stdint.h>

#define EV_MAX_CACHES 24
#define EV_CACHE_PATH_MAX 512

struct EV_CacheEntry
{
    char path[EV_CACHE_PATH_MAX];
    /** Profile name, as RSCache_ProfileByName takes it. */
    char rev[32];
    /** Display name; the directory's basename unless the user set one. */
    char label[96];
    /** Filled when this entry has been indexed (i.e. selected at least once). */
    int npc_count;
    int seq_count;
    int model_count;
    bool indexed;
};

struct EV_CacheList
{
    struct EV_CacheEntry items[EV_MAX_CACHES];
    int count;
    /** Index into `items`, or -1. */
    int active;
};

/* ---- the registry -------------------------------------------------------- */

/** Load the list from `file`. A missing file is not an error — it yields an
 *  empty list, which is the first-run state. */
void
ev_caches_load(struct EV_CacheList* list, const char* file);

/** Persist. Best effort; a failure to write is reported but not fatal, because
 *  losing the list is an annoyance and refusing to serve is worse. */
bool
ev_caches_save(const struct EV_CacheList* list, const char* file);

/**
 * Add a cache directory.
 *
 * `rev` may be NULL, in which case it is detected (see ev_cache_detect_rev).
 * Refuses a path with no `main_file_cache.dat2`, and refuses a duplicate path
 * rather than listing it twice. Returns the new index, or -1.
 */
int
ev_caches_add(struct EV_CacheList* list, const char* path, const char* rev);

/**
 * Change an entry's revision profile.
 *
 * Separate from add because detection is a guess that the user has to be able
 * to override *after* seeing what it produced — a wrong profile does not fail,
 * it decodes at the wrong field widths, so the symptom is nonsense records
 * rather than an error. Does not reopen anything; the caller re-selects.
 *
 * Returns false for a bad index or an empty rev.
 */
bool
ev_caches_set_rev(struct EV_CacheList* list, int index, const char* rev);

/** Returns true if it removed something. Adjusts `active`. */
bool
ev_caches_remove(struct EV_CacheList* list, int index);

/**
 * Guess a cache's revision profile.
 *
 * A guess, and treated as one: the UI shows it and lets the user override,
 * because the wrong profile does not fail loudly — it decodes records at the
 * wrong field widths and produces plausible nonsense.
 *
 * It scores every candidate profile by how many npc, obj, loc and sequence
 * records decode to *exact consumption* and takes the best. Four types rather
 * than npc alone because within the RS2 band the npc stream barely moved: every
 * profile from 530 to 643 reads a 634 cache's npcs perfectly, and an npc-only
 * score is a four-way tie decided by loop order. loc and seq are where those
 * revisions separate.
 *
 * Codec-identical revisions (634 and 643) no score can separate, so the
 * directory's own name breaks ties — and only ties.
 *
 * Writes into `out_rev` (at least 32 bytes). Returns false if nothing decoded.
 */
bool
ev_cache_detect_rev(const char* path, char* out_rev, int out_len);

/**
 * Scan `root` one level deep for directories holding a dat2 cache.
 *
 * Appends anything not already listed. This repo keeps every cache as
 * `cache.<name>` at its root, so a scan turns "add a cache" from "type an
 * absolute path" into "click the one you want".
 */
int
ev_caches_discover(struct EV_CacheList* list, const char* root);

/* ---- the per-cache index ------------------------------------------------- */

struct EV_IndexNpc
{
    int id;
    char* name; /* owned; may be NULL */
};

struct EV_Index
{
    struct EV_IndexNpc* npcs;
    int npc_count;
    int* seq_ids;
    int seq_count;
    int* model_ids;
    int model_count;
};

/**
 * Where a cache's index is cached, and what invalidates it.
 *
 * `<cache_dir>/.ev/index-<rev>.evi`. Inside the cache directory rather than
 * beside the viewer because the index describes THAT cache: copy or move the
 * directory and its index travels with it, and deleting the cache does not
 * leave a stale index behind pointing at nothing.
 *
 * Per revision, because the profile decides how every record decodes — the same
 * bytes read as osrs184 and as osrs239 give different npc records. One file
 * keyed only by directory would hand back the previous profile's answers after
 * the revision is corrected, which is the silent-wrong-answer case the
 * correction existed to fix.
 */
#define EV_INDEX_DIR ".ev"

/**
 * A cache's fingerprint: what the stored index is checked against.
 *
 * Sizes and modification times of `main_file_cache.dat2` and every `idxN`,
 * folded together. NOT a content hash: the data file is hundreds of megabytes
 * and hashing it would cost more than the indexing it saves, which would defeat
 * the point. That trade is a real one — a rewrite that preserves every file's
 * size and mtime is not detected — but any ordinary edit, repack or partial
 * download changes at least one of them.
 */
uint64_t
ev_cache_fingerprint(const char* cache_dir);

/**
 * Build the index for an already-open cache.
 *
 * Walks each type's reference table rather than probing ids one by one: a
 * sharded RS2 config table has its records as *files inside* group archives, so
 * "load id N" is two levels deep and scanning 0..40000 misses everything. Ids
 * for sequences and models are taken without decoding, which is what keeps this
 * to seconds rather than minutes.
 */
bool
ev_index_build(
    struct Tool_Dat2Cache* cache,
    const struct RSCache* profile,
    const char* cache_dir,
    const char* rev,
    struct EV_Index* out);

void
ev_index_free(struct EV_Index* index);

#endif
