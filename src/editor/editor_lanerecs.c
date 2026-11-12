/*
 * The lane's own records in the editor's client: editor_lanerecs.h.
 */

#include "editor_lanerecs.h"

#include "editor_alloc.h"
#include "editor_host.h"
#include "engine/cache_provider.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LANEREC_KINDS 3
/** Frames between tries at a record whose names the catalog could not answer
 *  yet (its index was still being built). */
#define LANEREC_RETRY_TICKS 30

static const char* const k_lanerec_types[LANEREC_KINDS] = { "loc", "npc", "obj" };

struct LaneRec
{
    int kind;
    int id;
    char name[EDITOR_CMD_NAME_MAX];
    /** The CFG document revision the provider's record was decoded from. */
    uint32_t revision;
    /** The provider holds a record this module made. */
    int defined;
    /** A name in the block met a catalog still building: decode again. */
    int busy;
};

/** A miss answered "nobody has it", good while the kind's ledger stands. */
struct LaneRecMiss
{
    int kind;
    int id;
};

static struct
{
    struct Editor* editor;
    struct CacheProvider* provider;
    struct LaneRec* recs;
    int count;
    int capacity;
    struct LaneRecMiss* misses;
    int miss_count;
    int miss_capacity;
    /** Per kind: the ledger revision the remembered misses were answered
     *  under (UINT32_MAX: none remembered). */
    uint32_t miss_revision[LANEREC_KINDS];
    /** The resolver met EDITOR_HOST_BUSY during the decode in flight. */
    int resolve_busy;
    int retry_in;
    /** A defined record changed or went since the last Tick answered. */
    int changed;
} g_lanerecs;

/** The loaded mirror document (kind, key), opened from the authority when
 *  this mirror does not hold it. NULL when it cannot be opened. */
static struct Editor_TextDoc*
lanerecs_doc(
    enum Editor_DocKind kind,
    const char* key)
{
    struct Editor_DocHandle handle;

    if( !(Editor_DocFind(&g_lanerecs.editor->doc, kind, key, &handle) && Editor_DocHandleLoaded(&handle)) )
    {
        if( !Editor_OpenDoc(g_lanerecs.editor, kind, key) )
            return NULL;
        if( !Editor_DocFind(&g_lanerecs.editor->doc, kind, key, &handle) )
            return NULL;
    }
    return handle.text;
}

static struct Editor_TextDoc*
lanerecs_ledger(int kind)
{
    char key[EDITOR_DOC_KEY_MAX];

    if( !Editor_AllocDocKey(k_lanerec_types[kind], "alloc", key, sizeof(key)) )
        return NULL;
    return lanerecs_doc(EDITOR_DOC_TEXT, key);
}

/** Editor_RecDefResolveFn over the server's catalog. */
static int
lanerecs_resolve(
    void* user,
    const char* type,
    const char* name)
{
    struct EditorHost_CatalogRecord record;
    enum EditorHost_Status status;
    uint32_t kind = TORIRSMAPED_CATALOG_KIND_COUNT;
    int id = -1;

    (void)user;
    if( !(g_lanerecs.editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return -1;
    for( uint32_t k = 0; k < TORIRSMAPED_CATALOG_KIND_COUNT; k++ )
        if( strcmp(ToriRSMapEdCatalog_KindName(k), type) == 0 )
            kind = k;
    if( kind == TORIRSMAPED_CATALOG_KIND_COUNT )
        return -1;
    memset(&record, 0, sizeof(record));
    status = Editor_HostMapEdCatalogRecord(&g_lanerecs.editor->host, kind, -1, name, &record);
    if( status == EDITOR_HOST_OK )
        id = record.id;
    else if( status == EDITOR_HOST_BUSY )
        g_lanerecs.resolve_busy = 1;
    Editor_HostCatalogRecordFree(&record);
    return id;
}

static struct LaneRec*
lanerecs_find(
    int kind,
    int id)
{
    for( int i = 0; i < g_lanerecs.count; i++ )
        if( g_lanerecs.recs[i].kind == kind && g_lanerecs.recs[i].id == id )
            return &g_lanerecs.recs[i];
    return NULL;
}

static struct LaneRec*
lanerecs_slot(
    int kind,
    int id)
{
    struct LaneRec* rec = lanerecs_find(kind, id);

    if( rec )
        return rec;
    if( g_lanerecs.count == g_lanerecs.capacity )
    {
        int const grown = g_lanerecs.capacity ? g_lanerecs.capacity * 2 : 16;
        g_lanerecs.recs = ToriMem_Realloc(ToriMem_Editor(), g_lanerecs.recs, (size_t)grown * sizeof(*g_lanerecs.recs));
        assert(g_lanerecs.recs);
        g_lanerecs.capacity = grown;
    }
    rec = &g_lanerecs.recs[g_lanerecs.count++];
    memset(rec, 0, sizeof(*rec));
    rec->kind = kind;
    rec->id = id;
    return rec;
}

/** Decode `doc` as record (kind, id) and hand it to the provider. 1 when it
 *  did; 0 when the document holds no field (no such record). */
static int
lanerecs_decode(
    int kind,
    int id,
    const struct Editor_TextDoc* doc,
    struct Editor_RecDefReport* report)
{
    struct Editor_RecDefContext ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.profile = CacheProvider_Profile(g_lanerecs.provider);
    ctx.resolve = lanerecs_resolve;
    g_lanerecs.resolve_busy = 0;
    if( kind == CACHEPROVIDER_CATALOG_LOC )
    {
        struct ToriRS_Location* loc = Editor_RecDefLoc(&ctx, id, doc->text, doc->length, report);
        if( !loc )
            return 0;
        CacheProvider_LocationReplace(g_lanerecs.provider, id, loc);
    }
    else if( kind == CACHEPROVIDER_CATALOG_NPC )
    {
        struct ToriRS_Npctype* npc = Editor_RecDefNpc(&ctx, id, doc->text, doc->length, report);
        if( !npc )
            return 0;
        CacheProvider_NpctypeReplace(g_lanerecs.provider, id, npc);
    }
    else
    {
        struct ToriRS_Objtype* obj = Editor_RecDefObj(&ctx, id, doc->text, doc->length, report);
        if( !obj )
            return 0;
        CacheProvider_ObjtypeReplace(g_lanerecs.provider, id, obj);
    }
    return 1;
}

/** The provider's records (kind, id) and (kind, like_id) compared into
 *  `report` (Editor_RecDefReport::differs). Nothing when either is not
 *  resident. */
static void
lanerecs_compare(
    int kind,
    int id,
    int like_id,
    struct Editor_RecDefReport* report)
{
    if( kind == CACHEPROVIDER_CATALOG_LOC )
    {
        const struct ToriRS_Location* made = CacheProvider_LocationGet(g_lanerecs.provider, id);
        const struct ToriRS_Location* like = CacheProvider_LocationGet(g_lanerecs.provider, like_id);
        if( made && like )
            report->differs = Editor_RecDefLocDiff(made, like, report->differs_in, sizeof(report->differs_in));
    }
    else if( kind == CACHEPROVIDER_CATALOG_NPC )
    {
        const struct ToriRS_Npctype* made = CacheProvider_NpctypeGet(g_lanerecs.provider, id);
        const struct ToriRS_Npctype* like = CacheProvider_NpctypeGet(g_lanerecs.provider, like_id);
        if( made && like )
            report->differs = Editor_RecDefNpcDiff(made, like, report->differs_in, sizeof(report->differs_in));
    }
    else
    {
        const struct ToriRS_Objtype* made = CacheProvider_ObjtypeGet(g_lanerecs.provider, id);
        const struct ToriRS_Objtype* like = CacheProvider_ObjtypeGet(g_lanerecs.provider, like_id);
        if( made && like )
            report->differs = Editor_RecDefObjDiff(made, like, report->differs_in, sizeof(report->differs_in));
    }
}

int
Editor_LaneRecords_Define(
    int kind,
    int id,
    const char* name,
    int like_id,
    struct Editor_RecDefReport* report)
{
    struct Editor_RecDefReport local;
    struct Editor_TextDoc* doc;
    struct LaneRec* rec;
    char key[EDITOR_DOC_KEY_MAX];
    int written;

    assert(kind >= 0);
    assert(kind < LANEREC_KINDS);
    assert(id >= 0);
    assert(name);
    assert(name[0]);
    if( !report )
        report = &local;
    memset(report, 0, sizeof(*report));
    if( !g_lanerecs.editor || !g_lanerecs.editor->host_open )
        return 0;
    written = snprintf(key, sizeof(key), "%s:%s", k_lanerec_types[kind], name);
    if( written <= 0 || (size_t)written >= sizeof(key) || strlen(name) >= sizeof(rec->name) )
        return 0;
    doc = lanerecs_doc(EDITOR_DOC_CFG, key);
    if( !doc || !lanerecs_decode(kind, id, doc, report) )
        return 0;
    rec = lanerecs_slot(kind, id);
    snprintf(rec->name, sizeof(rec->name), "%s", name);
    rec->revision = doc->revision;
    rec->defined = 1;
    rec->busy = g_lanerecs.resolve_busy;
    if( like_id >= 0 && like_id != id )
        lanerecs_compare(kind, id, like_id, report);
    if( report->bad > 0 )
        TORIRS_REPORT("editor: %s %s (%d) drawn from its text with %d field(s) unread, first %s\n",
            k_lanerec_types[kind], name, id, report->bad, report->first_bad);
    return 1;
}

/** The provider's miss (CacheProvider_SetAbsentRecordSource). */
static int
lanerecs_absent(
    void* user,
    int kind,
    int id)
{
    struct Editor_TextDoc* ledger;
    char name[EDITOR_CMD_NAME_MAX];

    (void)user;
    assert(kind >= 0);
    assert(kind < LANEREC_KINDS);
    if( !g_lanerecs.editor || !g_lanerecs.editor->host_open || id < 0 )
        return 0;
    ledger = lanerecs_ledger(kind);
    if( !ledger )
        return 0;
    /* "Not found" is an answer, kept until the ledger that gave it moves: a
     * ghost over an id nobody defines asks every frame. */
    if( g_lanerecs.miss_revision[kind] != ledger->revision )
    {
        int kept = 0;
        for( int i = 0; i < g_lanerecs.miss_count; i++ )
            if( g_lanerecs.misses[i].kind != kind )
                g_lanerecs.misses[kept++] = g_lanerecs.misses[i];
        g_lanerecs.miss_count = kept;
        g_lanerecs.miss_revision[kind] = ledger->revision;
    }
    for( int i = 0; i < g_lanerecs.miss_count; i++ )
        if( g_lanerecs.misses[i].kind == kind && g_lanerecs.misses[i].id == id )
            return 0;
    if( Editor_RecDefLedgerName(ledger->text, ledger->length, id, name, sizeof(name))
        && Editor_LaneRecords_Define(kind, id, name, -1, NULL) )
        return 1;
    if( g_lanerecs.miss_count == g_lanerecs.miss_capacity )
    {
        int const grown = g_lanerecs.miss_capacity ? g_lanerecs.miss_capacity * 2 : 32;
        g_lanerecs.misses =
            ToriMem_Realloc(ToriMem_Editor(), g_lanerecs.misses, (size_t)grown * sizeof(*g_lanerecs.misses));
        assert(g_lanerecs.misses);
        g_lanerecs.miss_capacity = grown;
    }
    g_lanerecs.misses[g_lanerecs.miss_count].kind = kind;
    g_lanerecs.misses[g_lanerecs.miss_count].id = id;
    g_lanerecs.miss_count++;
    return 0;
}

void
Editor_LaneRecords_Attach(
    struct Editor* editor,
    struct CacheProvider* provider)
{
    assert(editor);
    assert(provider);
    Editor_LaneRecords_Detach();
    g_lanerecs.editor = editor;
    g_lanerecs.provider = provider;
    for( int k = 0; k < LANEREC_KINDS; k++ )
        g_lanerecs.miss_revision[k] = UINT32_MAX;
    CacheProvider_SetAbsentRecordSource(provider, lanerecs_absent, NULL);
}

void
Editor_LaneRecords_Detach(void)
{
    if( g_lanerecs.provider )
        CacheProvider_SetAbsentRecordSource(g_lanerecs.provider, NULL, NULL);
    ToriMem_Free(ToriMem_Editor(), g_lanerecs.recs);
    ToriMem_Free(ToriMem_Editor(), g_lanerecs.misses);
    memset(&g_lanerecs, 0, sizeof(g_lanerecs));
}

int
Editor_LaneRecords_Tick(void)
{
    int changed;
    int retry = 0;

    if( !g_lanerecs.editor || !g_lanerecs.editor->host_open )
        return 0;
    if( g_lanerecs.retry_in > 0 )
        g_lanerecs.retry_in--;
    else
    {
        retry = 1;
        g_lanerecs.retry_in = LANEREC_RETRY_TICKS;
    }
    for( int i = 0; i < g_lanerecs.count; i++ )
    {
        struct LaneRec* rec = &g_lanerecs.recs[i];
        struct Editor_DocHandle handle;
        char key[EDITOR_DOC_KEY_MAX];

        snprintf(key, sizeof(key), "%s:%s", k_lanerec_types[rec->kind], rec->name);
        if( !Editor_DocFind(&g_lanerecs.editor->doc, EDITOR_DOC_CFG, key, &handle) || !handle.text )
            continue;
        if( handle.text->revision == rec->revision && !(rec->busy && retry) )
            continue;
        rec->revision = handle.text->revision;
        if( lanerecs_decode(rec->kind, rec->id, handle.text, NULL) )
        {
            rec->defined = 1;
            rec->busy = g_lanerecs.resolve_busy;
            g_lanerecs.changed = 1;
        }
        else if( rec->defined )
        {
            /* The block went (the copy was undone): there is no such record
             * any more, and the client must not go on placing it. A redo
             * brings the block back and the next tick defines it again. */
            (void)CacheProvider_RecordRetire(
                g_lanerecs.provider, (enum CacheProvider_CatalogKind)rec->kind, rec->id);
            rec->defined = 0;
            g_lanerecs.changed = 1;
        }
    }
    changed = g_lanerecs.changed;
    g_lanerecs.changed = 0;
    return changed;
}

int
Editor_LaneRecords_Name(
    int kind,
    int id,
    char* name,
    size_t name_size)
{
    const struct LaneRec* rec = lanerecs_find(kind, id);

    if( !rec || !rec->defined )
        return 0;
    if( name )
    {
        assert(name_size > 0);
        snprintf(name, name_size, "%s", rec->name);
    }
    return 1;
}

int
Editor_LaneRecords_Gone(
    int kind,
    int id,
    char* name,
    size_t name_size)
{
    const struct LaneRec* rec = lanerecs_find(kind, id);

    if( !rec || rec->defined )
        return 0;
    if( name )
    {
        assert(name_size > 0);
        snprintf(name, name_size, "%s", rec->name);
    }
    return 1;
}
