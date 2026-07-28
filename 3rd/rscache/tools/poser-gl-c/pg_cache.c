#include "pg_cache.h"

#include "asset_access.h"
#include "cache_edit.h"
#include "rscache.h"
#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PG_Cache
{
    struct RSCache profile;
    struct Tool_Dat2Cache dat2;
    char path[1024];
    char rev[64];
    bool higher_rev;

    struct PG_NpcDef* npcs;
    int npc_count;
    struct PG_ItemDef* items;
    int item_count;
    struct PG_IdkDef* idks;
    int idk_count;
    struct PG_SeqDef* seqs;
    int seq_count;

    struct PG_FrameArchive* archives;
    int archive_count;
    int archive_capacity;
};

/* ---- small helpers ------------------------------------------------------- */

static char*
dup_string(const char* s)
{
    size_t n;
    char* out;
    if( !s )
        return NULL;
    n = strlen(s) + 1;
    out = malloc(n);
    if( out )
        memcpy(out, s, n);
    return out;
}

static int*
dup_ints(const int* src, int count)
{
    int* out;
    if( count <= 0 || !src )
        return NULL;
    out = malloc((size_t)count * sizeof(int));
    if( out )
        memcpy(out, src, (size_t)count * sizeof(int));
    return out;
}

static uint16_t*
dup_shorts_as_u16(const short* src, int count)
{
    uint16_t* out;
    if( count <= 0 || !src )
        return NULL;
    out = malloc((size_t)count * sizeof(uint16_t));
    if( !out )
        return NULL;
    for( int i = 0; i < count; i++ )
        out[i] = (uint16_t)src[i];
    return out;
}

static uint16_t*
dup_ints_as_u16(const int* src, int count)
{
    uint16_t* out;
    if( count <= 0 || !src )
        return NULL;
    out = malloc((size_t)count * sizeof(uint16_t));
    if( !out )
        return NULL;
    for( int i = 0; i < count; i++ )
        out[i] = (uint16_t)src[i];
    return out;
}

/* ---- generic config walk -------------------------------------------------- */

/*
 * Every record of one config type, decoded in a single pass over its archives.
 *
 * asset_access has a per-id loader, but it reloads and re-decodes the whole
 * archive for each record; the lists here want all fourteen thousand npcs at
 * startup, which turns that into fourteen thousand archive decodes. Walking the
 * archives once instead is the difference between a second and a minute.
 */
typedef void (*PG_ConfigVisit)(
    void* ctx,
    int id,
    const struct RSCache* profile,
    char* data,
    int size);

static int
pg_walk_config(
    struct PG_Cache* c,
    enum RSCache_Type type,
    int config_kind,
    PG_ConfigVisit visit,
    void* ctx)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&c->profile, type);
    int table;
    int* groups = NULL;
    int group_count = 0;

    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->dat2.disk, RSCACHE_DAT2_TABLE_CONFIGS);
        groups = malloc(sizeof(int));
        if( !groups )
            return 0;
        groups[group_count++] = config_kind;
    }
    else
    {
        int table_id = RSCache_Dat2DiskTableId(c->dat2.disk, addr.table);
        struct RSCache_ReferenceTable* rt =
            table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL : c->dat2.disk->tables[table_id];
        if( !rt || rt->id_count <= 0 )
            return 0;
        table = table_id;
        groups = malloc((size_t)rt->id_count * sizeof(int));
        if( !groups )
            return 0;
        for( int i = 0; i < rt->id_count; i++ )
            groups[group_count++] = rt->ids[i];
    }

    for( int g = 0; g < group_count; g++ )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(c->dat2.disk, table, groups[g]);
        struct RSCache_FileList* files;
        struct RSCache local;

        if( !archive )
            continue;
        if( !RSCache_Dat2DiskArchiveInitMetadata(c->dat2.disk, archive) ||
            archive->file_count <= 0 )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        /* The archive carries its own group revision, and several config decoders
         * branch on it. Taking the profile's instead misreads records in a cache
         * whose groups were built at different times. */
        local = c->profile;
        RSCache_ProfileSetGroupRevision(&local, type, archive->revision);

        files = RSCache_FileListNewFromDecode(
            archive->data, archive->data_size, archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        for( int i = 0; i < files->file_count; i++ )
        {
            int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
            int global = addr.group_shift == 0
                             ? file_id
                             : ((groups[g] << addr.group_shift) | (file_id & addr.file_mask));
            if( files->file_sizes[i] <= 0 )
                continue;
            visit(ctx, global, &local, files->files[i], files->file_sizes[i]);
        }
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
    }
    free(groups);
    return 1;
}

/* ---- npc ------------------------------------------------------------------ */

struct NpcCollector
{
    struct PG_NpcDef* items;
    int count;
    int capacity;
};

static void
npc_visit(void* ctx, int id, const struct RSCache* profile, char* data, int size)
{
    struct NpcCollector* col = ctx;
    struct RSCache_Dat2ConfigNpc* npc =
        RSCache_Dat2ConfigNpcNewDecodeProfile(profile, data, size);
    struct PG_NpcDef* out;

    if( !npc )
        return;
    /* Only entries with geometry can be posed, and a cache holds thousands of
     * model-less npcs (spawn markers, script hooks) that would otherwise fill the
     * list with rows that select to nothing. */
    if( npc->models_count <= 0 || !npc->models )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return;
    }

    if( col->count == col->capacity )
    {
        int next = col->capacity ? col->capacity * 2 : 512;
        struct PG_NpcDef* grown = realloc(col->items, (size_t)next * sizeof(*grown));
        if( !grown )
        {
            RSCache_Dat2ConfigNpcFree(npc);
            return;
        }
        col->items = grown;
        col->capacity = next;
    }

    out = &col->items[col->count++];
    memset(out, 0, sizeof(*out));
    out->id = id;
    out->name = dup_string(npc->name ? npc->name : "null");
    out->models = dup_ints(npc->models, npc->models_count);
    out->model_count = npc->models_count;
    out->size = npc->size > 0 ? npc->size : 1;
    out->recolor_count = npc->recolor_count;
    out->recolor_from = dup_shorts_as_u16(npc->recolor_to_find, npc->recolor_count);
    out->recolor_to = dup_shorts_as_u16(npc->recolor_to_replace, npc->recolor_count);

    RSCache_Dat2ConfigNpcFree(npc);
}

/* ---- obj ------------------------------------------------------------------ */

struct ItemCollector
{
    struct PG_ItemDef* items;
    int count;
    int capacity;
};

static void
item_visit(void* ctx, int id, const struct RSCache* profile, char* data, int size)
{
    struct ItemCollector* col = ctx;
    struct RSCache_Dat2ConfigObj* obj =
        RSCache_Dat2ConfigObjNewDecodeProfile(profile, data, size);
    struct PG_ItemDef* out;

    if( !obj )
        return;
    /* Wield models, not the inventory icon: the list exists to put an item in the
     * entity's hand, and an item with no worn model has nothing to contribute. */
    if( obj->male_model_0 <= 0 && obj->male_model_1 <= 0 && obj->male_model_2 <= 0 )
    {
        RSCache_Dat2ConfigObjFree(obj);
        return;
    }

    if( col->count == col->capacity )
    {
        int next = col->capacity ? col->capacity * 2 : 512;
        struct PG_ItemDef* grown = realloc(col->items, (size_t)next * sizeof(*grown));
        if( !grown )
        {
            RSCache_Dat2ConfigObjFree(obj);
            return;
        }
        col->items = grown;
        col->capacity = next;
    }

    out = &col->items[col->count++];
    memset(out, 0, sizeof(*out));
    out->id = id;
    out->name = dup_string(obj->name ? obj->name : "null");
    out->model0 = obj->male_model_0;
    out->model1 = obj->male_model_1;
    out->model2 = obj->male_model_2;
    out->recolor_count = obj->recolor_count;
    out->recolor_from = dup_ints_as_u16(obj->recolors_from, obj->recolor_count);
    out->recolor_to = dup_ints_as_u16(obj->recolors_to, obj->recolor_count);

    RSCache_Dat2ConfigObjFree(obj);
}

/* ---- idk ------------------------------------------------------------------ */

struct IdkCollector
{
    struct PG_IdkDef* items;
    int count;
    int capacity;
};

static void
idk_visit(void* ctx, int id, const struct RSCache* profile, char* data, int size)
{
    struct IdkCollector* col = ctx;
    struct RSCache_Dat2ConfigIdk* idk =
        RSCache_Dat2ConfigIdkNewDecode(data, size);
    struct PG_IdkDef* out;

    if( !idk )
        return;
    if( col->count == col->capacity )
    {
        int next = col->capacity ? col->capacity * 2 : 256;
        struct PG_IdkDef* grown = realloc(col->items, (size_t)next * sizeof(*grown));
        if( !grown )
        {
            RSCache_Dat2ConfigIdkFree(idk);
            return;
        }
        col->items = grown;
        col->capacity = next;
    }
    out = &col->items[col->count++];
    memset(out, 0, sizeof(*out));
    out->id = id;
    out->body_part_id = idk->body_part_id;
    out->models = dup_ints(idk->model_ids, idk->model_ids_count);
    out->model_count = idk->model_ids_count;
    RSCache_Dat2ConfigIdkFree(idk);
}

/* ---- sequence -------------------------------------------------------------- */

struct SeqCollector
{
    struct PG_SeqDef* items;
    int count;
    int capacity;
};

static void
seq_visit(void* ctx, int id, const struct RSCache* profile, char* data, int size)
{
    struct SeqCollector* col = ctx;
    struct RSCache_Dat2ConfigSequence* seq =
        RSCache_Dat2ConfigSequenceNewDecodeProfile(profile, data, size);
    struct PG_SeqDef* out;

    if( !seq )
        return;
    if( seq->frame_count <= 0 || !seq->frame_ids || !seq->frame_lengths )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return;
    }

    if( col->count == col->capacity )
    {
        int next = col->capacity ? col->capacity * 2 : 1024;
        struct PG_SeqDef* grown = realloc(col->items, (size_t)next * sizeof(*grown));
        if( !grown )
        {
            RSCache_Dat2ConfigSequenceFree(seq);
            return;
        }
        col->items = grown;
        col->capacity = next;
    }
    out = &col->items[col->count++];
    memset(out, 0, sizeof(*out));
    out->id = id;
    out->frame_count = seq->frame_count;
    out->frame_ids = dup_ints(seq->frame_ids, seq->frame_count);
    out->frame_lengths = dup_ints(seq->frame_lengths, seq->frame_count);
    out->loop_offset = seq->max_loops;
    out->left_hand_item = seq->left_hand_item;
    out->right_hand_item = seq->right_hand_item;
    out->debug_name = dup_string(seq->debug_name);
    RSCache_Dat2ConfigSequenceFree(seq);
}

/* ---- ordering ------------------------------------------------------------- */

static int
cmp_npc(const void* a, const void* b)
{
    return ((const struct PG_NpcDef*)a)->id - ((const struct PG_NpcDef*)b)->id;
}
static int
cmp_item(const void* a, const void* b)
{
    return ((const struct PG_ItemDef*)a)->id - ((const struct PG_ItemDef*)b)->id;
}
static int
cmp_idk(const void* a, const void* b)
{
    return ((const struct PG_IdkDef*)a)->id - ((const struct PG_IdkDef*)b)->id;
}
static int
cmp_seq(const void* a, const void* b)
{
    return ((const struct PG_SeqDef*)a)->id - ((const struct PG_SeqDef*)b)->id;
}

/* ---- the composed player -------------------------------------------------- */

/*
 * poser-gl's synthetic "Player" entry: the default appearance's identikits,
 * merged. Values in 0x100..0x1FF name an identikit; everything else in that
 * array is a colour slot the viewer has no use for.
 */
static const int PLAYER_APPEARANCE[] = {
    0, 0, 0, 0, 274, 0, 282, 292, 256, 289, 298, 266,
};

static const struct PG_IdkDef*
find_idk(const struct PG_Cache* c, int id)
{
    int lo = 0, hi = c->idk_count - 1;
    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        if( c->idks[mid].id == id )
            return &c->idks[mid];
        if( c->idks[mid].id < id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static void
add_player(struct PG_Cache* c)
{
    int models[64];
    int count = 0;
    struct PG_NpcDef* grown;
    struct PG_NpcDef* player;

    for( size_t i = 0; i < sizeof(PLAYER_APPEARANCE) / sizeof(PLAYER_APPEARANCE[0]); i++ )
    {
        int value = PLAYER_APPEARANCE[i];
        const struct PG_IdkDef* idk;
        if( value < 0x100 || value >= 0x200 )
            continue;
        idk = find_idk(c, value - 0x100);
        if( !idk )
            continue;
        for( int m = 0; m < idk->model_count && count < 64; m++ )
            models[count++] = idk->models[m];
    }

    /* A cache whose identikit numbering differs from OSRS's leaves that empty.
     * One kit per body part is the same body, just not the default outfit, and it
     * still covers every joint an animation can address. */
    if( count == 0 )
    {
        int* ids = NULL;
        int n = 0;
        if( tool_dat2_idk_models(&c->dat2, &ids, &n) )
        {
            for( int i = 0; i < n && count < 64; i++ )
                models[count++] = ids[i];
            free(ids);
        }
    }
    if( count == 0 )
        return;

    grown = realloc(c->npcs, (size_t)(c->npc_count + 1) * sizeof(*grown));
    if( !grown )
        return;
    c->npcs = grown;

    /* Ahead of every cache record, since its id is -1 and the list is id-ordered. */
    memmove(&c->npcs[1], &c->npcs[0], (size_t)c->npc_count * sizeof(*c->npcs));
    player = &c->npcs[0];
    memset(player, 0, sizeof(*player));
    player->id = PG_PLAYER_NPC_ID;
    player->name = dup_string("Player");
    player->models = dup_ints(models, count);
    player->model_count = count;
    player->size = 1;
    c->npc_count++;
}

/* ---- open / close --------------------------------------------------------- */

struct PG_Cache*
pg_cache_open(const char* directory, const char* rev)
{
    struct PG_Cache* c = calloc(1, sizeof(*c));
    struct NpcCollector npcs = { 0 };
    struct ItemCollector items = { 0 };
    struct IdkCollector idks = { 0 };
    struct SeqCollector seqs = { 0 };

    if( !c )
        return NULL;
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &c->profile) )
    {
        free(c);
        return NULL;
    }
    if( RSCache_IsDat1(&c->profile) )
    {
        fprintf(
            stderr,
            "poser-gl: revision '%s' is a dat1 cache; this editor reads dat2 only\n",
            rev);
        free(c);
        return NULL;
    }
    if( !tool_dat2_open(directory, &c->profile, &c->dat2) )
    {
        fprintf(stderr, "poser-gl: cannot open cache '%s'\n", directory);
        free(c);
        return NULL;
    }
    snprintf(c->path, sizeof(c->path), "%s", directory);
    snprintf(c->rev, sizeof(c->rev), "%s", rev);
    /* RS2-on-dat2 stores models at four times the vertex scale OSRS uses, which
     * the viewer compensates for rather than baking into the geometry. */
    c->higher_rev = RSCache_IsRs2Dat2(&c->profile);

    pg_walk_config(c, RSCACHE_TYPE_IDK, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT, idk_visit, &idks);
    c->idks = idks.items;
    c->idk_count = idks.count;
    qsort(c->idks, (size_t)c->idk_count, sizeof(*c->idks), cmp_idk);

    pg_walk_config(c, RSCACHE_TYPE_NPC, RSCACHE_DAT2_CONFIG_KIND_NPC, npc_visit, &npcs);
    c->npcs = npcs.items;
    c->npc_count = npcs.count;
    qsort(c->npcs, (size_t)c->npc_count, sizeof(*c->npcs), cmp_npc);
    add_player(c);

    pg_walk_config(c, RSCACHE_TYPE_OBJ, RSCACHE_DAT2_CONFIG_KIND_OBJECT, item_visit, &items);
    c->items = items.items;
    c->item_count = items.count;
    qsort(c->items, (size_t)c->item_count, sizeof(*c->items), cmp_item);

    pg_walk_config(
        c, RSCACHE_TYPE_SEQUENCE, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE, seq_visit, &seqs);
    c->seqs = seqs.items;
    c->seq_count = seqs.count;
    qsort(c->seqs, (size_t)c->seq_count, sizeof(*c->seqs), cmp_seq);

    if( c->npc_count == 0 || c->seq_count == 0 )
    {
        fprintf(
            stderr,
            "poser-gl: cache '%s' decoded %d npcs and %d sequences with revision '%s' — "
            "wrong revision?\n",
            directory,
            c->npc_count,
            c->seq_count,
            rev);
        pg_cache_close(c);
        return NULL;
    }

    fprintf(
        stderr,
        "poser-gl: %s (%s) — %d npcs, %d items, %d idks, %d sequences\n",
        directory,
        rev,
        c->npc_count,
        c->item_count,
        c->idk_count,
        c->seq_count);
    return c;
}

static void
framemap_free(struct PG_FrameMapDef* fm)
{
    if( !fm )
        return;
    if( fm->maps )
    {
        for( int i = 0; i < fm->length; i++ )
            free(fm->maps[i]);
        free(fm->maps);
    }
    free(fm->types);
    free(fm->map_lengths);
    free(fm);
}

void
pg_cache_close(struct PG_Cache* c)
{
    if( !c )
        return;
    for( int i = 0; i < c->npc_count; i++ )
    {
        free(c->npcs[i].name);
        free(c->npcs[i].models);
        free(c->npcs[i].recolor_from);
        free(c->npcs[i].recolor_to);
    }
    free(c->npcs);
    for( int i = 0; i < c->item_count; i++ )
    {
        free(c->items[i].name);
        free(c->items[i].recolor_from);
        free(c->items[i].recolor_to);
    }
    free(c->items);
    for( int i = 0; i < c->idk_count; i++ )
        free(c->idks[i].models);
    free(c->idks);
    for( int i = 0; i < c->seq_count; i++ )
    {
        free(c->seqs[i].frame_ids);
        free(c->seqs[i].frame_lengths);
        free(c->seqs[i].debug_name);
    }
    free(c->seqs);
    for( int i = 0; i < c->archive_count; i++ )
    {
        struct PG_FrameArchive* a = &c->archives[i];
        for( int f = 0; f < a->frame_count; f++ )
        {
            free(a->frames[f].indices);
            free(a->frames[f].delta_x);
            free(a->frames[f].delta_y);
            free(a->frames[f].delta_z);
        }
        free(a->frames);
        framemap_free(a->framemap);
    }
    free(c->archives);
    tool_dat2_close(&c->dat2);
    free(c);
}

const char*
pg_cache_path(const struct PG_Cache* c)
{
    return c ? c->path : "";
}

const char*
pg_cache_rev(const struct PG_Cache* c)
{
    return c ? c->rev : "";
}

bool
pg_cache_is_higher_rev(const struct PG_Cache* c)
{
    return c ? c->higher_rev : false;
}

/* ---- lookups -------------------------------------------------------------- */

int
pg_cache_npc_count(const struct PG_Cache* c)
{
    return c ? c->npc_count : 0;
}
const struct PG_NpcDef*
pg_cache_npc_at(const struct PG_Cache* c, int index)
{
    if( !c || index < 0 || index >= c->npc_count )
        return NULL;
    return &c->npcs[index];
}
const struct PG_NpcDef*
pg_cache_npc(const struct PG_Cache* c, int id)
{
    int lo = 0, hi = c ? c->npc_count - 1 : -1;
    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        if( c->npcs[mid].id == id )
            return &c->npcs[mid];
        if( c->npcs[mid].id < id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

int
pg_cache_item_count(const struct PG_Cache* c)
{
    return c ? c->item_count : 0;
}
const struct PG_ItemDef*
pg_cache_item_at(const struct PG_Cache* c, int index)
{
    if( !c || index < 0 || index >= c->item_count )
        return NULL;
    return &c->items[index];
}
const struct PG_ItemDef*
pg_cache_item(const struct PG_Cache* c, int id)
{
    int lo = 0, hi = c ? c->item_count - 1 : -1;
    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        if( c->items[mid].id == id )
            return &c->items[mid];
        if( c->items[mid].id < id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

int
pg_cache_seq_count(const struct PG_Cache* c)
{
    return c ? c->seq_count : 0;
}
const struct PG_SeqDef*
pg_cache_seq_at(const struct PG_Cache* c, int index)
{
    if( !c || index < 0 || index >= c->seq_count )
        return NULL;
    return &c->seqs[index];
}
const struct PG_SeqDef*
pg_cache_seq(const struct PG_Cache* c, int id)
{
    int lo = 0, hi = c ? c->seq_count - 1 : -1;
    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        if( c->seqs[mid].id == id )
            return &c->seqs[mid];
        if( c->seqs[mid].id < id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

int
pg_cache_max_seq_id(const struct PG_Cache* c)
{
    if( !c || c->seq_count == 0 )
        return -1;
    return c->seqs[c->seq_count - 1].id;
}

/* ---- models --------------------------------------------------------------- */

struct PG_ModelDef*
pg_cache_load_model(
    struct PG_Cache* c,
    int model_id,
    const uint16_t* recolor_from,
    const uint16_t* recolor_to,
    int recolor_count)
{
    struct RSCache_Model* raw;
    struct PG_ModelDef* def;

    if( !c )
        return NULL;
    raw = tool_dat2_model_load(&c->dat2, model_id);
    def = pg_model_from_rscache(raw, model_id);
    if( raw )
        RSCache_ModelFree(raw);
    if( !def )
        return NULL;

    if( recolor_from && recolor_to )
        for( int i = 0; i < recolor_count; i++ )
            pg_model_recolour(def, recolor_from[i], recolor_to[i]);
    return def;
}

/* ---- frame archives -------------------------------------------------------- */

static struct PG_FrameMapDef*
framemap_adapt(const struct RSCache_Dat2Framemap* src)
{
    struct PG_FrameMapDef* fm = calloc(1, sizeof(*fm));
    if( !fm || !src )
    {
        free(fm);
        return NULL;
    }
    fm->id = src->id;
    fm->length = src->length;
    fm->types = dup_ints(src->types, src->length);
    fm->map_lengths = dup_ints(src->bone_groups_lengths, src->length);
    fm->maps = calloc((size_t)(src->length > 0 ? src->length : 1), sizeof(int*));
    if( !fm->maps || (src->length > 0 && (!fm->types || !fm->map_lengths)) )
    {
        framemap_free(fm);
        return NULL;
    }
    for( int i = 0; i < src->length; i++ )
        fm->maps[i] = dup_ints(src->bone_groups[i], src->bone_groups_lengths[i]);
    return fm;
}

const struct PG_FrameArchive*
pg_cache_frame_archive(struct PG_Cache* c, int archive_id)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    struct RSCache_Dat2Framemap* raw_fm = NULL;
    struct PG_FrameArchive* out;
    int anim_table;
    int framemap_id;

    if( !c )
        return NULL;
    for( int i = 0; i < c->archive_count; i++ )
        if( c->archives[i].archive_id == archive_id )
            return c->archives[i].frame_count > 0 ? &c->archives[i] : NULL;

    if( c->archive_count == c->archive_capacity )
    {
        int next = c->archive_capacity ? c->archive_capacity * 2 : 32;
        struct PG_FrameArchive* grown = realloc(c->archives, (size_t)next * sizeof(*grown));
        if( !grown )
            return NULL;
        c->archives = grown;
        c->archive_capacity = next;
    }
    out = &c->archives[c->archive_count++];
    memset(out, 0, sizeof(*out));
    out->archive_id = archive_id;

    anim_table = RSCache_Dat2DiskTableId(c->dat2.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
    archive = RSCache_Dat2DiskArchiveNewLoad(c->dat2.disk, anim_table, archive_id);
    if( !archive )
        return NULL;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->dat2.disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    /* Every frame in an archive shares one framemap, and the id sits in the head
     * of each frame file — so the first non-empty file answers it for the lot. */
    framemap_id = -1;
    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        framemap_id = RSCache_Dat2FrameFramemapIdFromFileProfile(
            &c->profile, files->files[i], files->file_sizes[i]);
        break;
    }
    if( framemap_id >= 0 )
        raw_fm = tool_dat2_framemap_load(&c->dat2, framemap_id);
    if( !raw_fm )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    out->framemap = framemap_adapt(raw_fm);

    out->frames = calloc((size_t)files->file_count, sizeof(struct PG_FrameDef));
    if( out->frames )
    {
        for( int i = 0; i < files->file_count; i++ )
        {
            int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
            struct RSCache_Dat2Frame* fr;
            struct PG_FrameDef* dst;

            if( files->file_sizes[i] <= 0 )
                continue;
            fr = RSCache_Dat2FrameNewDecodeProfile(
                &c->profile, file_id, raw_fm, files->files[i], files->file_sizes[i]);
            if( !fr )
                continue;

            dst = &out->frames[out->frame_count++];
            dst->id = file_id;
            dst->framemap = out->framemap;
            dst->count = fr->translator_count;
            dst->indices = dup_ints(fr->index_frame_ids, fr->translator_count);
            dst->delta_x = dup_ints(fr->translator_arg_x, fr->translator_count);
            dst->delta_y = dup_ints(fr->translator_arg_y, fr->translator_count);
            dst->delta_z = dup_ints(fr->translator_arg_z, fr->translator_count);
            RSCache_Dat2FrameFree(fr);
        }
    }

    RSCache_Dat2FramemapFree(raw_fm);
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return out->frame_count > 0 ? out : NULL;
}

/* ---- packing --------------------------------------------------------------- */

/*
 * The flag byte for one bone.
 *
 * A component absent from the stream is filled in on decode with a type-derived
 * default — 128 for scale, 0 for everything else — so writing a component only
 * when it differs from that default is both minimal and lossless. Deriving the
 * mask from "non-zero" instead, as the 317 exporter does, would write a scale of
 * 0 as absent and have it read back as 128.
 */
static int
frame_flags_for(int type, int dx, int dy, int dz)
{
    int def = (type == 3 || type == 10) ? 128 : 0;
    int flags = 0;
    if( dx != def )
        flags |= 1;
    if( dy != def )
        flags |= 2;
    if( dz != def )
        flags |= 4;
    return flags;
}

/** Lowest animation archive id that the reference table does not already list. */
static int
free_anim_archive_id(struct PG_Cache* c)
{
    int table = RSCache_Dat2DiskTableId(c->dat2.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
    struct RSCache_ReferenceTable* rt =
        table == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL : c->dat2.disk->tables[table];
    int max = -1;
    if( !rt )
        return -1;
    for( int i = 0; i < rt->id_count; i++ )
        if( rt->ids[i] > max )
            max = rt->ids[i];
    return max + 1;
}

int
pg_cache_pack_animation(
    struct PG_Cache* c,
    const char* out_directory,
    const struct PG_FrameMapDef* framemap,
    const struct PG_PackFrame* frames,
    int frame_count,
    const struct PG_SeqDef* sequence,
    int* out_archive_id)
{
    struct RSCache_Dat2Edit* edit;
    struct RSCache_Dat2Framemap wire_fm;
    struct RSCache_Dat2ConfigSequence seq;
    struct RSCache_RecordAddress seq_addr;
    int anim_table;
    int seq_table;
    int seq_archive;
    int seq_file;
    int archive_id;
    int frame_codec;
    int rc = 0;
    uint8_t* buffer = NULL;

    if( !c || !framemap || !frames || !sequence || frame_count <= 0 )
        return 1;

    archive_id = free_anim_archive_id(c);
    if( archive_id < 0 )
    {
        fprintf(stderr, "poser-gl: cache has no animations table to pack into\n");
        return 1;
    }
    if( out_archive_id )
        *out_archive_id = archive_id;

    edit = RSCache_Dat2EditNew(c->dat2.disk);
    if( !edit )
        return 1;

    anim_table = RSCache_Dat2DiskTableId(c->dat2.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
    frame_codec = RSCache_Dat2FrameCodecVersion(&c->profile);

    /* The encoder needs the framemap only to un-shift V2 transforms, but it needs
     * the real bone types to do it, so hand it the same map the frames were
     * authored against rather than a stub. */
    memset(&wire_fm, 0, sizeof(wire_fm));
    wire_fm.id = framemap->id;
    wire_fm.length = framemap->length;
    wire_fm.types = framemap->types;
    wire_fm.bone_groups_lengths = framemap->map_lengths;
    wire_fm.bone_groups = framemap->maps;

    for( int f = 0; f < frame_count && rc == 0; f++ )
    {
        const struct PG_PackFrame* src = &frames[f];
        struct RSCache_Dat2Frame wire;
        uint8_t* flags = calloc((size_t)(framemap->length > 0 ? framemap->length : 1), 1);
        bool* synth = calloc((size_t)(src->count > 0 ? src->count : 1), sizeof(bool));
        uint32_t bound;
        uint32_t written;

        if( !flags || !synth )
        {
            free(flags);
            free(synth);
            rc = 1;
            break;
        }
        for( int i = 0; i < src->count; i++ )
        {
            int bone = src->indices[i];
            if( bone < 0 || bone >= framemap->length )
                continue;
            flags[bone] = (uint8_t)frame_flags_for(
                framemap->types[bone], src->delta_x[i], src->delta_y[i], src->delta_z[i]);
        }

        memset(&wire, 0, sizeof(wire));
        wire._id = f;
        wire.framemap_id = framemap->id;
        wire.translator_count = src->count;
        wire.index_frame_ids = src->indices;
        wire.translator_arg_x = src->delta_x;
        wire.translator_arg_y = src->delta_y;
        wire.translator_arg_z = src->delta_z;
        wire.flag_count = framemap->length;
        wire.bone_flags = flags;
        wire.synthesized = synth;

        bound = RSCache_Dat2FrameEncodeBoundCodec(&wire, frame_codec);
        buffer = malloc(bound ? bound : 1);
        if( !buffer )
        {
            free(flags);
            free(synth);
            rc = 1;
            break;
        }
        written = RSCache_Dat2FrameEncodeCodec(&wire, frame_codec, &wire_fm, buffer, bound);
        if( written == 0 )
        {
            fprintf(stderr, "poser-gl: frame %d failed to encode\n", f);
            rc = 1;
        }
        else if( !RSCache_Dat2EditPutFile(edit, anim_table, archive_id, f, buffer, written) )
        {
            fprintf(stderr, "poser-gl: frame %d failed to stage\n", f);
            rc = 1;
        }
        free(buffer);
        buffer = NULL;
        free(flags);
        free(synth);
    }

    if( rc == 0 )
    {
        uint32_t written;
        uint32_t capacity = (uint32_t)(64 + sequence->frame_count * 8);

        memset(&seq, 0, sizeof(seq));
        seq.id = sequence->id;
        seq.frame_count = sequence->frame_count;
        seq.frame_ids = sequence->frame_ids;
        seq.frame_lengths = sequence->frame_lengths;
        seq.max_loops = sequence->loop_offset;
        seq.left_hand_item = sequence->left_hand_item;
        seq.right_hand_item = sequence->right_hand_item;
        seq.forced_priority = 5;
        seq.priority = -1;
        seq.reply_mode = -1;
        seq.precedence_animating = -1;
        seq.anim_maya_id = -1;
        seq.frame_step = -1;

        buffer = malloc(capacity);
        written = buffer ? RSCache_Dat2ConfigSequenceEncode(&c->profile, &seq, buffer, capacity)
                         : 0;
        if( written == 0 )
        {
            fprintf(stderr, "poser-gl: sequence %d failed to encode\n", sequence->id);
            rc = 1;
        }
        else
        {
            seq_addr = RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_SEQUENCE);
            if( seq_addr.group_shift == 0 )
            {
                seq_table = RSCache_Dat2DiskTableId(c->dat2.disk, RSCACHE_DAT2_TABLE_CONFIGS);
                seq_archive = RSCACHE_DAT2_CONFIG_KIND_SEQUENCE;
                seq_file = sequence->id;
            }
            else
            {
                seq_table = RSCache_Dat2DiskTableId(c->dat2.disk, seq_addr.table);
                seq_archive = sequence->id >> seq_addr.group_shift;
                seq_file = sequence->id & seq_addr.file_mask;
            }
            if( !RSCache_Dat2EditPutFile(
                    edit, seq_table, seq_archive, seq_file, buffer, written) )
            {
                fprintf(stderr, "poser-gl: sequence %d failed to stage\n", sequence->id);
                rc = 1;
            }
        }
        free(buffer);
        buffer = NULL;
    }

    if( rc == 0 && !RSCache_Dat2EditCommit(edit, out_directory) )
    {
        fprintf(stderr, "poser-gl: commit to '%s' failed\n", out_directory);
        rc = 1;
    }
    RSCache_Dat2EditFree(edit);
    return rc;
}
