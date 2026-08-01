/*
 * The loc config table as the script host sees it: params, names, footprints.
 *
 * ── Why this is not read through the scene ───────────────────────────
 *
 * mock230_scene.c's `load_loc_configs` already decodes every loc record and
 * holds the whole `struct RSCache_Dat2ConfigLoc` for each — an accessor over
 * `g_loc_configs` would cost zero new bytes. It is still wrong: that table is
 * owned by the *scene*, and the scene is rebuilt every time the player crosses
 * a rebuild boundary. Params would be unavailable before first login and would
 * flicker across every rebuild. A param lookup must not have a scene's
 * lifetime, so this is its own boot-time table.
 *
 * (Filed while measuring: `g_loc_configs` is scene-*lifetime* but
 * scene-*independent*, so the scene re-pays that decode on every rebuild for no
 * reason. Out of scope here; worth hoisting.)
 *
 * ── What it costs ────────────────────────────────────────────────────
 *
 * Measured on cache.osrs239: 62,194 loc records, of which 1,070 carry params at
 * all and those hold 1,709 rows — every one an int, none a string, none
 * RSCACHE_PARAM_LONG. Retained is ~27 KB. The record *count* is what made the
 * triage call this "a real memory question"; the retained answer is 0.06% of
 * the 45 MB the scene already spends holding the same records.
 *
 * ── Names, and what they actually cost ───────────────────────────────
 *
 * `loc_name` / `lc_name` need `loc->name`, so it is retained now. The previous
 * revision of this file put the price at "~700 KB, 25x the param table" and
 * left it out on that basis; re-measured, it is 325,417 bytes of text across
 * 30,033 named records. The 700 KB figure was `strdup` per record — 30,033
 * separate allocations, each with a malloc header, plus a pointer array over
 * all 62,194 ids.
 *
 * So it is stored the cheap way instead: one concatenated blob and a sorted
 * (id, offset) index over only the records that carry a name. That is
 * 325,417 + 30,033 * 8 = ~560 KB in two allocations. Measured, not estimated —
 * the loader prints the number and the test asserts the row count.
 *
 * Not retained: `loc->desc`. **0 of the 62,194 records carry one** — the field
 * is gone from OSRS loc configs — so `lc_desc` would push the reference's
 * `'null'` fallback for every id in the cache and there are no callers in the
 * reference tree. Left to the VM's loud stub rather than implemented as a
 * constant.
 *
 * ── Footprints ───────────────────────────────────────────────────────
 *
 * `lc_width` / `lc_length` read `size_x` / `size_z`, which the decoder defaults
 * to 1x1 and 17,309 records override. Only the overrides are stored, one byte
 * each (measured maxima: 17 and 33, both g1 on the wire), so the table is
 * 17,309 * 8 = ~138 KB and an id that is not in it answers 1x1 — which is the
 * decoder's own default, not a guess.
 *
 * ── Which ids exist ──────────────────────────────────────────────────
 *
 * The reference's handlers all run `check(id, LocTypeValid)` and throw on an id
 * outside the config, so this side has to be able to answer the same question.
 * A bitmap over the id space costs (max_id + 1) / 8 = ~7.8 KB and is exact,
 * where a `0 <= id <= max` range test would assume a density nothing checks.
 */

#include "mock230.h"
#include "mock230_paramtable.h"

#include <rscache.h>

#include <datatypes/dat2_config_loc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Mock230ParamTable g_loc_params;
static int g_loc_records;

/** (id -> offset into g_name_blob), ascending by id. */
struct LocNameRow
{
    int32_t id;
    int32_t offset;
};

/** (id -> footprint), ascending by id. Only the records that are not 1x1. */
struct LocSizeRow
{
    int32_t id;
    uint8_t size_x;
    uint8_t size_z;
};

static char* g_name_blob;
static size_t g_name_blob_len;
static size_t g_name_blob_cap;
static struct LocNameRow* g_names;
static int g_name_count;
static int g_name_cap;

static struct LocSizeRow* g_sizes;
static int g_size_count;
static int g_size_cap;

/** Bit per loc id: 1 when the config group holds a record for it. */
static uint8_t* g_known;
static int g_known_bits;

static int
compare_name_row(const void* a, const void* b)
{
    int32_t left = ((const struct LocNameRow*)a)->id;
    int32_t right = ((const struct LocNameRow*)b)->id;

    return left < right ? -1 : (left > right ? 1 : 0);
}

static int
compare_size_row(const void* a, const void* b)
{
    int32_t left = ((const struct LocSizeRow*)a)->id;
    int32_t right = ((const struct LocSizeRow*)b)->id;

    return left < right ? -1 : (left > right ? 1 : 0);
}

static void
add_name(
    int loc_id,
    const char* name)
{
    size_t length = strlen(name) + 1;

    if( g_name_blob_len + length > g_name_blob_cap )
    {
        size_t want = g_name_blob_cap ? g_name_blob_cap * 2 : 65536;
        char* grown;

        while( want < g_name_blob_len + length )
            want *= 2;
        grown = realloc(g_name_blob, want);
        if( !grown )
            return;
        g_name_blob = grown;
        g_name_blob_cap = want;
    }
    if( g_name_count == g_name_cap )
    {
        int want = g_name_cap ? g_name_cap * 2 : 1024;
        struct LocNameRow* grown = realloc(g_names, (size_t)want * sizeof(*grown));

        if( !grown )
            return;
        g_names = grown;
        g_name_cap = want;
    }
    memcpy(g_name_blob + g_name_blob_len, name, length);
    g_names[g_name_count].id = loc_id;
    g_names[g_name_count].offset = (int32_t)g_name_blob_len;
    g_name_count++;
    g_name_blob_len += length;
}

static void
add_size(
    int loc_id,
    int size_x,
    int size_z)
{
    if( g_size_count == g_size_cap )
    {
        int want = g_size_cap ? g_size_cap * 2 : 1024;
        struct LocSizeRow* grown = realloc(g_sizes, (size_t)want * sizeof(*grown));

        if( !grown )
            return;
        g_sizes = grown;
        g_size_cap = want;
    }
    /*
     * Both are `g1` on the wire, so a byte each holds every value the cache can
     * express. Clamping rather than widening would be a silent wrong answer, so
     * assert the assumption instead of hiding it.
     */
    if( size_x < 0 || size_x > 255 || size_z < 0 || size_z > 255 )
    {
        fprintf(stderr, "mock230: loc %d has an out-of-byte footprint %dx%d\n", loc_id,
                size_x, size_z);
        return;
    }
    g_sizes[g_size_count].id = loc_id;
    g_sizes[g_size_count].size_x = (uint8_t)size_x;
    g_sizes[g_size_count].size_z = (uint8_t)size_z;
    g_size_count++;
}

static void
mark_known(int loc_id)
{
    if( loc_id < 0 )
        return;
    if( loc_id >= g_known_bits )
    {
        int want = g_known_bits ? g_known_bits * 2 : 65536;
        uint8_t* grown;

        while( want <= loc_id )
            want *= 2;
        grown = realloc(g_known, (size_t)(want + 7) / 8);
        if( !grown )
            return;
        memset(grown + (g_known_bits + 7) / 8, 0,
               (size_t)(want + 7) / 8 - (size_t)(g_known_bits + 7) / 8);
        g_known = grown;
        g_known_bits = want;
    }
    g_known[loc_id >> 3] |= (uint8_t)(1u << (loc_id & 7));
}

const struct Mock230ParamRow*
mock230_loc_param(
    int loc_id,
    int param_id)
{
    return mock230_paramtable_find(&g_loc_params, loc_id, param_id);
}

int
mock230_loc_known(int loc_id)
{
    if( loc_id < 0 || loc_id >= g_known_bits || !g_known )
        return 0;
    return (g_known[loc_id >> 3] >> (loc_id & 7)) & 1;
}

const char*
mock230_loc_name(int loc_id)
{
    int low = 0;
    int high = g_name_count - 1;

    if( !g_names || !g_name_blob )
        return NULL;
    while( low <= high )
    {
        int mid = low + (high - low) / 2;

        if( g_names[mid].id == loc_id )
            return g_name_blob + g_names[mid].offset;
        if( g_names[mid].id < loc_id )
            low = mid + 1;
        else
            high = mid - 1;
    }
    return NULL;
}

void
mock230_loc_footprint(
    int loc_id,
    int* out_width,
    int* out_length)
{
    int low = 0;
    int high = g_size_count - 1;

    /* The decoder's own defaults, so an id with no override and an id with no
     * record answer the same 1x1 rather than 0x0. */
    if( out_width )
        *out_width = 1;
    if( out_length )
        *out_length = 1;
    if( !g_sizes )
        return;
    while( low <= high )
    {
        int mid = low + (high - low) / 2;

        if( g_sizes[mid].id == loc_id )
        {
            if( out_width )
                *out_width = g_sizes[mid].size_x;
            if( out_length )
                *out_length = g_sizes[mid].size_z;
            return;
        }
        if( g_sizes[mid].id < loc_id )
            low = mid + 1;
        else
            high = mid - 1;
    }
}

int
mock230_locinfo_count(void)
{
    return g_loc_records;
}

int
mock230_locinfo_param_count(void)
{
    return g_loc_params.count;
}

int
mock230_locinfo_name_count(void)
{
    return g_name_count;
}

int
mock230_locinfo_size_count(void)
{
    return g_size_count;
}

int
mock230_locinfo_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;

    mock230_locinfo_free();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = MOCK230_CACHE_REVISION;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Run from src/ as well as from the repo root, like objinfo. */
        char fallback[512];

        snprintf(fallback, sizeof(fallback), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(fallback);
    }
    if( !disk )
    {
        fprintf(stderr, "mock230: no loc params (cache '%s' not found)\n", cache_dir);
        return 0;
    }

    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_LOCS);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        fprintf(stderr, "mock230: no loc config archive in '%s'\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    /* The loc decoder branches on the group revision — mock230_scene.c sets it
     * the same way before decoding the same archive. */
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_LOC, archive->revision);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigLoc* loc;

        if( files->file_sizes[i] <= 0 )
            continue;
        loc = RSCache_Dat2ConfigLocNewDecodeProfile(&profile, files->files[i],
                                                    files->file_sizes[i]);
        if( !loc )
            continue;
        mock230_paramtable_read(&g_loc_params, archive->file_ids[i], &loc->params);
        mark_known(archive->file_ids[i]);
        if( loc->name )
            add_name(archive->file_ids[i], loc->name);
        if( loc->size_x != 1 || loc->size_z != 1 )
            add_size(archive->file_ids[i], loc->size_x, loc->size_z);
        RSCache_Dat2ConfigLocFree(loc);
    }

    /*
     * Loc is the worst-ordered of the four param tables: 525 of the 599 records
     * carrying two or more params are out of key order (87.6%). Sorting by
     * construction would miss nearly nine in ten of them, silently.
     *
     * The name and footprint tables are sorted for the weaker reason that
     * nothing promises `archive->file_ids` is ascending — it happens to be on
     * this cache, and a binary search that relied on that would work until it
     * did not.
     */
    mock230_paramtable_sort(&g_loc_params);
    if( g_names && g_name_count > 1 )
        qsort(g_names, (size_t)g_name_count, sizeof(*g_names), compare_name_row);
    if( g_sizes && g_size_count > 1 )
        qsort(g_sizes, (size_t)g_size_count, sizeof(*g_sizes), compare_size_row);

    g_loc_records = archive->file_count;

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);

    fprintf(stderr,
            "mock230: loc configs loaded (%d records from %s; %d param rows in %zu bytes, "
            "%d names in %zu bytes, %d footprints in %zu bytes)\n",
            g_loc_records, cache_dir, g_loc_params.count,
            mock230_paramtable_bytes(&g_loc_params), g_name_count,
            g_name_blob_len + (size_t)g_name_count * sizeof(struct LocNameRow),
            g_size_count, (size_t)g_size_count * sizeof(struct LocSizeRow));
    return 1;
}

void
mock230_locinfo_free(void)
{
    mock230_paramtable_free(&g_loc_params);
    g_loc_records = 0;

    free(g_name_blob);
    g_name_blob = NULL;
    g_name_blob_len = 0;
    g_name_blob_cap = 0;
    free(g_names);
    g_names = NULL;
    g_name_count = 0;
    g_name_cap = 0;

    free(g_sizes);
    g_sizes = NULL;
    g_size_count = 0;
    g_size_cap = 0;

    free(g_known);
    g_known = NULL;
    g_known_bits = 0;
}
