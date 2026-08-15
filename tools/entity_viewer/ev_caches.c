/*
 * The viewer's cache registry and per-cache index. See ev_caches.h for why.
 */

#include "ev_caches.h"
#include <assert.h>

#include "dat2disk.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_config_sequence.h"
#include "filelist.h"
#include "reference_table.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

/* ---- small helpers ------------------------------------------------------- */

static bool
dir_has_cache(const char* path)
{
    char probe[EV_CACHE_PATH_MAX + 64];
    snprintf(probe, sizeof(probe), "%s/main_file_cache.dat2", path);
    struct stat st;
    return stat(probe, &st) == 0 && S_ISREG(st.st_mode);
}

static const char*
basename_of(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ---- revision detection -------------------------------------------------- */

/*
 * Candidates, newest first within each family.
 *
 * Ordered rather than exhaustive on purpose: the list is the profiles this tool
 * can usefully browse, and trying every registered revision would mostly
 * distinguish profiles that differ in ways a config record cannot show.
 *
 * `void634` rather than `rs634`: the two share every codec, and the one that
 * differs — RSCACHE_QUIRK_VOID_RS634_NO_XTEAS — is right for the cache in this
 * tree and harmless for a stock 634 one, which the viewer never asks for keys
 * for anyway. A user who wants the bare revision can still set it by hand.
 */
static const char* const RS2_CANDIDATES[] = { "rs727", "rs643", "void634", "rs558", "rs530" };
static const char* const OSRS_CANDIDATES[] = { "osrs239", "osrs230", "osrs184" };

/*
 * Detection probes profiles it expects to be wrong, and a wrong profile makes
 * the decoders print — "Buffer overflow while reading models at index 31",
 * "Failed to load referencetable 16". Those go to STDOUT, so they land in the
 * server's log and, worse, read as failures when they are the mechanism
 * working. Muted for the duration of a probe and restored after; a real error
 * from the chosen profile still prints, because the chosen profile is used
 * outside this.
 */
struct muted
{
    int saved;
};

static void
mute_stdout(struct muted* m)
{
    fflush(stdout);
    m->saved = dup(STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if( devnull >= 0 )
    {
        dup2(devnull, STDOUT_FILENO);
        close(devnull);
    }
}

static void
unmute_stdout(struct muted* m)
{
    fflush(stdout);
    if( m->saved >= 0 )
    {
        dup2(m->saved, STDOUT_FILENO);
        close(m->saved);
        m->saved = -1;
    }
}

/**
 * Did this record decode to exact consumption under `profile`?
 *
 * Exact consumption is the signal that matters: a wrong profile usually still
 * *decodes* — it stops early on a byte it read as a terminator — so "did it
 * return non-NULL" separates nothing. Landing exactly on the end of every
 * record is what a right profile does and a wrong one does not.
 */
static bool
record_is_exact(
    const struct RSCache* profile,
    enum RSCache_Type type,
    char* data,
    int size)
{
    bool exact = false;
    switch( type )
    {
    case RSCACHE_TYPE_NPC:
    {
        struct RSCache_Dat2ConfigNpc* r =
            RSCache_Dat2ConfigNpcNewDecodeProfile(profile, data, size);
        exact = r && r->_consumed == size;
        RSCache_Dat2ConfigNpcFree(r);
        break;
    }
    case RSCACHE_TYPE_OBJ:
    {
        struct RSCache_Dat2ConfigObj* r =
            RSCache_Dat2ConfigObjNewDecodeProfile(profile, data, size);
        exact = r && r->_consumed == size;
        RSCache_Dat2ConfigObjFree(r);
        break;
    }
    case RSCACHE_TYPE_LOC:
    {
        struct RSCache_Dat2ConfigLoc* r =
            RSCache_Dat2ConfigLocNewDecodeProfile(profile, data, size);
        exact = r && r->_consumed == size;
        RSCache_Dat2ConfigLocFree(r);
        break;
    }
    case RSCACHE_TYPE_SEQUENCE:
    {
        struct RSCache_Dat2ConfigSequence* r =
            RSCache_Dat2ConfigSequenceNewDecodeProfile(profile, data, size);
        exact = r && r->_consumed == size;
        RSCache_Dat2ConfigSequenceFree(r);
        break;
    }
    default:
        break;
    }
    return exact;
}

/**
 * Score one type: how many of the first `limit` records consume exactly.
 *
 * Adds to the running tallies rather than answering on its own, because the
 * verdict is over the whole set — see score_profile.
 *
 * The two record layouts are the same pair walk_records deals with, and getting
 * them confused is silent: under the config-group layout (OldSchool) the
 * reference table's ids name config *kinds*, so decoding a table id as a record
 * hands the decoder a whole archive and scores every profile at zero.
 */
static void
score_type(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    enum RSCache_Type type,
    int limit,
    int* seen,
    int* exact)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, type);
    int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
    if( table_id < 0 )
        return;

    /* The groups to walk: one named archive under the config-group layout, or
     * every id in the reference table under the sharded one. */
    int single = addr.group;
    int* group_ids = &single;
    int group_count = 1;
    struct RSCache_ReferenceTable* table = NULL;

    if( addr.group_shift != 0 )
    {
        struct RSCache_Dat2DiskArchive* ref =
            RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
        if( !ref )
            return;
        table = RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
        RSCache_Dat2DiskArchiveFree(ref);
        if( !table )
            return;
        group_ids = table->ids;
        group_count = table->id_count;
    }

    for( int g = 0; g < group_count && *seen < limit; g++ )
    {
        struct RSCache_Dat2DiskArchive* a =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, group_ids[g]);
        if( !a )
            continue;
        RSCache_Dat2DiskArchiveInitMetadata(disk, a);

        struct RSCache_FileList* fl =
            RSCache_FileListNewFromDecode(a->data, a->data_size, a->file_count);
        if( fl )
        {
            for( int f = 0; f < fl->file_count && *seen < limit; f++ )
            {
                if( fl->file_sizes[f] <= 0 )
                    continue;
                (*seen)++;
                if( record_is_exact(profile, type, fl->files[f], fl->file_sizes[f]) )
                    (*exact)++;
            }
            RSCache_FileListFree(fl);
        }
        RSCache_Dat2DiskArchiveFree(a);
    }

    if( table )
        RSCache_ReferenceTableFree(table);
}

/*
 * The types the score is taken over.
 *
 * Not npc alone, which is what this used to be. Every RS2 profile from 530 to
 * 643 reads a 634 cache's npc records to 100% exact consumption — the npc stream
 * did not move across that whole band — so an npc-only score is a four-way tie
 * and the winner is whichever candidate the loop happened to reach first. loc
 * and seq are where those revisions actually separate (32% vs 100% loc between
 * 558 and 643; 70% vs 100% seq between 643 and 558), and obj separates 643 from
 * 727. Scoring all four is what makes the answer evidence rather than order.
 */
static const enum RSCache_Type SCORED_TYPES[] = {
    RSCACHE_TYPE_NPC,
    RSCACHE_TYPE_OBJ,
    RSCACHE_TYPE_LOC,
    RSCACHE_TYPE_SEQUENCE,
};

/** Percentage of sampled records consuming exactly, or -1 if nothing decoded. */
static int
score_profile(const char* path, const struct RSCache* profile, int limit)
{
    struct muted mute;
    mute_stdout(&mute);

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        unmute_stdout(&mute);
        return -1;
    }
    struct RSCache local = *profile;
    RSCache_Dat2DiskSetProfile(disk, &local);

    int seen = 0;
    int exact = 0;
    for( size_t t = 0; t < sizeof(SCORED_TYPES) / sizeof(SCORED_TYPES[0]); t++ )
    {
        /* Per-type budget, so one plentiful type cannot crowd out the ones that
         * carry the distinguishing evidence. */
        int type_seen = 0;
        int type_exact = 0;
        score_type(disk, &local, SCORED_TYPES[t], limit, &type_seen, &type_exact);
        seen += type_seen;
        exact += type_exact;
    }

    RSCache_Dat2DiskFree(disk);
    unmute_stdout(&mute);
    return seen > 0 ? (exact * 100) / seen : -1;
}

bool
ev_cache_detect_rev(const char* path, char* out_rev, int out_len)
{
    if( out_len < 8 )
        return false;
    assert(path);
    assert(out_rev);

    /*
     * Score every candidate and take the best.
     *
     * An earlier version split the families first by asking whether a materials
     * table existed — RS2 has one, OldSchool does not. That test is unsound:
     * RSCache_Dat2DiskTableId resolves a LOGICAL table through whatever profile
     * is currently set, so under an RS2 profile it answers yes for caches that
     * have no materials at all, and every OldSchool cache in the tree detected
     * as rs727. Scoring is slower by a few probes and does not have a wrong
     * answer to give.
     */
    const char* best = NULL;
    int best_score = -1;

    const char* const* lists[2] = { RS2_CANDIDATES, OSRS_CANDIDATES };
    int counts[2] = { (int)(sizeof(RS2_CANDIDATES) / sizeof(RS2_CANDIDATES[0])),
                      (int)(sizeof(OSRS_CANDIDATES) / sizeof(OSRS_CANDIDATES[0])) };

    /*
     * The directory's own name breaks ties, and only ties.
     *
     * Revisions inside one band can be codec-identical — 634 and 643 read every
     * config type the same way — so no amount of decoding separates them, and
     * the winner would otherwise be whichever the loop reached first. This tree
     * names every cache `cache.<rev>`, so the folder is the one piece of
     * evidence left. It never overrules a higher score: a directory called
     * cache.void634 holding an OldSchool cache still detects as OldSchool.
     */
    const char* base = basename_of(path);

    for( int l = 0; l < 2; l++ )
    {
        for( int i = 0; i < counts[l]; i++ )
        {
            struct RSCache p;
            if( !RSCache_ProfileByName(lists[l][i], &p) )
                continue;
            int score = score_profile(path, &p, 240);
            bool named = strstr(base, lists[l][i]) != NULL;
            bool better = score > best_score;
            bool tie_and_named = score == best_score && named && best &&
                                 strstr(base, best) == NULL;
            if( better || tie_and_named )
            {
                best_score = score;
                best = lists[l][i];
            }
        }
    }

    /* Nothing decoded a single npc record exactly: the caller should treat the
     * result as unknown rather than take a coin flip. */
    if( !best || best_score <= 0 )
        return false;

    snprintf(out_rev, (size_t)out_len, "%s", best);
    return true;
}

/* ---- the registry -------------------------------------------------------- */

void
ev_caches_load(struct EV_CacheList* list, const char* file)
{
    memset(list, 0, sizeof(*list));
    list->active = -1;

    FILE* f = fopen(file, "r");
    if( !f )
        return; /* first run */

    char line[EV_CACHE_PATH_MAX + 160];
    while( fgets(line, sizeof(line), f) && list->count < EV_MAX_CACHES )
    {
        char* nl = strchr(line, '\n');
        if( nl )
            *nl = '\0';
        if( line[0] == '\0' || line[0] == '#' )
            continue;

        /* rev \t path \t label */
        char* tab1 = strchr(line, '\t');
        if( !tab1 )
            continue;
        *tab1 = '\0';
        char* rest = tab1 + 1;
        char* tab2 = strchr(rest, '\t');
        const char* label = NULL;
        if( tab2 )
        {
            *tab2 = '\0';
            label = tab2 + 1;
        }

        struct EV_CacheEntry* e = &list->items[list->count];
        memset(e, 0, sizeof(*e));
        snprintf(e->rev, sizeof(e->rev), "%s", line);
        snprintf(e->path, sizeof(e->path), "%s", rest);
        snprintf(e->label, sizeof(e->label), "%s",
                 (label && *label) ? label : basename_of(e->path));
        list->count++;
    }
    fclose(f);
}

bool
ev_caches_save(const struct EV_CacheList* list, const char* file)
{
    FILE* f = fopen(file, "w");
    if( !f )
        return false;
    fprintf(f, "# entity viewer cache registry: rev<TAB>path<TAB>label\n");
    for( int i = 0; i < list->count; i++ )
        fprintf(f, "%s\t%s\t%s\n", list->items[i].rev, list->items[i].path,
                list->items[i].label);
    fclose(f);
    return true;
}

int
ev_caches_add(struct EV_CacheList* list, const char* path, const char* rev)
{
    char resolved[EV_CACHE_PATH_MAX];

    assert(list);
    assert(path);
    if( !*path )
        return -1;

    /*
     * Canonicalise before anything else.
     *
     * The same cache reaches this by several spellings — the command line's
     * relative path, discovery's `<root>/name`, and whatever the user typed —
     * and a textual comparison calls those three different caches. They then
     * fill the list, and because the list is bounded, the *real* additions
     * start failing. That looks like "add is broken", not "add is confused
     * about identity".
     */
    if( !realpath(path, resolved) )
        snprintf(resolved, sizeof(resolved), "%s", path);

    if( !dir_has_cache(resolved) )
        return -1;

    for( int i = 0; i < list->count; i++ )
        if( strcmp(list->items[i].path, resolved) == 0 )
            return i; /* already listed; not an error */

    if( list->count >= EV_MAX_CACHES )
        return -1;

    struct EV_CacheEntry* e = &list->items[list->count];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", resolved);
    snprintf(e->label, sizeof(e->label), "%s", basename_of(resolved));

    if( rev && *rev )
        snprintf(e->rev, sizeof(e->rev), "%s", rev);
    else if( !ev_cache_detect_rev(resolved, e->rev, (int)sizeof(e->rev)) )
        snprintf(e->rev, sizeof(e->rev), "osrs239");

    return list->count++;
}

bool
ev_caches_set_rev(struct EV_CacheList* list, int index, const char* rev)
{
    assert(list);
    if( index < 0 || index >= list->count || !rev || !*rev )
        return false;
    snprintf(list->items[index].rev, sizeof(list->items[index].rev), "%s", rev);
    /* The counts came from a decode under the OLD profile, so they are no
     * longer answers about this entry. */
    list->items[index].indexed = false;
    list->items[index].npc_count = 0;
    list->items[index].seq_count = 0;
    list->items[index].model_count = 0;
    return true;
}

bool
ev_caches_remove(struct EV_CacheList* list, int index)
{
    assert(list);
    if( index < 0 || index >= list->count )
        return false;
    for( int i = index; i + 1 < list->count; i++ )
        list->items[i] = list->items[i + 1];
    list->count--;

    if( list->active == index )
        list->active = -1;
    else if( list->active > index )
        list->active--;
    return true;
}

int
ev_caches_discover(struct EV_CacheList* list, const char* root)
{
    DIR* d = opendir(root);
    if( !d )
        return 0;

    int added = 0;
    struct dirent* ent;
    while( (ent = readdir(d)) != NULL )
    {
        if( ent->d_name[0] == '.' )
            continue;
        char path[EV_CACHE_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);

        struct stat st;
        if( stat(path, &st) != 0 || !S_ISDIR(st.st_mode) )
            continue;
        if( !dir_has_cache(path) )
            continue;

        int before = list->count;
        if( ev_caches_add(list, path, NULL) >= 0 && list->count > before )
            added++;
    }
    closedir(d);
    return added;
}

/* ---- the index ----------------------------------------------------------- */

/*
 * Enumerating records, which is layout-dependent in two ways at once.
 *
 * `RSCache_RecordAddressFor` answers "where does this type live", and its answer
 * has three shapes:
 *
 *   sharded (RS2 npc/seq/loc/...)   group_shift != 0; the record id splits into
 *                                   a group archive and a file inside it.
 *   config group (OldSchool)        group_shift == 0 and `group` names ONE
 *                                   archive in the configs table whose FILES are
 *                                   the records. Enumerating the reference
 *                                   table's ids here lists config *kinds*, not
 *                                   records — which is how a first attempt
 *                                   reported 45 models.
 *   its own table (models)          not a config record at all. The type has no
 *                                   sharded mapping, so RecordAddressFor falls
 *                                   back to the config layout and must not be
 *                                   used; the table id IS the record id.
 *
 * Getting this wrong does not fail loudly. It returns a short, plausible list.
 */

/** Ids of a type that owns a whole table, where group id == record id. */
static int*
collect_table_ids(
    struct RSCache_Dat2Disk* disk,
    int logical_table,
    int* out_count)
{
    *out_count = 0;
    int table_id = RSCache_Dat2DiskTableId(disk, logical_table);
    if( table_id < 0 )
        return NULL;

    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !ref )
        return NULL;
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
        return NULL;

    int* ids = (int*)malloc((size_t)(table->id_count > 0 ? table->id_count : 1) * sizeof(int));
    assert(ids);
    for( int i = 0; i < table->id_count; i++ )
        ids[i] = table->ids[i];
    *out_count = table->id_count;

    RSCache_ReferenceTableFree(table);
    return ids;
}

/**
 * Walk every record of a config type, sharded or grouped, calling `visit`.
 *
 * One walker for both layouts, because the difference is entirely in how an id
 * is formed and every caller wants the same thing out.
 */
typedef void (*record_visitor)(int id, const char* data, int size, void* user);

static void
walk_records(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    enum RSCache_Type type,
    record_visitor visit,
    void* user)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, type);
    int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
    if( table_id < 0 )
        return;

    if( addr.group_shift == 0 )
    {
        /* One archive; its files are the records and the file id is the id. */
        struct RSCache_Dat2DiskArchive* a =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, addr.group);
        if( !a )
            return;
        RSCache_Dat2DiskArchiveInitMetadata(disk, a);
        struct RSCache_FileList* fl =
            RSCache_FileListNewFromDecode(a->data, a->data_size, a->file_count);
        if( fl )
        {
            for( int f = 0; f < fl->file_count; f++ )
            {
                if( fl->file_sizes[f] <= 0 )
                    continue;
                int id = a->file_ids ? a->file_ids[f] : f;
                visit(id, fl->files[f], fl->file_sizes[f], user);
            }
            RSCache_FileListFree(fl);
        }
        RSCache_Dat2DiskArchiveFree(a);
        return;
    }

    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !ref )
        return;
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
        return;

    for( int i = 0; i < table->id_count; i++ )
    {
        struct RSCache_Dat2DiskArchive* a =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, table->ids[i]);
        if( !a )
            continue;
        RSCache_Dat2DiskArchiveInitMetadata(disk, a);
        struct RSCache_FileList* fl =
            RSCache_FileListNewFromDecode(a->data, a->data_size, a->file_count);
        if( fl )
        {
            for( int f = 0; f < fl->file_count; f++ )
            {
                if( fl->file_sizes[f] <= 0 )
                    continue;
                int file_id = a->file_ids ? a->file_ids[f] : f;
                int id = (table->ids[i] << addr.group_shift) | file_id;
                visit(id, fl->files[f], fl->file_sizes[f], user);
            }
            RSCache_FileListFree(fl);
        }
        RSCache_Dat2DiskArchiveFree(a);
    }
    RSCache_ReferenceTableFree(table);
}

struct id_sink
{
    int* ids;
    int count;
    int cap;
};

static void
sink_id(int id, const char* data, int size, void* user)
{
    (void)data;
    (void)size;
    struct id_sink* s = (struct id_sink*)user;
    if( s->count == s->cap )
    {
        int grown = s->cap ? s->cap * 2 : 1024;
        int* p = (int*)realloc(s->ids, (size_t)grown * sizeof(int));
        assert(p);
        s->ids = p;
        s->cap = grown;
    }
    s->ids[s->count++] = id;
}

/** npc ids AND their names, which needs each record decoded. */
struct npc_sink
{
    struct EV_IndexNpc* rows;
    int count;
    int cap;
    const struct RSCache* profile;
};

static void
sink_npc(int id, const char* data, int size, void* user)
{
    struct npc_sink* s = (struct npc_sink*)user;
    struct RSCache_Dat2ConfigNpc* npc =
        RSCache_Dat2ConfigNpcNewDecodeProfile(s->profile, (char*)data, size);
    if( !npc )
        return;

    if( s->count == s->cap )
    {
        int grown = s->cap ? s->cap * 2 : 1024;
        struct EV_IndexNpc* p =
            (struct EV_IndexNpc*)realloc(s->rows, (size_t)grown * sizeof(*p));
        if( !p )
            return;
        s->rows = p;
        s->cap = grown;
    }
    s->rows[s->count].id = id;
    s->rows[s->count].name = npc->name ? strdup(npc->name) : NULL;
    s->count++;
}

/* ---- the on-disk index --------------------------------------------------- */

/*
 * Bumped whenever the serialised layout or what goes into it changes.
 *
 * Without it, an older file is read back into a newer struct and the counts
 * land on the wrong fields — a corrupt index rather than a rejected one, and
 * the symptom is npcs with impossible ids rather than an error.
 */
#define EV_INDEX_MAGIC 0x58495645u /* "EVIX" */
#define EV_INDEX_VERSION 2u

/*
 * The length written for a name that is NULL, as distinct from one that is the
 * empty string.
 *
 * Encoding both as 0 makes the round-trip lossy: npc 325 in cache.osrs239 has a
 * name of "", and it came back absent. Nothing downstream cares much today —
 * the search treats them alike — but a store that silently edits what it was
 * given is a trap, and this one is one sentinel wide.
 */
#define EV_INDEX_NAME_ABSENT 0xFFFFFFFFu

uint64_t
ev_cache_fingerprint(const char* cache_dir)
{
    /* FNV-1a over each file's name, size and mtime. Order is fixed by the loop
     * rather than by readdir, so the same directory always folds identically. */
    uint64_t h = 1469598103934665603ull;
    char path[EV_CACHE_PATH_MAX];
    struct stat st;
    int seen = 0;

    assert(cache_dir);

#define EV_FOLD(bytes, n)                                                      \
    do                                                                         \
    {                                                                          \
        const unsigned char* b_ = (const unsigned char*)(bytes);               \
        for( size_t i_ = 0; i_ < (size_t)(n); i_++ )                           \
        {                                                                      \
            h ^= b_[i_];                                                       \
            h *= 1099511628211ull;                                             \
        }                                                                      \
    } while( 0 )

    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", cache_dir);
    if( stat(path, &st) == 0 )
    {
        int64_t size = (int64_t)st.st_size;
        int64_t mtime = (int64_t)st.st_mtime;
        EV_FOLD(&size, sizeof(size));
        EV_FOLD(&mtime, sizeof(mtime));
        seen++;
    }

    /*
     * 0..255 covers every index a dat2 cache can have, plus 255 (the master
     * index). Missing ones simply do not fold — a cache that later GAINS an idx
     * therefore changes the fingerprint, which is the point.
     */
    for( int i = 0; i <= 255; i++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", cache_dir, i);
        if( stat(path, &st) != 0 )
            continue;
        int32_t id = i;
        int64_t size = (int64_t)st.st_size;
        int64_t mtime = (int64_t)st.st_mtime;
        EV_FOLD(&id, sizeof(id));
        EV_FOLD(&size, sizeof(size));
        EV_FOLD(&mtime, sizeof(mtime));
        seen++;
    }
#undef EV_FOLD

    /* Nothing found is not a fingerprint. Returning the empty-hash seed would
     * make two different unreadable directories agree, so any stored index
     * would validate against the wrong cache. */
    return seen ? h : 0;
}

static void
index_path(const char* cache_dir, const char* rev, char* out, size_t cap)
{
    snprintf(out, cap, "%s/%s/index-%s.evi", cache_dir, EV_INDEX_DIR, rev && *rev ? rev : "unknown");
}

static bool
read_exact(FILE* f, void* dst, size_t n)
{
    return fread(dst, 1, n, f) == n;
}

static bool
read_u32(FILE* f, uint32_t* out)
{
    return read_exact(f, out, sizeof(*out));
}

/** Returns false — and leaves `out` empty — for a missing, stale or malformed
 *  file. All three mean the same thing to the caller: build it. */
static bool
index_load(const char* cache_dir, const char* rev, uint64_t fingerprint, struct EV_Index* out)
{
    char path[EV_CACHE_PATH_MAX];
    uint32_t magic = 0, version = 0;
    uint64_t stored = 0;
    uint32_t npc_count = 0, seq_count = 0, model_count = 0;

    memset(out, 0, sizeof(*out));
    if( !fingerprint )
        return false;

    index_path(cache_dir, rev, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if( !f )
        return false;

    if( !read_u32(f, &magic) || magic != EV_INDEX_MAGIC ||
        !read_u32(f, &version) || version != EV_INDEX_VERSION ||
        !read_exact(f, &stored, sizeof(stored)) || stored != fingerprint ||
        !read_u32(f, &npc_count) || !read_u32(f, &seq_count) || !read_u32(f, &model_count) )
    {
        fclose(f);
        return false;
    }

    /*
     * A truncated file must not be accepted as a short index.
     *
     * Every allocation below is sized from a count in the header, so a file cut
     * off mid-write would otherwise load as "this cache has 4000 npcs" and the
     * missing ones would look like a decoder that lost them. Any short read
     * aborts the whole load.
     */
    bool ok = true;

    if( seq_count )
    {
        out->seq_ids = malloc((size_t)seq_count * sizeof(*out->seq_ids));
        ok = out->seq_ids && read_exact(f, out->seq_ids, (size_t)seq_count * sizeof(*out->seq_ids));
        if( ok )
            out->seq_count = (int)seq_count;
    }

    if( ok && model_count )
    {
        out->model_ids = malloc((size_t)model_count * sizeof(*out->model_ids));
        ok = out->model_ids &&
             read_exact(f, out->model_ids, (size_t)model_count * sizeof(*out->model_ids));
        if( ok )
            out->model_count = (int)model_count;
    }

    if( ok && npc_count )
    {
        out->npcs = calloc((size_t)npc_count, sizeof(*out->npcs));
        ok = out->npcs != NULL;
        for( uint32_t i = 0; ok && i < npc_count; i++ )
        {
            int32_t id = 0;
            uint32_t len = 0;
            if( !read_exact(f, &id, sizeof(id)) || !read_u32(f, &len) ||
                (len != EV_INDEX_NAME_ABSENT && len > 4096u) )
            {
                ok = false;
                break;
            }
            out->npcs[i].id = id;
            if( len != EV_INDEX_NAME_ABSENT )
            {
                out->npcs[i].name = malloc((size_t)len + 1);
                if( !out->npcs[i].name || (len && !read_exact(f, out->npcs[i].name, len)) )
                {
                    ok = false;
                    break;
                }
                out->npcs[i].name[len] = '\0';
            }
            /* Counted as we go, so a mid-list failure frees exactly what was
             * built rather than walking uninitialised slots. */
            out->npc_count = (int)i + 1;
        }
    }

    fclose(f);
    if( !ok )
    {
        ev_index_free(out);
        return false;
    }
    return true;
}

static bool
write_exact(FILE* f, const void* src, size_t n)
{
    return fwrite(src, 1, n, f) == n;
}

/**
 * Write the index beside the cache. Best effort.
 *
 * A read-only or otherwise unwritable cache directory is a normal thing to point
 * this at — a mounted share, a reference copy — and refusing to serve it would
 * be worse than rebuilding the index each time. So a failure here is reported
 * once and otherwise ignored.
 *
 * Written to a temporary and renamed, because the reader validates a fingerprint
 * and a half-written file can carry a correct one: an interrupted write would
 * otherwise leave a file that passes validation and is short.
 */
static bool
index_save(const char* cache_dir, const char* rev, uint64_t fingerprint, const struct EV_Index* ix)
{
    char dir[EV_CACHE_PATH_MAX];
    char path[EV_CACHE_PATH_MAX];
    char tmp[EV_CACHE_PATH_MAX];

    if( !fingerprint )
        return false;

    snprintf(dir, sizeof(dir), "%s/%s", cache_dir, EV_INDEX_DIR);
    if( mkdir(dir, 0777) != 0 && errno != EEXIST )
        return false;

    /*
     * Make the directory ignore itself.
     *
     * This repo already ignores its own `cache.` directories, but a cache can
     * be added from
     * anywhere — including somewhere inside a working tree that does not. A
     * derived file that can be rebuilt in a tenth of a second has no business
     * showing up in `git status`, and the user cannot be expected to add an
     * ignore rule for a directory they did not create.
     */
    snprintf(path, sizeof(path), "%s/.gitignore", dir);
    if( access(path, F_OK) != 0 )
    {
        FILE* gi = fopen(path, "wb");
        if( gi )
        {
            fputs("# Derived: the entity viewer's cache index, rebuilt on demand.\n*\n", gi);
            fclose(gi);
        }
    }

    index_path(cache_dir, rev, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* f = fopen(tmp, "wb");
    if( !f )
        return false;

    uint32_t magic = EV_INDEX_MAGIC;
    uint32_t version = EV_INDEX_VERSION;
    uint32_t npc_count = (uint32_t)ix->npc_count;
    uint32_t seq_count = (uint32_t)ix->seq_count;
    uint32_t model_count = (uint32_t)ix->model_count;

    bool ok = write_exact(f, &magic, sizeof(magic)) &&
              write_exact(f, &version, sizeof(version)) &&
              write_exact(f, &fingerprint, sizeof(fingerprint)) &&
              write_exact(f, &npc_count, sizeof(npc_count)) &&
              write_exact(f, &seq_count, sizeof(seq_count)) &&
              write_exact(f, &model_count, sizeof(model_count));

    if( ok && seq_count )
        ok = write_exact(f, ix->seq_ids, (size_t)seq_count * sizeof(*ix->seq_ids));
    if( ok && model_count )
        ok = write_exact(f, ix->model_ids, (size_t)model_count * sizeof(*ix->model_ids));

    for( uint32_t i = 0; ok && i < npc_count; i++ )
    {
        int32_t id = ix->npcs[i].id;
        const char* name = ix->npcs[i].name;
        uint32_t len = name ? (uint32_t)strlen(name) : EV_INDEX_NAME_ABSENT;
        ok = write_exact(f, &id, sizeof(id)) && write_exact(f, &len, sizeof(len)) &&
             (len == 0 || len == EV_INDEX_NAME_ABSENT || write_exact(f, name, len));
    }

    if( fclose(f) != 0 )
        ok = false;

    if( !ok || rename(tmp, path) != 0 )
    {
        remove(tmp);
        return false;
    }
    return true;
}

/* The uncached build, kept separate so the caching path reads as a wrapper. */
static bool
index_build_fresh(
    struct Tool_Dat2Cache* cache,
    const struct RSCache* profile,
    const char* cache_dir,
    struct EV_Index* out)
{
    (void)cache;
    memset(out, 0, sizeof(*out));

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
        return false;
    struct RSCache local = *profile;
    RSCache_Dat2DiskSetProfile(disk, &local);

    struct npc_sink npcs = { NULL, 0, 0, &local };
    walk_records(disk, &local, RSCACHE_TYPE_NPC, sink_npc, &npcs);
    out->npcs = npcs.rows;
    out->npc_count = npcs.count;

    struct id_sink seqs = { NULL, 0, 0 };
    walk_records(disk, &local, RSCACHE_TYPE_SEQUENCE, sink_id, &seqs);
    out->seq_ids = seqs.ids;
    out->seq_count = seqs.count;

    /* Models own a table; the group id IS the model id. */
    out->model_ids =
        collect_table_ids(disk, RSCACHE_DAT2_TABLE_MODELS, &out->model_count);

    RSCache_Dat2DiskFree(disk);
    return true;
}

bool
ev_index_build(
    struct Tool_Dat2Cache* cache,
    const struct RSCache* profile,
    const char* cache_dir,
    const char* rev,
    struct EV_Index* out)
{
    assert(profile);
    assert(cache_dir);
    assert(rev);
    assert(out);

    uint64_t fingerprint = ev_cache_fingerprint(cache_dir);

    if( index_load(cache_dir, rev, fingerprint, out) )
        return true;

    if( !index_build_fresh(cache, profile, cache_dir, out) )
        return false;

    if( !index_save(cache_dir, rev, fingerprint, out) )
        fprintf(
            stderr,
            "index: could not write %s/%s — it will be rebuilt on each start\n",
            cache_dir, EV_INDEX_DIR);
    return true;
}

void
ev_index_free(struct EV_Index* index)
{
    if( !index )
        return;
    for( int i = 0; i < index->npc_count; i++ )
        free(index->npcs[i].name);
    free(index->npcs);
    free(index->seq_ids);
    free(index->model_ids);
    memset(index, 0, sizeof(*index));
}
