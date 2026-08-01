/*
 * NPC metadata from the cache: names, and the menu ops the server may act on.
 *
 * Same recipe as mock230_objinfo.c — profile, CONFIGS table, KIND_NPC archive,
 * file list, decode each — and worth keeping separate for the same reason: it
 * is the only place that knows the cache's npc layout.
 *
 * The immediate consumer is npc_say. NPCs have no overhead text in this client
 * (World_NpcSetChat writes a field nothing reads), so the mock renders npc
 * speech as a chatbox line, which needs the speaker's name.
 */

#include "mock230.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Mock230NpcInfo* g_npcs;
static int g_npc_count;

static const struct Mock230NpcInfo k_unknown = { .name = NULL, .combat_level = -1, .size = 0 };

/*
 * Combat bonuses out of the npc record's param table — the same ids as the obj
 * table (0..11 bonuses, 14 attack rate); see mock230_objinfo.c for why those
 * numbers are trustworthy. cache.osrs230's Guard (3254) reads stabdefence +18,
 * slashdefence +25, crushdefence +19, strengthbonus +5 and attackrate 4, which
 * is the whole reason a guard is a real fight and a man is not.
 */
static void
read_combat_params(
    const struct RSCache_Params* params,
    struct Mock230NpcInfo* out)
{
    for( int i = 0; i < params->count; i++ )
    {
        int key = params->keys[i];
        int value;

        if( params->kinds[i] != 0 || !params->values[i] )
            continue;
        value = *(const int*)params->values[i];

        if( key >= 0 && key < 12 )
        {
            out->bonus[key] = value;
            out->has_params = 1;
        }
        else if( key == 14 )
        {
            out->attackrate = value;
            out->has_params = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The npc param table                                                 */
/* ------------------------------------------------------------------ */

/*
 * Every param on every npc record, kept so `nc_param` can answer.
 *
 * `read_combat_params` above already walks this exact list and throws all but
 * fourteen keys away. That is the whole gap `docs/osrs230_mockserver.md` §3.13d
 * called "blocked on data": the data was being decoded and discarded one line
 * from here, and the type half it also wanted has been in `configs/all.param`
 * since cachepack first unpacked it.
 *
 * Flat and sorted, for the reasons `mock230_objinfo.c` records at length: one
 * array of 16-byte rows beats a vector per record when most records have
 * between five and ten params, and the lookup is a binary search.
 *
 * Sorted explicitly, *not* by construction. The decode loop walks npc ids in
 * ascending order but a record's own params arrive in whatever order the cache
 * wrote them, which is not ascending by key — the obj table shipped with that
 * bug and it presents as missing data rather than as wrong data, because a
 * binary search over an almost-sorted array simply fails to find things.
 *
 * String values are strdup'd: the record they came from is freed as soon as the
 * decode loop moves on.
 */
struct NpcParam
{
    int32_t npc_id;
    int32_t key;
    int32_t ival;
    char* sval;
};

static struct NpcParam* g_npc_params;
static int g_npc_param_count;
static int g_npc_param_capacity;

static int
compare_npc_param(
    const void* a,
    const void* b)
{
    const struct NpcParam* left = (const struct NpcParam*)a;
    const struct NpcParam* right = (const struct NpcParam*)b;

    if( left->npc_id != right->npc_id )
        return left->npc_id < right->npc_id ? -1 : 1;
    if( left->key != right->key )
        return left->key < right->key ? -1 : 1;
    return 0;
}

static void
add_param(
    int npc_id,
    int key,
    int ival,
    const char* sval)
{
    struct NpcParam* row;

    if( g_npc_param_count == g_npc_param_capacity )
    {
        int capacity = g_npc_param_capacity ? g_npc_param_capacity * 2 : 4096;
        struct NpcParam* grown =
            (struct NpcParam*)realloc(g_npc_params, (size_t)capacity * sizeof(*grown));

        if( !grown )
            return;
        g_npc_params = grown;
        g_npc_param_capacity = capacity;
    }
    row = &g_npc_params[g_npc_param_count++];
    row->npc_id = npc_id;
    row->key = key;
    row->ival = ival;
    row->sval = sval ? strdup(sval) : NULL;
}

static void
read_params(
    int npc_id,
    const struct RSCache_Params* params)
{
    for( int i = 0; i < params->count; i++ )
    {
        if( !params->values[i] )
            continue;
        if( params->kinds[i] == RSCACHE_PARAM_STRING )
            add_param(npc_id, params->keys[i], 0, (const char*)params->values[i]);
        else if( params->kinds[i] == RSCACHE_PARAM_INT )
            add_param(npc_id, params->keys[i], *(const int*)params->values[i], NULL);
        /*
         * RSCACHE_PARAM_LONG is dropped rather than truncated, same call as the
         * obj table: eight bytes squeezed onto a 32-bit stack is a wrong answer
         * that looks right, where a missing param reports as "not set" — a state
         * content already handles. cache.osrs239's npc table emits none.
         */
    }
}

const struct Mock230NpcParam*
mock230_npc_param(
    int npc_id,
    int param_id)
{
    int low = 0;
    int high = g_npc_param_count - 1;

    while( low <= high )
    {
        int mid = (low + high) / 2;
        struct NpcParam* row = &g_npc_params[mid];

        if( row->npc_id < npc_id || (row->npc_id == npc_id && row->key < param_id) )
            low = mid + 1;
        else if( row->npc_id > npc_id || (row->npc_id == npc_id && row->key > param_id) )
            high = mid - 1;
        else
            return (const struct Mock230NpcParam*)row;
    }
    return NULL;
}

int
mock230_npcinfo_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int highest = -1;

    mock230_npcinfo_free();

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
        fprintf(stderr, "mock230: no npc metadata (cache '%s' not found)\n", cache_dir);
        return 0;
    }

    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_NPC);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        fprintf(stderr, "mock230: no npc config archive in '%s'\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_NPC, archive->revision);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    /* File ids are sparse, so the table is sized from the largest id rather
     * than from the file count. */
    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] > highest )
            highest = archive->file_ids[i];
    }
    if( highest < 0 )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    g_npc_count = highest + 1;
    g_npcs = (struct Mock230NpcInfo*)calloc((size_t)g_npc_count, sizeof(*g_npcs));
    if( !g_npcs )
    {
        g_npc_count = 0;
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    for( int i = 0; i < g_npc_count; i++ )
    {
        g_npcs[i].name = NULL;
        g_npcs[i].combat_level = -1;
        g_npcs[i].size = 1;
        g_npcs[i].attackrate = 4;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigNpc* npc;

        if( id < 0 || id >= g_npc_count )
            continue;
        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(
            &profile, files->files[i], files->file_sizes[i]);
        if( !npc )
            continue;

        if( npc->name && strcmp(npc->name, "null") != 0 )
            g_npcs[id].name = strdup(npc->name);
        g_npcs[id].combat_level = npc->combat_level;
        g_npcs[id].size = npc->size > 0 ? npc->size : 1;
        /* Config opcode 18 (`dat2_config_npc.c:666`), already decoded by the
         * linked rscache npc decoder and discarded here until now. Stored
         * unconditionally, above the `name` gate the accessor applies: a
         * nameless multinpc instance still dispatches, and 1,585 of the
         * categorised records in this cache have no name. 0 is the decoder's
         * "no category stated" and is deliberately not a name in
         * pack/category.pack — binding a trigger to 0 would match everything. */
        g_npcs[id].category = npc->category;
        for( int op = 0; op < 5; op++ )
            g_npcs[id].ops[op] = npc->actions[op] ? strdup(npc->actions[op]) : NULL;
        read_combat_params(&npc->params, &g_npcs[id]);
        /* The whole list, not only the fourteen keys above. */
        read_params(id, &npc->params);
        RSCache_Dat2ConfigNpcFree(npc);
    }

    /* Binary-searched, so it has to actually be ordered — see `struct
     * NpcParam`. */
    qsort(g_npc_params, (size_t)g_npc_param_count, sizeof(*g_npc_params), compare_npc_param);

    /* Read the count before the free, not after: the archive owns it. The
     * first version of this printed "0 records" from freed memory while the
     * table itself was fine. */
    {
        int loaded = archive->file_count;

        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);

        fprintf(stderr,
                "mock230: npc metadata loaded (%d records from %s, %d params in %zu KB)\n",
                loaded, cache_dir, g_npc_param_count,
                ((size_t)g_npc_param_count * sizeof(struct NpcParam)) / 1024);
    }
    return 1;
}

void
mock230_npcinfo_free(void)
{
    if( g_npcs )
    {
        for( int i = 0; i < g_npc_count; i++ )
        {
            free((void*)g_npcs[i].name);
            for( int op = 0; op < 5; op++ )
                free((void*)g_npcs[i].ops[op]);
        }
        free(g_npcs);
    }
    g_npcs = NULL;
    g_npc_count = 0;

    for( int i = 0; i < g_npc_param_count; i++ )
        free(g_npc_params[i].sval);
    free(g_npc_params);
    g_npc_params = NULL;
    g_npc_param_count = 0;
    g_npc_param_capacity = 0;
}

int
mock230_npcinfo_known(int npc_id)
{
    /* The same gate the accessor below applies, stated once each. */
    return g_npcs && npc_id >= 0 && npc_id < g_npc_count && g_npcs[npc_id].name != NULL;
}

int
mock230_npc_category(int npc_id)
{
    if( !g_npcs || npc_id < 0 || npc_id >= g_npc_count )
        return -1;
    /* Not `mock230_npcinfo(npc_id)->category`: that accessor hides the whole row
     * of a nameless record, and the nameless records are exactly the multinpc
     * instances a trigger has to reach. See the header. */
    return g_npcs[npc_id].category > 0 ? g_npcs[npc_id].category : -1;
}

int
mock230_npc_category_members(int category)
{
    int members = 0;

    if( !g_npcs || category <= 0 )
        return 0;
    for( int i = 0; i < g_npc_count; i++ )
    {
        if( g_npcs[i].category == category )
            members++;
    }
    return members;
}

const struct Mock230NpcInfo*
mock230_npcinfo(int npc_id)
{
    static struct Mock230NpcInfo placeholder;

    if( g_npcs && npc_id >= 0 && npc_id < g_npc_count && g_npcs[npc_id].name )
        return &g_npcs[npc_id];

    /* Never NULL: a name is used in player-facing text, so an unknown id has to
     * render as something rather than crash the message that would have told
     * you which id it was. */
    placeholder = k_unknown;
    placeholder.name = "Someone";
    return &placeholder;
}

/*
 * The same row, ungated.
 *
 * `mock230_npcinfo` above answers "what do I call this thing", and its
 * placeholder is right for that. Reading a *field* off it is a different
 * question and the name gate gives the wrong answer to it: 1,585 of
 * cache.osrs239's category-carrying npc records have no name, so
 * `npc_category` through the gated accessor would report 0 — "no category" —
 * for every multinpc instance in the game.
 */
const struct Mock230NpcInfo*
mock230_npcinfo_record(int npc_id)
{
    if( !g_npcs || npc_id < 0 || npc_id >= g_npc_count )
        return NULL;
    return &g_npcs[npc_id];
}
