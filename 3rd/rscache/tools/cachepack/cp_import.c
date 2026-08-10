#include "cp_import.h"

#include "cachepack.h"
#include "cp_assets.h"
#include "cp_membership.h"

#include "asset_access.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_spotanim.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/model.h"
#include "filelist.h"
#include "port_plan.h"
#include "reference_table.h"
#include "tool_profile.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define CP_IMPORT_MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define CP_IMPORT_MKDIR(path) mkdir(path, 0755)
#endif

struct Import_Export { int source_id; char name[96]; };
struct Import_List { struct Import_Export* v; int n, cap; };
struct Import_Ints { int* v; int n, cap; };
struct Import_Manifest
{
    char from_rev[64], from_cache[1024], to_rev[64], to_tree[1024];
    char lane[256], ledger[512], prefix[96];
    int npc_base, obj_base, loc_base, spotanim_base;
    int model_base, seq_base, animset_base, framemap_base, synth_base;
    int legacy_scape2009;
    struct Import_List npcs, objs, seqs, spotanims, locs;
};

static char* trim(char* s)
{
    while( isspace((unsigned char)*s) ) s++;
    char* comment = strpbrk(s, ";#");
    if( comment ) *comment = '\0';
    size_t n = strlen(s);
    while( n && isspace((unsigned char)s[n - 1]) ) s[--n] = '\0';
    return s;
}

static int mkdir_p(const char* path)
{
    char tmp[1600];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for( char* p = tmp + 1; *p; p++ )
    {
        if( *p != '/' ) continue;
        *p = '\0';
        if( CP_IMPORT_MKDIR(tmp) != 0 && errno != EEXIST ) return 0;
        *p = '/';
    }
    return CP_IMPORT_MKDIR(tmp) == 0 || errno == EEXIST;
}

static void path_dir(char* path)
{
    char* slash = strrchr(path, '/');
    if( slash ) { *slash = '\0'; mkdir_p(path); *slash = '/'; }
}

static int write_bytes(const char* path, const void* data, size_t size)
{
    char work[1600];
    snprintf(work, sizeof(work), "%s", path);
    path_dir(work);
    FILE* f = fopen(path, "wb");
    if( !f ) { fprintf(stderr, "cachepack import: cannot write %s: %s\n", path, strerror(errno)); return 0; }
    int ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

static int list_add(struct Import_List* l, int id, const char* name)
{
    if( l->n == l->cap )
    {
        int cap = l->cap ? l->cap * 2 : 8;
        void* p = realloc(l->v, (size_t)cap * sizeof(*l->v));
        if( !p ) return 0;
        l->v = p; l->cap = cap;
    }
    l->v[l->n].source_id = id;
    snprintf(l->v[l->n].name, sizeof(l->v[l->n].name), "%s", name);
    l->n++;
    return 1;
}

static int list_find(const struct Import_List* l, int id)
{
    for( int i = 0; i < l->n; i++ )
        if( l->v[i].source_id == id ) return i;
    return -1;
}

static int list_add_unique(struct Import_List* l, int id, const char* name)
{
    return list_find(l, id) >= 0 ? 1 : list_add(l, id, name);
}

static int ints_add(struct Import_Ints* l, int id)
{
    if( id < 0 ) return 1;
    for( int i = 0; i < l->n; i++ ) if( l->v[i] == id ) return 1;
    if( l->n == l->cap )
    {
        int cap = l->cap ? l->cap * 2 : 16;
        int* p = realloc(l->v, (size_t)cap * sizeof(int));
        if( !p ) return 0;
        l->v = p; l->cap = cap;
    }
    l->v[l->n++] = id;
    return 1;
}

static void join_manifest_path(char* out, size_t cap, const char* manifest, const char* value)
{
    if( value[0] == '/' ) { snprintf(out, cap, "%s", value); return; }
    char base[1200]; snprintf(base, sizeof(base), "%s", manifest);
    char* slash = strrchr(base, '/');
    if( slash ) { *slash = '\0'; snprintf(out, cap, "%s/%s", base, value); }
    else snprintf(out, cap, "%s", value);
}

static int parse_nonnegative(const char* key, const char* value, int* out)
{
    char* end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if( errno || !end || *end || parsed < 0 || parsed > 0x7fffffffL )
    {
        fprintf(stderr, "cachepack import: %s must be a non-negative integer (got %s)\n",
                key, value);
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int manifest_load(const char* path, struct Import_Manifest* m)
{
    memset(m, 0, sizeof(*m));
    snprintf(m->lane, sizeof(m->lane), "ported/scape2009_summoning");
    snprintf(m->prefix, sizeof(m->prefix), "summoning");
    m->npc_base = 20000;
    m->obj_base = 40000;
    m->loc_base = 20000;
    m->spotanim_base = 20000;
    m->model_base = 100000;
    m->seq_base = 20000;
    m->animset_base = 20000;
    m->framemap_base = 8000;
    m->synth_base = 20000;
    FILE* f = fopen(path, "rb");
    if( !f ) { fprintf(stderr, "cachepack import: cannot open manifest %s\n", path); return 0; }
    char line[2048], section[64] = "";
    while( fgets(line, sizeof(line), f) )
    {
        char* s = trim(line);
        if( !*s ) continue;
        if( *s == '[' )
        {
            char* end = strchr(s, ']');
            if( !end ) { fclose(f); return 0; }
            *end = '\0'; snprintf(section, sizeof(section), "%s", s + 1); continue;
        }
        char* eq = strchr(s, '=');
        if( !eq ) { fclose(f); return 0; }
        *eq = '\0'; char* key = trim(s); char* value = trim(eq + 1);
        if( strcmp(section, "import") == 0 || strncmp(section, "import:", 7) == 0 )
        {
            if( strcmp(section, "import:scape2009") == 0 ) m->legacy_scape2009 = 1;
            if( strcmp(key, "from_rev") == 0 ) snprintf(m->from_rev, sizeof(m->from_rev), "%s", value);
            else if( strcmp(key, "from_cache") == 0 ) join_manifest_path(m->from_cache, sizeof(m->from_cache), path, value);
            else if( strcmp(key, "to_rev") == 0 ) snprintf(m->to_rev, sizeof(m->to_rev), "%s", value);
            else if( strcmp(key, "to_tree") == 0 ) join_manifest_path(m->to_tree, sizeof(m->to_tree), path, value);
            else if( strcmp(key, "lane") == 0 ) snprintf(m->lane, sizeof(m->lane), "%s", value);
            else if( strcmp(key, "ledger") == 0 ) snprintf(m->ledger, sizeof(m->ledger), "%s", value);
            else if( strcmp(key, "prefix") == 0 ) snprintf(m->prefix, sizeof(m->prefix), "%s", value);
            else if( strcmp(key, "npc_base") == 0 && !parse_nonnegative(key, value, &m->npc_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "obj_base") == 0 && !parse_nonnegative(key, value, &m->obj_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "loc_base") == 0 && !parse_nonnegative(key, value, &m->loc_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "spotanim_base") == 0 && !parse_nonnegative(key, value, &m->spotanim_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "model_base") == 0 && !parse_nonnegative(key, value, &m->model_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "seq_base") == 0 && !parse_nonnegative(key, value, &m->seq_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "animset_base") == 0 && !parse_nonnegative(key, value, &m->animset_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "framemap_base") == 0 && !parse_nonnegative(key, value, &m->framemap_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "synth_base") == 0 && !parse_nonnegative(key, value, &m->synth_base) ) { fclose(f); return 0; }
            else if( strcmp(key, "from_rev") != 0 && strcmp(key, "from_cache") != 0 &&
                     strcmp(key, "to_rev") != 0 && strcmp(key, "to_tree") != 0 &&
                     strcmp(key, "lane") != 0 && strcmp(key, "ledger") != 0 &&
                     strcmp(key, "prefix") != 0 )
            {
                fprintf(stderr, "cachepack import: unknown import key %s\n", key);
                fclose(f);
                return 0;
            }
        }
        else if( strcmp(section, "export:npc") == 0 || strcmp(section, "export:obj") == 0 ||
                 strcmp(section, "export:seq") == 0 || strcmp(section, "export:spotanim") == 0 ||
                 strcmp(section, "export:loc") == 0 )
        {
            char name[96];
            snprintf(name, sizeof(name), "%s", *value ? value : key);
            struct Import_List* list = strcmp(section, "export:npc") == 0 ? &m->npcs :
                                       strcmp(section, "export:obj") == 0 ? &m->objs :
                                       strcmp(section, "export:seq") == 0 ? &m->seqs :
                                       strcmp(section, "export:spotanim") == 0 ? &m->spotanims :
                                                                                &m->locs;
            int source_id = -1;
            if( !parse_nonnegative("export id", key, &source_id) ||
                !list_add_unique(list, source_id, name) ) { fclose(f); return 0; }
        }
    }
    fclose(f);
    if( !m->from_rev[0] || !m->from_cache[0] || !m->to_rev[0] || !m->to_tree[0] ||
        !m->prefix[0] || strchr(m->prefix, '/') ||
        (m->npcs.n == 0 && m->objs.n == 0 && m->seqs.n == 0 &&
         m->spotanims.n == 0 && m->locs.n == 0) )
    {
        fprintf(stderr, "cachepack import: manifest needs from_rev/from_cache/to_rev/to_tree and exports\n");
        return 0;
    }
    return 1;
}

static void manifest_free(struct Import_Manifest* m)
{
    free(m->npcs.v); free(m->objs.v); free(m->seqs.v);
    free(m->spotanims.v); free(m->locs.v);
}

static int map_allocate(struct Tool_IdMap* map, const struct Import_Ints* ids, int base)
{
    tool_id_map_init(map);
    for( int i = 0; i < ids->n; i++ )
        if( !tool_id_map_put(map, ids->v[i], base + i) ) return 0;
    return 1;
}

static int map_id(const struct Tool_IdMap* map, int source)
{
    int dest = source;
    tool_id_map_lookup(map, source, &dest);
    return dest;
}

static int record_load(struct Tool_Dat2Cache* cache, enum RSCache_Type type, int id,
                       uint8_t** out, int* out_size)
{
    *out = NULL; *out_size = 0;
    struct RSCache_RecordAddress a = RSCache_RecordAddressFor(&cache->profile, type);
    int table = RSCache_Dat2DiskTableId(cache->disk, a.table);
    int group = a.group_shift ? id >> a.group_shift : a.group;
    int file = a.group_shift ? id & a.file_mask : id;
    struct RSCache_Dat2DiskArchive* ar = RSCache_Dat2DiskArchiveNewLoad(cache->disk, table, group);
    if( !ar || !RSCache_Dat2DiskArchiveInitMetadata(cache->disk, ar) )
    { if( ar ) RSCache_Dat2DiskArchiveFree(ar); return 0; }
    struct RSCache_FileList* files = RSCache_FileListNewFromDecode(ar->data, ar->data_size, ar->file_count);
    int pos = tool_archive_file_position(ar, file);
    if( !files || pos < 0 )
    { if( files ) RSCache_FileListFree(files); RSCache_Dat2DiskArchiveFree(ar); return 0; }
    *out_size = files->file_sizes[pos];
    *out = malloc((size_t)*out_size);
    if( *out ) memcpy(*out, files->files[pos], (size_t)*out_size);
    RSCache_FileListFree(files); RSCache_Dat2DiskArchiveFree(ar);
    return *out != NULL;
}

static void canonical_name(const struct Import_Manifest* m, char* out, size_t cap, const char* raw)
{
    size_t n = strlen(m->prefix);
    if( strncmp(raw, m->prefix, n) == 0 && raw[n] == '_' ) snprintf(out, cap, "%s", raw);
    else snprintf(out, cap, "%s_%s", m->prefix, raw);
}

static void closure_name(const struct Import_Manifest* m, char* out, size_t cap,
                         const char* kind, int source_id)
{
    snprintf(out, cap, "%s_%s_%d", m->prefix, kind, source_id);
}

static void export_or_closure_name(const struct Import_Manifest* m,
                                   const struct Import_List* exports, int source_id,
                                   const char* kind, char* out, size_t cap)
{
    int at = list_find(exports, source_id);
    if( at >= 0 ) canonical_name(m, out, cap, exports->v[at].name);
    else closure_name(m, out, cap, kind, source_id);
}

static int emit_config(struct CP_Ctx* ctx, enum CP_TypeId type, int id,
                       const char* name, const uint8_t* bytes, int size, FILE* out)
{
    struct CP_Lines lines; cp_lines_init(&lines);
    const struct CP_Type* t = cp_type(type);
    int ok = t->unpack(ctx, id, bytes, size, &lines);
    if( ok ) cp_lines_write(&lines, name, out);
    cp_lines_free(&lines);
    return ok;
}

static int save_alloc(struct CP_Ctx* ctx, enum CP_TypeId type, const char* tree, const char* lane)
{
    char path[1400]; snprintf(path, sizeof(path), "%s/%s/pack/%s.alloc", tree, lane, cp_type(type)->name);
    char dir[1400]; snprintf(dir, sizeof(dir), "%s/%s/pack", tree, lane); mkdir_p(dir);
    return lc_pack_save(&ctx->names.alloc[type], path);
}

static int save_asset_pack(const struct LC_Pack* pack, const char* tree,
                           const char* lane, const char* packname)
{
    char dir[1400], path[1500];
    snprintf(dir, sizeof(dir), "%s/%s/pack", tree, lane); mkdir_p(dir);
    snprintf(path, sizeof(path), "%s/%s.pack", dir, packname);
    return lc_pack_save(pack, path);
}

static int save_client_membership(const struct Import_Manifest* m, const char* ns,
                                  const struct Import_List* exports,
                                  const struct Import_Ints* ids, const char* closure_kind)
{
    char path[1400];
    struct CP_Membership set;
    char overlay[1400]; snprintf(overlay, sizeof(overlay), "%s/%s", m->to_tree, m->lane);
    cp_membership_path(path, sizeof(path), overlay, ns, CP_MEMBERSHIP_CLIENT);
    if( !cp_membership_load(&set, path, ns, CP_MEMBERSHIP_CLIENT, 1) ) return 0;
    int ok = 1;
    if( exports )
    {
        for( int i = 0; ok && i < exports->n; i++ )
        {
            char name[128]; canonical_name(m, name, sizeof(name), exports->v[i].name);
            ok = cp_membership_add(&set, name) >= 0;
        }
    }
    else if( ids )
    {
        for( int i = 0; ok && i < ids->n; i++ )
        {
            char name[128];
            if( strcmp(ns, "seq") == 0 )
                export_or_closure_name(m, &m->seqs, ids->v[i], closure_kind, name, sizeof(name));
            else
                closure_name(m, name, sizeof(name), closure_kind, ids->v[i]);
            ok = cp_membership_add(&set, name) >= 0;
        }
    }
    if( ok ) ok = cp_membership_save(&set, path);
    cp_membership_free(&set);
    return ok;
}

/* Update one translation without regenerating the human-owned ledger.  Existing
 * comments, order, and unrelated decisions survive byte-for-byte. */
static int ledger_set(const char* path, const char* kind, int source_id,
                      const char* source_name, int dest_id, const char* dest_name)
{
    FILE* in = fopen(path, "rb");
    if( !in ) { fprintf(stderr, "cachepack import: cannot read ledger %s\n", path); return 0; }
    char tmp[1600]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* out = fopen(tmp, "wb");
    if( !out ) { fclose(in); return 0; }
    char line[4096]; int found = 0, ok = 1;
    while( fgets(line, sizeof(line), in) )
    {
        char row_kind[64]; int row_id = -1;
        if( sscanf(line, "%63[^\t]\t%d", row_kind, &row_id) == 2 &&
            strcmp(row_kind, kind) == 0 && row_id == source_id )
        {
            char old_source[256], old_dest[256], old_dest_name[256];
            char disposition[64], signoff[64];
            int old_dest_id = -1;
            int fields = sscanf(line,
                                "%*63[^\t]\t%*d\t%255[^\t]\t%255[^\t]\t%255[^\t]\t%63[^\t]\t%63[^\n]",
                                old_source, old_dest, old_dest_name, disposition, signoff);
            /* The importer may fill a pending allocation, but it must never
             * erase the human-owned disposition/signoff columns on a rerun. */
            if( fields != 5 )
            {
                fprintf(stderr, "cachepack import: malformed ledger row for %s %d\n",
                        kind, source_id);
                ok = 0;
            }
            else
            {
                char expected_id[32];
                snprintf(expected_id, sizeof(expected_id), "%d", dest_id);
                old_dest_id = strcmp(old_dest, "-") == 0 ? -1 : atoi(old_dest);
                if( old_dest_id >= 0 && old_dest_id != dest_id )
                {
                    fprintf(stderr,
                            "cachepack import: ledger allocation conflict for %s %d: %d != %d\n",
                            kind, source_id, old_dest_id, dest_id);
                    ok = 0;
                }
                fprintf(out, "%s\t%d\t%s\t%s\t%s\t%s\t%s\n",
                        kind, source_id, old_source,
                        old_dest_id < 0 ? expected_id : old_dest,
                        old_dest_id < 0 ? dest_name : old_dest_name,
                        strcmp(disposition, "pending") == 0 ? "minted" : disposition,
                        signoff);
            }
            found = 1;
        }
        else if( fputs(line, out) == EOF ) ok = 0;
    }
    if( !found )
        fprintf(out, "%s\t%d\t%s\t%d\t%s\tminted\tunreviewed\n",
                kind, source_id, source_name, dest_id, dest_name);
    if( ferror(in) || fclose(in) != 0 || fclose(out) != 0 ) ok = 0;
    if( ok && rename(tmp, path) != 0 ) ok = 0;
    if( !ok ) unlink(tmp);
    return ok;
}

static int save_ledger(const struct Import_Manifest* m,
                       const struct Import_Ints* models,
                       const struct Import_Ints* seqs,
                       const struct Import_Ints* frames,
                       const struct Import_Ints* framemaps,
                       const struct Import_Ints* synths,
                       const struct Tool_IdMap* model_map,
                       const struct Tool_IdMap* seq_map,
                       const struct Tool_IdMap* frame_map,
                       const struct Tool_IdMap* fm_map,
                       const struct Tool_IdMap* synth_map)
{
    if( !m->ledger[0] ) return 1;
    char path[1400];
    if( m->ledger[0] == '/' ) snprintf(path, sizeof(path), "%s", m->ledger);
    else snprintf(path, sizeof(path), "%s/%s", m->to_tree, m->ledger);
    int ok = 1;
    for( int i = 0; ok && i < m->npcs.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->npcs.v[i].name);
        ok = ledger_set(path, "npc", m->npcs.v[i].source_id, m->npcs.v[i].name,
                        m->npc_base + i, dest);
    }
    for( int i = 0; ok && i < m->objs.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->objs.v[i].name);
        ok = ledger_set(path, "obj", m->objs.v[i].source_id, m->objs.v[i].name,
                        m->obj_base + i, dest);
    }
    for( int i = 0; ok && i < m->locs.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->locs.v[i].name);
        ok = ledger_set(path, "loc", m->locs.v[i].source_id, m->locs.v[i].name,
                        m->loc_base + i, dest);
    }
    for( int i = 0; ok && i < m->spotanims.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->spotanims.v[i].name);
        ok = ledger_set(path, "spotanim", m->spotanims.v[i].source_id,
                        m->spotanims.v[i].name, m->spotanim_base + i, dest);
    }
#define LEDGER_CLOSURE(kind, list, map, stem) \
    for( int i = 0; ok && i < (list)->n; i++ ) { \
        char src[96], dst[320]; \
        snprintf(src, sizeof(src), stem "_%d", (list)->v[i]); \
        closure_name(m, dst, sizeof(dst), stem, (list)->v[i]); \
        ok = ledger_set(path, kind, (list)->v[i], src, map_id(map, (list)->v[i]), dst); \
    }
    LEDGER_CLOSURE("model", models, model_map, "model");
    for( int i = 0; ok && i < seqs->n; i++ )
    {
        char src[96], dst[320];
        snprintf(src, sizeof(src), "seq_%d", seqs->v[i]);
        export_or_closure_name(m, &m->seqs, seqs->v[i], "seq", dst, sizeof(dst));
        ok = ledger_set(path, "seq", seqs->v[i], src, map_id(seq_map, seqs->v[i]), dst);
    }
    LEDGER_CLOSURE("frame_archive", frames, frame_map, "animset");
    LEDGER_CLOSURE("framemap", framemaps, fm_map, "framemap");
    LEDGER_CLOSURE("synth", synths, synth_map, "synth");
#undef LEDGER_CLOSURE
    return ok;
}

static struct RSCache_Dat2ConfigSpotanim*
spotanim_load(struct Tool_Dat2Cache* src, int id)
{
    uint8_t* raw = NULL;
    int size = 0;
    if( !record_load(src, RSCACHE_TYPE_SPOTANIM, id, &raw, &size) ) return NULL;
    struct RSCache_Dat2ConfigSpotanim* spot =
        RSCache_Dat2ConfigSpotanimNewDecodeProfile(&src->profile, (char*)raw, size);
    free(raw);
    if( !spot || spot->_consumed != size )
    {
        fprintf(stderr, "cachepack import: spotanim %d did not decode exactly (%d/%d)\n",
                id, spot ? spot->_consumed : 0, size);
        RSCache_Dat2ConfigSpotanimFree(spot);
        return NULL;
    }
    return spot;
}

static struct RSCache_Dat2ConfigLoc*
loc_load(struct Tool_Dat2Cache* src, int id)
{
    uint8_t* raw = NULL;
    int size = 0;
    if( !record_load(src, RSCACHE_TYPE_LOC, id, &raw, &size) ) return NULL;
    struct RSCache_Dat2ConfigLoc* loc =
        RSCache_Dat2ConfigLocNewDecodeProfile(&src->profile, (char*)raw, size);
    free(raw);
    if( !loc || loc->_consumed != size )
    {
        fprintf(stderr, "cachepack import: loc %d did not decode exactly (%d/%d)\n",
                id, loc ? loc->_consumed : 0, size);
        RSCache_Dat2ConfigLocFree(loc);
        return NULL;
    }
    return loc;
}

static int map_required(const struct Tool_IdMap* map, const char* kind, int source, int* out)
{
    if( source < 0 ) { *out = source; return 1; }
    if( tool_id_map_lookup(map, source, out) ) return 1;
    fprintf(stderr, "cachepack import: %s dependency %d was not allocated\n", kind, source);
    return 0;
}

static int collect_sequence(struct Tool_Dat2Cache* src, int id,
                            struct Import_Ints* frames, struct Import_Ints* synths)
{
    struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(src, id);
    if( !seq || seq->_consumed == 0 )
    {
        fprintf(stderr, "cachepack import: cannot decode sequence %d\n", id);
        RSCache_Dat2ConfigSequenceFree(seq);
        return 0;
    }
    int ok = 1;
    for( int f = 0; ok && f < seq->frame_count; f++ )
        ok = ints_add(frames, (seq->frame_ids[f] >> 16) & 0xffff);
    for( int f = 0; ok && f < seq->chat_frame_id_count; f++ )
        if( seq->chat_frame_ids[f] >= 0 )
            ok = ints_add(frames, (seq->chat_frame_ids[f] >> 16) & 0xffff);
    for( int s = 0; ok && s < seq->frame_sounds.count; s++ )
        ok = ints_add(synths, seq->frame_sounds.sounds[s].id);
    RSCache_Dat2ConfigSequenceFree(seq);
    return ok;
}

static int write_model_asset(const struct Import_Manifest* m, struct Tool_Dat2Cache* src,
                             const struct Tool_IdMap* model_map, int source_id)
{
    struct Tool_Bytes raw = {0};
    int table = RSCache_Dat2DiskTableId(src->disk, RSCACHE_DAT2_TABLE_MODELS);
    if( !tool_dat2_archive_bytes(src, table, source_id, &raw) )
    {
        fprintf(stderr, "cachepack import: model %d is absent\n", source_id);
        return 0;
    }
    struct RSCache_ModelProvenance* prov = NULL;
    struct RSCache_Model* model = RSCache_ModelNewDecodeProvenance(raw.data, raw.size, &prov);
    if( !model || !prov )
    {
        fprintf(stderr, "cachepack import: model %d cannot be decoded with provenance\n", source_id);
        RSCache_ModelFree(model); RSCache_ModelProvenanceFree(prov); tool_bytes_free(&raw);
        return 0;
    }

    int format = prov->format;
    if( m->legacy_scape2009 )
    {
        /* Historical compatibility: the checked-in Summoning lane was authored
         * before texture dependency import existed and its tests require the
         * same untextured V2/V3 output. New manifests never take this branch. */
        free(model->face_textures); model->face_textures = NULL;
        free(model->face_texture_coords); model->face_texture_coords = NULL;
        free(model->texture_render_types); model->texture_render_types = NULL;
        free(model->textured_p_coordinate); model->textured_p_coordinate = NULL;
        free(model->textured_m_coordinate); model->textured_m_coordinate = NULL;
        free(model->textured_n_coordinate); model->textured_n_coordinate = NULL;
        model->textured_face_count = 0;
        format = prov->format == RSCACHE_MODEL_FORMAT_OB3 ? RSCACHE_MODEL_FORMAT_V3
                                                          : RSCACHE_MODEL_FORMAT_V2;
    }

    uint32_t bound = RSCache_ModelEncodeBound(model, prov);
    uint8_t* out = malloc(bound);
    uint32_t n = out ? RSCache_ModelEncodeFormat(model, prov, format, out, bound) : 0;
    if( !n )
        fprintf(stderr,
                "cachepack import: model %d cannot be safely encoded (format=%d, textures=%s)\n",
                source_id, format, model->face_textures ? "yes" : "no");

    const char* ext = m->legacy_scape2009 ? "model" : cp_asset_extension(CP_ASSET_MODEL, out, (int)n);
    char path[1500];
    snprintf(path, sizeof(path), "%s/models/%s/%s_model_%d.%s", m->to_tree, m->lane,
             m->prefix, source_id, ext);
    int ok = n && write_bytes(path, out, n);
    free(out); RSCache_ModelFree(model); RSCache_ModelProvenanceFree(prov); tool_bytes_free(&raw);
    (void)model_map;
    return ok;
}

static int write_framemap_asset(const struct Import_Manifest* m, struct Tool_Dat2Cache* src,
                                const struct RSCache* to, int source_id)
{
    struct RSCache_Dat2Framemap* fm = tool_dat2_framemap_load(src, source_id);
    if( !fm )
    {
        fprintf(stderr, "cachepack import: framemap %d is absent\n", source_id);
        return 0;
    }
    int codec = RSCache_Dat2FramemapCodecVersion(to);
    uint32_t bound = RSCache_Dat2FramemapEncodeBoundCodec(fm, codec);
    uint8_t* out = malloc(bound);
    uint32_t n = out ? RSCache_Dat2FramemapEncodeCodec(fm, codec, out, bound) : 0;
    char path[1500];
    snprintf(path, sizeof(path), "%s/framemaps/%s/%s_framemap_%d.base", m->to_tree,
             m->lane, m->prefix, source_id);
    int ok = n && write_bytes(path, out, n);
    if( !ok ) fprintf(stderr, "cachepack import: framemap %d transcode failed\n", source_id);
    free(out); RSCache_Dat2FramemapFree(fm);
    return ok;
}

static int write_frame_archive(const struct Import_Manifest* m, struct Tool_Dat2Cache* src,
                               const struct RSCache* to, const struct Tool_IdMap* fm_map,
                               int source_id)
{
    int table = RSCache_Dat2DiskTableId(src->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
    struct RSCache_Dat2DiskArchive* ar =
        RSCache_Dat2DiskArchiveNewLoad(src->disk, table, source_id);
    if( !ar || !RSCache_Dat2DiskArchiveInitMetadata(src->disk, ar) )
    {
        fprintf(stderr, "cachepack import: frame archive %d is absent\n", source_id);
        RSCache_Dat2DiskArchiveFree(ar);
        return 0;
    }
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(ar->data, ar->data_size, ar->file_count);
    if( !files ) { RSCache_Dat2DiskArchiveFree(ar); return 0; }

    int ok = 1;
    int dst_codec = RSCache_Dat2FrameCodecVersion(to);
    for( int f = 0; ok && f < files->file_count; f++ )
    {
        int file_id = ar->file_ids ? ar->file_ids[f] : f;
        int old_fm = RSCache_Dat2FrameFramemapIdFromFileProfile(
            &src->profile, files->files[f], files->file_sizes[f]);
        int new_fm = -1;
        if( old_fm < 0 || !map_required(fm_map, "framemap", old_fm, &new_fm) ) { ok = 0; break; }
        struct RSCache_Dat2Framemap* fm = tool_dat2_framemap_load(src, old_fm);
        struct RSCache_Dat2Frame* frame = fm ? RSCache_Dat2FrameNewDecodeProfile(
            &src->profile, file_id, fm, files->files[f], files->file_sizes[f]) : NULL;
        if( !fm || !frame )
        {
            fprintf(stderr, "cachepack import: frame %d:%d V2 decode failed\n", source_id, file_id);
            RSCache_Dat2FrameFree(frame); RSCache_Dat2FramemapFree(fm); ok = 0; break;
        }
        frame->framemap_id = new_fm;
        fm->id = new_fm;
        uint32_t bound = RSCache_Dat2FrameEncodeBoundCodec(frame, dst_codec);
        char* encoded = malloc(bound);
        uint32_t n = encoded ? RSCache_Dat2FrameEncodeCodec(
            frame, dst_codec, fm, (uint8_t*)encoded, bound) : 0;
        RSCache_Dat2FrameFree(frame); RSCache_Dat2FramemapFree(fm);
        if( !n )
        {
            fprintf(stderr, "cachepack import: frame %d:%d V2->V1 transcode failed\n",
                    source_id, file_id);
            free(encoded); ok = 0; break;
        }
        free(files->files[f]);
        files->files[f] = encoded;
        files->file_sizes[f] = (int)n;
    }

    uint32_t bound = ok ? RSCache_FileListEncodeBound(files) : 0;
    uint8_t* out = bound ? malloc(bound) : NULL;
    uint32_t n = out ? RSCache_FileListEncode(files, out, bound) : 0;
    char path[1500];
    snprintf(path, sizeof(path), "%s/animsets/%s/%s_animset_%d.anim", m->to_tree,
             m->lane, m->prefix, source_id);
    ok = ok && n && write_bytes(path, out, n);
    if( ok )
    {
        struct LC_Pack members = {0};
        snprintf(members.type, sizeof(members.type), "frame");
        for( int f = 0; f < ar->file_count; f++ )
        {
            int file_id = ar->file_ids ? ar->file_ids[f] : f;
            char name[64]; snprintf(name, sizeof(name), "frame_%d", file_id);
            lc_pack_set(&members, file_id, name);
        }
        char stem[1500];
        snprintf(stem, sizeof(stem), "%s/animsets/%s/%s_animset_%d", m->to_tree,
                 m->lane, m->prefix, source_id);
        ok = cp_member_pack_save(&members, stem, "memberpack");
        lc_pack_free(&members);
    }
    free(out); RSCache_FileListFree(files); RSCache_Dat2DiskArchiveFree(ar);
    return ok;
}

static int write_synth_asset(const struct Import_Manifest* m, struct Tool_Dat2Cache* src,
                             int source_id)
{
    int table = RSCache_Dat2DiskTableId(src->disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS);
    struct RSCache_Dat2DiskArchive* ar =
        RSCache_Dat2DiskArchiveNewLoad(src->disk, table, source_id);
    if( !ar || !RSCache_Dat2DiskArchiveInitMetadata(src->disk, ar) )
    {
        fprintf(stderr, "cachepack import: referenced synth %d is absent\n", source_id);
        RSCache_Dat2DiskArchiveFree(ar);
        return 0;
    }
    if( ar->file_count != 1 )
    {
        fprintf(stderr, "cachepack import: synth %d unexpectedly has %d members\n",
                source_id, ar->file_count);
        RSCache_Dat2DiskArchiveFree(ar);
        return 0;
    }
    char path[1500];
    snprintf(path, sizeof(path), "%s/synth/%s/%s_synth_%d.synth", m->to_tree,
             m->lane, m->prefix, source_id);
    int ok = write_bytes(path, ar->data, (size_t)ar->data_size);
    RSCache_Dat2DiskArchiveFree(ar);
    return ok;
}

static int import_run(const struct Import_Manifest* m, int apply)
{
    struct RSCache from, to;
    if( !tool_resolve_profile(m->from_rev, NULL, NULL, NULL, NULL, &from) ||
        !tool_resolve_profile(m->to_rev, NULL, NULL, NULL, NULL, &to) ) return 0;
    struct Tool_Dat2Cache src;
    if( !tool_dat2_open(m->from_cache, &from, &src) ) return 0;

    struct Import_Ints models = {0}, seqs = {0}, frames = {0}, framemaps = {0};
    struct Tool_NeutralNpc* neutral = calloc((size_t)m->npcs.n, sizeof(*neutral));
    struct RSCache_Dat2ConfigObj** objects = calloc((size_t)m->objs.n, sizeof(*objects));
    int ok = neutral != NULL && objects != NULL;

    for( int n = 0; ok && n < m->npcs.n; n++ )
    {
        int exact = 0;
        struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load_checked(&src, m->npcs.v[n].source_id, &exact);
        if( !npc || !exact || !tool_neutral_npc_from_dat2(&src, npc, m->npcs.v[n].source_id, &neutral[n]) ) ok = 0;
        if( npc ) RSCache_Dat2ConfigNpcFree(npc);
        for( int i = 0; ok && i < neutral[n].models_count; i++ ) ok = ints_add(&models, neutral[n].models[i]);
        for( int i = 0; ok && i < neutral[n].chathead_models_count; i++ ) ok = ints_add(&models, neutral[n].chathead_models[i]);
        for( int i = 0; ok && i < TOOL_ANIM_SLOT_COUNT; i++ )
            if( neutral[n].anim_present[i] ) ok = ints_add(&seqs, neutral[n].anim[i]);
    }

    for( int i = 0; ok && i < m->objs.n; i++ )
    {
        uint8_t* raw = NULL; int size = 0;
        if( !record_load(&src, RSCACHE_TYPE_OBJ, m->objs.v[i].source_id, &raw, &size) ) { ok = 0; break; }
        objects[i] = RSCache_Dat2ConfigObjNewDecodeProfile(&from, (char*)raw, size); free(raw);
        if( !objects[i] || objects[i]->_consumed != size ) { ok = 0; break; }
        int ids[] = { objects[i]->inventory_model_id, objects[i]->male_model_0, objects[i]->male_model_1,
                      objects[i]->male_model_2, objects[i]->female_model_0, objects[i]->female_model_1,
                      objects[i]->female_model_2, objects[i]->male_head_model, objects[i]->male_head_model_2,
                      objects[i]->female_head_model, objects[i]->female_head_model_2 };
        for( size_t k = 0; ok && k < sizeof(ids)/sizeof(ids[0]); k++ ) if( ids[k] > 0 ) ok = ints_add(&models, ids[k]);
    }

    for( int i = 0; ok && i < seqs.n; i++ )
    {
        struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(&src, seqs.v[i]);
        if( !seq || seq->_consumed == 0 ) { if(seq) RSCache_Dat2ConfigSequenceFree(seq); ok = 0; break; }
        for( int f = 0; f < seq->frame_count; f++ ) ok = ok && ints_add(&frames, (seq->frame_ids[f] >> 16) & 0xffff);
        RSCache_Dat2ConfigSequenceFree(seq);
    }

    for( int i = 0; ok && i < frames.n; i++ )
    {
        int table = RSCache_Dat2DiskTableId(src.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
        struct RSCache_Dat2DiskArchive* ar = RSCache_Dat2DiskArchiveNewLoad(src.disk, table, frames.v[i]);
        if( !ar || !RSCache_Dat2DiskArchiveInitMetadata(src.disk, ar) ) { if(ar)RSCache_Dat2DiskArchiveFree(ar); ok=0; break; }
        struct RSCache_FileList* files = RSCache_FileListNewFromDecode(ar->data, ar->data_size, ar->file_count);
        if( !files ) { RSCache_Dat2DiskArchiveFree(ar); ok=0; break; }
        for( int f = 0; f < files->file_count; f++ )
            if( files->file_sizes[f] >= 2 ) ok = ok && ints_add(&framemaps, RSCache_Dat2FramemapIdFromFrameArchive(files->files[f], files->file_sizes[f]));
        RSCache_FileListFree(files); RSCache_Dat2DiskArchiveFree(ar);
    }

    struct Tool_IdMap model_map = {0}, seq_map = {0}, frame_map = {0}, fm_map = {0};
    ok = ok && map_allocate(&model_map, &models, 100000) && map_allocate(&seq_map, &seqs, 20000) &&
         map_allocate(&frame_map, &frames, 20000) && map_allocate(&fm_map, &framemaps, 8000);

    struct CP_Ctx emit; memset(&emit, 0, sizeof(emit)); emit.profile = to; emit.warn_limit = 20;
    snprintf(emit.srcdir, sizeof(emit.srcdir), "%s", m->to_tree);
    if( ok && !cp_names_load(&emit.names, m->to_tree) ) ok = 0;

    struct LC_Pack model_pack = {0}, seq_asset_pack = {0}, fm_pack = {0};
    snprintf(model_pack.type, sizeof(model_pack.type), "7_models");
    snprintf(seq_asset_pack.type, sizeof(seq_asset_pack.type), "0_animations");
    snprintf(fm_pack.type, sizeof(fm_pack.type), "1_skeletons");

    for( int i = 0; ok && i < models.n; i++ )
    {
        char name[320]; snprintf(name, sizeof(name), "%s/summoning_model_%d", m->lane, models.v[i]);
        lc_pack_set(&model_pack, map_id(&model_map, models.v[i]), name);
    }
    for( int i = 0; ok && i < seqs.n; i++ )
    {
        char name[128]; snprintf(name, sizeof(name), "summoning_seq_%d", seqs.v[i]);
        lc_pack_set(&emit.names.alloc[CP_TYPE_SEQ], map_id(&seq_map, seqs.v[i]), name);
    }
    for( int i = 0; ok && i < frames.n; i++ )
    {
        char name[320]; snprintf(name, sizeof(name), "%s/summoning_animset_%d", m->lane, frames.v[i]);
        lc_pack_set(&seq_asset_pack, map_id(&frame_map, frames.v[i]), name);
    }
    for( int i = 0; ok && i < framemaps.n; i++ )
    {
        char name[320]; snprintf(name, sizeof(name), "%s/summoning_framemap_%d", m->lane, framemaps.v[i]);
        lc_pack_set(&fm_pack, map_id(&fm_map, framemaps.v[i]), name);
    }
    for( int i = 0; ok && i < m->npcs.n; i++ )
    {
        char name[128]; canonical_name(name, sizeof(name), m->npcs.v[i].name);
        lc_pack_set(&emit.names.alloc[CP_TYPE_NPC], 20000 + i, name);
    }
    for( int i = 0; ok && i < m->objs.n; i++ )
    {
        char name[128]; canonical_name(name, sizeof(name), m->objs.v[i].name);
        lc_pack_set(&emit.names.alloc[CP_TYPE_OBJ], 40000 + i, name);
    }

    printf("cachepack import (%s): npc=%d obj=%d model=%d seq=%d animset=%d framemap=%d\n",
           apply ? "apply" : "dry-run", m->npcs.n, m->objs.n, models.n, seqs.n, frames.n, framemaps.n);

    if( apply && ok )
    {
        for( int i = 0; ok && i < models.n; i++ )
        {
            struct Tool_Bytes raw = {0};
            int table = RSCache_Dat2DiskTableId(src.disk, RSCACHE_DAT2_TABLE_MODELS);
            if( !tool_dat2_archive_bytes(&src, table, models.v[i], &raw) ) { ok=0; break; }
            struct RSCache_ModelProvenance* prov = NULL;
            struct RSCache_Model* model = RSCache_ModelNewDecodeProvenance(raw.data, raw.size, &prov);
            if( !model || !prov ) { tool_bytes_free(&raw); ok=0; break; }
            free(model->face_textures); model->face_textures = NULL;
            free(model->face_texture_coords); model->face_texture_coords = NULL;
            free(model->texture_render_types); model->texture_render_types = NULL;
            free(model->textured_p_coordinate); model->textured_p_coordinate = NULL;
            free(model->textured_m_coordinate); model->textured_m_coordinate = NULL;
            free(model->textured_n_coordinate); model->textured_n_coordinate = NULL;
            model->textured_face_count = 0;
            int format = prov->format == RSCACHE_MODEL_FORMAT_OB3 ? RSCACHE_MODEL_FORMAT_V3 : RSCACHE_MODEL_FORMAT_V2;
            uint32_t bound = RSCache_ModelEncodeBound(model, prov); uint8_t* out = malloc(bound);
            uint32_t n = out ? RSCache_ModelEncodeFormat(model, prov, format, out, bound) : 0;
            char path[1500]; snprintf(path, sizeof(path), "%s/models/%s/summoning_model_%d.model", m->to_tree, m->lane, models.v[i]);
            ok = n && write_bytes(path, out, n);
            free(out); RSCache_ModelFree(model); RSCache_ModelProvenanceFree(prov); tool_bytes_free(&raw);
        }
        for( int i = 0; ok && i < framemaps.n; i++ )
        {
            struct RSCache_Dat2Framemap* fm = tool_dat2_framemap_load(&src, framemaps.v[i]);
            uint32_t bound = RSCache_Dat2FramemapEncodeBoundCodec(fm, RSCACHE_CODEC_FRAMEMAP_V1);
            uint8_t* out = malloc(bound); uint32_t n = out ? RSCache_Dat2FramemapEncodeCodec(fm, RSCACHE_CODEC_FRAMEMAP_V1, out, bound) : 0;
            char path[1500]; snprintf(path, sizeof(path), "%s/framemaps/%s/summoning_framemap_%d.base", m->to_tree, m->lane, framemaps.v[i]);
            ok = fm && n && write_bytes(path, out, n); free(out); RSCache_Dat2FramemapFree(fm);
        }
        for( int i = 0; ok && i < frames.n; i++ )
        {
            int table = RSCache_Dat2DiskTableId(src.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
            struct RSCache_Dat2DiskArchive* ar = RSCache_Dat2DiskArchiveNewLoad(src.disk, table, frames.v[i]);
            if( !ar || !RSCache_Dat2DiskArchiveInitMetadata(src.disk, ar) ) { if(ar)RSCache_Dat2DiskArchiveFree(ar); ok=0; break; }
            struct RSCache_FileList* files = RSCache_FileListNewFromDecode(ar->data, ar->data_size, ar->file_count);
            for( int f = 0; files && f < files->file_count; f++ ) if( files->file_sizes[f] >= 2 )
            { int old = RSCache_Dat2FramemapIdFromFrameArchive(files->files[f], files->file_sizes[f]); int nw = map_id(&fm_map, old); files->files[f][0]=(char)(nw>>8); files->files[f][1]=(char)nw; }
            uint32_t bound = files ? RSCache_FileListEncodeBound(files) : 0; uint8_t* out = malloc(bound);
            uint32_t n = out ? RSCache_FileListEncode(files, out, bound) : 0;
            char path[1500]; snprintf(path, sizeof(path), "%s/animsets/%s/summoning_animset_%d.anim", m->to_tree, m->lane, frames.v[i]);
            ok = files && n && write_bytes(path, out, n);
            if( ok )
            {
                struct LC_Pack members = {0};
                snprintf(members.type, sizeof(members.type), "frame");
                for( int f = 0; f < ar->file_count; f++ )
                {
                    int file_id = ar->file_ids ? ar->file_ids[f] : f;
                    char name[64]; snprintf(name, sizeof(name), "frame_%d", file_id);
                    lc_pack_set(&members, file_id, name);
                }
                char stem[1500]; snprintf(stem, sizeof(stem), "%s/animsets/%s/summoning_animset_%d", m->to_tree, m->lane, frames.v[i]);
                ok = cp_member_pack_save(&members, stem, "memberpack");
                lc_pack_free(&members);
            }
            free(out); if(files)RSCache_FileListFree(files); RSCache_Dat2DiskArchiveFree(ar);
        }

        char configdir[1500]; snprintf(configdir, sizeof(configdir), "%s/%s/configs", m->to_tree, m->lane); mkdir_p(configdir);
        char path[1550];
        snprintf(path, sizeof(path), "%s/summoning.seq", configdir); FILE* seqout = fopen(path, "wb");
        for( int i = 0; ok && seqout && i < seqs.n; i++ )
        {
            struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(&src, seqs.v[i]);
            for( int f = 0; seq && f < seq->frame_count; f++ ) seq->frame_ids[f] = (map_id(&frame_map, (seq->frame_ids[f]>>16)&0xffff)<<16) | (seq->frame_ids[f]&0xffff);
            free(seq->frame_sounds.frames); free(seq->frame_sounds.sounds); memset(&seq->frame_sounds,0,sizeof(seq->frame_sounds)); seq->rs2_530_sound_flag=false;
            uint32_t bound = RSCache_Dat2ConfigSequenceEncodeBound(seq); uint8_t* bytes = malloc(bound);
            uint32_t n = bytes ? RSCache_Dat2ConfigSequenceEncode(&to, seq, bytes, bound) : 0;
            char name[128]; snprintf(name,sizeof(name),"summoning_seq_%d",seqs.v[i]); ok = n && emit_config(&emit,CP_TYPE_SEQ,map_id(&seq_map,seqs.v[i]),name,bytes,(int)n,seqout);
            free(bytes); RSCache_Dat2ConfigSequenceFree(seq);
        }
        if(seqout)fclose(seqout); else ok=0;

        snprintf(path, sizeof(path), "%s/summoning.npc", configdir); FILE* npcout = fopen(path, "wb");
        for( int i=0; ok && npcout && i<m->npcs.n; i++ )
        {
            /* Texture ids are cache-local.  The Summoning lane's checked-in
             * policy is to drop them until a destination texture has received
             * explicit visual sign-off. */
            free(neutral[i].retexture_to_find);
            free(neutral[i].retexture_to_replace);
            neutral[i].retexture_to_find = NULL;
            neutral[i].retexture_to_replace = NULL;
            neutral[i].retexture_count = 0;
            for(int k=0;k<neutral[i].models_count;k++)neutral[i].models[k]=map_id(&model_map,neutral[i].models[k]);
            for(int k=0;k<neutral[i].chathead_models_count;k++)neutral[i].chathead_models[k]=map_id(&model_map,neutral[i].chathead_models[k]);
            for(int k=0;k<TOOL_ANIM_SLOT_COUNT;k++)if(neutral[i].anim_present[k])neutral[i].anim[k]=map_id(&seq_map,neutral[i].anim[k]);
            neutral[i].source_id=20000+i;
            struct RSCache_Dat2ConfigNpc* npc = tool_neutral_npc_to_dat2(&neutral[i],&to,0,-1,NULL,NULL);
            uint32_t bound=RSCache_Dat2ConfigNpcEncodeBound(npc); uint8_t* bytes=malloc(bound); uint32_t n=bytes?RSCache_Dat2ConfigNpcEncodeProfile(&to,npc,bytes,bound):0;
            char name[128]; canonical_name(name,sizeof(name),m->npcs.v[i].name); ok=n&&emit_config(&emit,CP_TYPE_NPC,20000+i,name,bytes,(int)n,npcout);
            free(bytes); RSCache_Dat2ConfigNpcFree(npc);
        }
        if(npcout)fclose(npcout); else ok=0;

        snprintf(path, sizeof(path), "%s/summoning.obj", configdir); FILE* objout = fopen(path, "wb");
        for( int i=0; ok && objout && i<m->objs.n; i++ )
        {
#define REMAP_OBJ_MODEL(field) if(objects[i]->field>0)objects[i]->field=map_id(&model_map,objects[i]->field)
            REMAP_OBJ_MODEL(inventory_model_id); REMAP_OBJ_MODEL(male_model_0); REMAP_OBJ_MODEL(male_model_1); REMAP_OBJ_MODEL(male_model_2);
            REMAP_OBJ_MODEL(female_model_0); REMAP_OBJ_MODEL(female_model_1); REMAP_OBJ_MODEL(female_model_2);
            REMAP_OBJ_MODEL(male_head_model); REMAP_OBJ_MODEL(male_head_model_2); REMAP_OBJ_MODEL(female_head_model); REMAP_OBJ_MODEL(female_head_model_2);
#undef REMAP_OBJ_MODEL
            uint32_t bound=RSCache_Dat2ConfigObjEncodeBound(objects[i]); uint8_t* bytes=malloc(bound); uint32_t n=bytes?RSCache_Dat2ConfigObjEncodeProfile(&to,objects[i],bytes,bound):0;
            char name[128]; canonical_name(name,sizeof(name),m->objs.v[i].name); ok=n&&emit_config(&emit,CP_TYPE_OBJ,40000+i,name,bytes,(int)n,objout); free(bytes);
        }
        if(objout)fclose(objout); else ok=0;

        ok = ok && save_alloc(&emit,CP_TYPE_NPC,m->to_tree,m->lane) && save_alloc(&emit,CP_TYPE_OBJ,m->to_tree,m->lane) && save_alloc(&emit,CP_TYPE_SEQ,m->to_tree,m->lane);
        ok = ok && save_asset_pack(&model_pack,m->to_tree,m->lane,"7_models") && save_asset_pack(&seq_asset_pack,m->to_tree,m->lane,"0_animations") && save_asset_pack(&fm_pack,m->to_tree,m->lane,"1_skeletons");
        ok = ok && save_client_membership(m->to_tree,m->lane,"npc",&m->npcs,NULL) && save_client_membership(m->to_tree,m->lane,"obj",&m->objs,NULL) && save_client_membership(m->to_tree,m->lane,"seq",NULL,&seqs);
        ok = ok && save_ledger(m,&models,&seqs,&frames,&framemaps,&model_map,&seq_map,&frame_map,&fm_map);
    }

    for(int i=0;i<m->npcs.n;i++)tool_neutral_npc_free(&neutral[i]);
    for(int i=0;i<m->objs.n;i++)if(objects[i])RSCache_Dat2ConfigObjFree(objects[i]);
    free(neutral); free(objects); free(models.v); free(seqs.v); free(frames.v); free(framemaps.v);
    tool_id_map_free(&model_map); tool_id_map_free(&seq_map); tool_id_map_free(&frame_map); tool_id_map_free(&fm_map);
    lc_pack_free(&model_pack); lc_pack_free(&seq_asset_pack); lc_pack_free(&fm_pack);
    cp_names_free(&emit.names); tool_dat2_close(&src);
    return ok;
}

int cp_import_command(int argc, char** argv)
{
    const char* manifest = NULL; const char* to_tree = NULL; int apply = 0;
    for( int i=0;i<argc;i++ )
    {
        if(strcmp(argv[i],"--manifest")==0 && i+1<argc)manifest=argv[++i];
        else if(strcmp(argv[i],"--to-tree")==0 && i+1<argc)to_tree=argv[++i];
        else if(strcmp(argv[i],"--apply")==0)apply=1;
        else { fprintf(stderr,"Usage: cachepack import --manifest FILE [--to-tree DIR] [--apply]\n"); return 1; }
    }
    if(!manifest){fprintf(stderr,"cachepack import: --manifest is required\n");return 1;}
    struct Import_Manifest m; if(!manifest_load(manifest,&m))return 1;
    if(to_tree)snprintf(m.to_tree,sizeof(m.to_tree),"%s",to_tree);
    int ok=import_run(&m,apply); manifest_free(&m);
    return ok?0:1;
}
