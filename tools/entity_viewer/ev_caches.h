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
 * The split it can make confidently is the family: an RS2-era cache has a
 * materials table (26) and shards its configs, an OldSchool one does not.
 * Within a family it picks the newest profile whose npc records decode to
 * exact consumption most often.
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
    struct EV_Index* out);

void
ev_index_free(struct EV_Index* index);

#endif
