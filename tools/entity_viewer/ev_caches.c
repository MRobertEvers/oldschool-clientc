/*
 * The viewer's cache registry and per-cache index. See ev_caches.h for why.
 */

#include "ev_caches.h"
#include <assert.h>

#include "dat2disk.h"
#include "datatypes/dat2_config_npc.h"
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
 * distinguish profiles that differ in ways an npc record cannot show.
 */
static const char* const RS2_CANDIDATES[] = { "rs727", "rs643", "rs558", "rs530" };
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
 * How many of the first `limit` npc records decode to exact consumption.
 *
 * Exact consumption is the signal that matters: a wrong profile usually still
 * *decodes* — it stops early on a byte it read as a terminator — so "did it
 * return non-NULL" separates nothing. Landing exactly on the end of every
 * record is what a right profile does and a wrong one does not.
 */
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

    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&local, RSCACHE_TYPE_NPC);
    int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
    if( table_id < 0 )
    {
        RSCache_Dat2DiskFree(disk);
        unmute_stdout(&mute);
        return -1;
    }

    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !ref )
    {
        RSCache_Dat2DiskFree(disk);
        unmute_stdout(&mute);
        return -1;
    }
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
    {
        RSCache_Dat2DiskFree(disk);
        unmute_stdout(&mute);
        return -1;
    }

    int seen = 0;
    int exact = 0;
    for( int i = 0; i < table->id_count && seen < limit; i++ )
    {
        struct RSCache_Dat2DiskArchive* a =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, table->ids[i]);
        if( !a )
            continue;
        RSCache_Dat2DiskArchiveInitMetadata(disk, a);

        if( addr.group_shift == 0 )
        {
            struct RSCache_Dat2ConfigNpc* n =
                RSCache_Dat2ConfigNpcNewDecodeProfile(&local, a->data, a->data_size);
            seen++;
            if( n && n->_consumed == a->data_size )
                exact++;
        }
        else
        {
            struct RSCache_FileList* fl =
                RSCache_FileListNewFromDecode(a->data, a->data_size, a->file_count);
            if( fl )
            {
                for( int f = 0; f < fl->file_count && seen < limit; f++ )
                {
                    if( fl->file_sizes[f] <= 0 )
                        continue;
                    struct RSCache_Dat2ConfigNpc* n = RSCache_Dat2ConfigNpcNewDecodeProfile(
                        &local, (char*)fl->files[f], fl->file_sizes[f]);
                    seen++;
                    if( n && n->_consumed == fl->file_sizes[f] )
                        exact++;
                }
                RSCache_FileListFree(fl);
            }
        }
        RSCache_Dat2DiskArchiveFree(a);
    }

    RSCache_ReferenceTableFree(table);
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

    for( int l = 0; l < 2; l++ )
    {
        for( int i = 0; i < counts[l]; i++ )
        {
            struct RSCache p;
            if( !RSCache_ProfileByName(lists[l][i], &p) )
                continue;
            int score = score_profile(path, &p, 240);
            if( score > best_score )
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

bool
ev_index_build(
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
