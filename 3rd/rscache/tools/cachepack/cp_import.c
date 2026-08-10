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
#include "datatypes/music_patch.h"
#include "datatypes/music_song.h"
#include "datatypes/sound_vorbis.h"
#include "filelist.h"
#include "port_plan.h"
#include "reference_table.h"
#include "tool_profile.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
    int sample_base, sample_identity_min, sample_setup_dest;
    int preserve_audio_ids;
    int legacy_scape2009;
    struct Import_List npcs, objs, models, seqs, spotanims, locs, synths;
    struct Import_List songs, patches, samples;
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
    m->sample_base = 20000;
    m->sample_identity_min = 0;
    m->sample_setup_dest = 20000;
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
            else if( strcmp(key, "npc_base") == 0 ) { if( !parse_nonnegative(key, value, &m->npc_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "obj_base") == 0 ) { if( !parse_nonnegative(key, value, &m->obj_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "loc_base") == 0 ) { if( !parse_nonnegative(key, value, &m->loc_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "spotanim_base") == 0 ) { if( !parse_nonnegative(key, value, &m->spotanim_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "model_base") == 0 ) { if( !parse_nonnegative(key, value, &m->model_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "seq_base") == 0 ) { if( !parse_nonnegative(key, value, &m->seq_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "animset_base") == 0 ) { if( !parse_nonnegative(key, value, &m->animset_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "framemap_base") == 0 ) { if( !parse_nonnegative(key, value, &m->framemap_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "synth_base") == 0 ) { if( !parse_nonnegative(key, value, &m->synth_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "sample_base") == 0 ) { if( !parse_nonnegative(key, value, &m->sample_base) ) { fclose(f); return 0; } }
            else if( strcmp(key, "sample_identity_min") == 0 ) { if( !parse_nonnegative(key, value, &m->sample_identity_min) ) { fclose(f); return 0; } }
            else if( strcmp(key, "sample_setup_dest") == 0 ) { if( !parse_nonnegative(key, value, &m->sample_setup_dest) ) { fclose(f); return 0; } }
            else if( strcmp(key, "preserve_audio_ids") == 0 )
            {
                if( strcmp(value, "yes") == 0 || strcmp(value, "true") == 0 || strcmp(value, "1") == 0 )
                    m->preserve_audio_ids = 1;
                else if( strcmp(value, "no") == 0 || strcmp(value, "false") == 0 || strcmp(value, "0") == 0 )
                    m->preserve_audio_ids = 0;
                else
                {
                    fprintf(stderr, "cachepack import: preserve_audio_ids must be yes/no\n");
                    fclose(f);
                    return 0;
                }
            }
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
                 strcmp(section, "export:model") == 0 ||
                 strcmp(section, "export:seq") == 0 || strcmp(section, "export:spotanim") == 0 ||
                 strcmp(section, "export:loc") == 0 || strcmp(section, "export:synth") == 0 ||
                 strcmp(section, "export:song") == 0 || strcmp(section, "export:patch") == 0 ||
                 strcmp(section, "export:sample") == 0 )
        {
            char name[96];
            snprintf(name, sizeof(name), "%s", *value ? value : key);
            struct Import_List* list = strcmp(section, "export:npc") == 0 ? &m->npcs :
                                       strcmp(section, "export:obj") == 0 ? &m->objs :
                                       strcmp(section, "export:model") == 0 ? &m->models :
                                       strcmp(section, "export:seq") == 0 ? &m->seqs :
                                       strcmp(section, "export:spotanim") == 0 ? &m->spotanims :
                                       strcmp(section, "export:loc") == 0 ? &m->locs :
                                       strcmp(section, "export:synth") == 0 ? &m->synths :
                                       strcmp(section, "export:song") == 0 ? &m->songs :
                                       strcmp(section, "export:patch") == 0 ? &m->patches :
                                                                              &m->samples;
            int source_id = -1;
            if( !parse_nonnegative("export id", key, &source_id) ||
                !list_add_unique(list, source_id, name) ) { fclose(f); return 0; }
        }
    }
    fclose(f);
    if( !m->from_rev[0] || !m->from_cache[0] || !m->to_rev[0] || !m->to_tree[0] ||
        !m->prefix[0] || strchr(m->prefix, '/') ||
        (m->npcs.n == 0 && m->objs.n == 0 && m->models.n == 0 && m->seqs.n == 0 &&
         m->spotanims.n == 0 && m->locs.n == 0 && m->synths.n == 0 &&
         m->songs.n == 0 && m->patches.n == 0 && m->samples.n == 0) )
    {
        fprintf(stderr, "cachepack import: manifest needs from_rev/from_cache/to_rev/to_tree and exports\n");
        return 0;
    }
    return 1;
}

static void manifest_free(struct Import_Manifest* m)
{
    free(m->npcs.v); free(m->objs.v); free(m->models.v); free(m->seqs.v);
    free(m->spotanims.v); free(m->locs.v); free(m->synths.v);
    free(m->songs.v); free(m->patches.v); free(m->samples.v);
}

static void manifest_ledger_path(
    const struct Import_Manifest* m,
    char* path,
    size_t path_size)
{
    if( !m->ledger[0] ) path[0] = '\0';
    else if( m->ledger[0] == '/' ) snprintf(path, path_size, "%s", m->ledger);
    else snprintf(path, path_size, "%s/%s", m->to_tree, m->ledger);
}

static int ints_contains(const struct Import_Ints* ids, int id)
{
    for( int i = 0; i < ids->n; i++ )
        if( ids->v[i] == id ) return 1;
    return 0;
}

/*
 * Existing ledger allocations are public IDs: dependency discovery order may
 * change when a manifest grows, but an already-published source ID must never
 * move. Reserve every destination recorded for this kind (including records
 * no longer in the current closure), reuse mappings for current records, then
 * fill the first genuinely free IDs at or above the configured base.
 */
static int map_allocate(
    struct Tool_IdMap* map,
    const struct Import_Ints* ids,
    int base,
    const struct Import_Manifest* m,
    const char* kind)
{
    struct Tool_IdMap prior, destinations;
    struct Import_Ints reserved = {0};
    char path[1400];
    int ok = 1;

    tool_id_map_init(map);
    tool_id_map_init(&prior);
    tool_id_map_init(&destinations);
    manifest_ledger_path(m, path, sizeof(path));
    if( path[0] )
    {
        FILE* ledger = fopen(path, "rb");
        if( ledger )
        {
            char line[4096];
            while( ok && fgets(line, sizeof(line), ledger) )
            {
                char row_kind[64], source_name[256], dest_text[64];
                int source_id = -1;
                int fields = sscanf(
                    line,
                    "%63[^\t]\t%d\t%255[^\t]\t%63[^\t]",
                    row_kind,
                    &source_id,
                    source_name,
                    dest_text);
                if( fields != 4 || strcmp(row_kind, kind) != 0 ||
                    strcmp(dest_text, "-") == 0 )
                    continue;

                char* end = NULL;
                errno = 0;
                long parsed = strtol(dest_text, &end, 10);
                if( errno || !end || *end || parsed < 0 || parsed > INT_MAX )
                {
                    fprintf(
                        stderr,
                        "cachepack import: malformed ledger destination for %s %d: %s\n",
                        kind,
                        source_id,
                        dest_text);
                    ok = 0;
                    break;
                }
                int dest_id = (int)parsed;
                int existing = -1;
                if( tool_id_map_lookup(&prior, source_id, &existing) )
                {
                    if( existing != dest_id )
                    {
                        fprintf(
                            stderr,
                            "cachepack import: duplicate ledger source for %s %d: %d and %d\n",
                            kind,
                            source_id,
                            existing,
                            dest_id);
                        ok = 0;
                    }
                    continue;
                }
                if( tool_id_map_lookup(&destinations, dest_id, &existing) )
                {
                    fprintf(
                        stderr,
                        "cachepack import: duplicate ledger destination for %s %d: sources %d and %d\n",
                        kind,
                        dest_id,
                        existing,
                        source_id);
                    ok = 0;
                    break;
                }
                ok = tool_id_map_put(&prior, source_id, dest_id) &&
                     tool_id_map_put(&destinations, dest_id, source_id) &&
                     ints_add(&reserved, dest_id);
            }
            if( ferror(ledger) ) ok = 0;
            fclose(ledger);
        }
        else if( errno != ENOENT )
        {
            fprintf(stderr, "cachepack import: cannot read ledger %s: %s\n", path, strerror(errno));
            ok = 0;
        }
    }

    for( int i = 0; ok && i < ids->n; i++ )
    {
        int dest_id = -1;
        if( tool_id_map_lookup(&prior, ids->v[i], &dest_id) )
            ok = tool_id_map_put(map, ids->v[i], dest_id);
    }
    int next = base;
    for( int i = 0; ok && i < ids->n; i++ )
    {
        int ignored = -1;
        if( tool_id_map_lookup(map, ids->v[i], &ignored) ) continue;
        while( next < INT_MAX && ints_contains(&reserved, next) ) next++;
        if( next == INT_MAX && ints_contains(&reserved, next) )
        {
            fprintf(stderr, "cachepack import: exhausted destination IDs for %s\n", kind);
            ok = 0;
            break;
        }
        ok = tool_id_map_put(map, ids->v[i], next) && ints_add(&reserved, next);
        if( next < INT_MAX ) next++;
    }

    tool_id_map_free(&prior);
    tool_id_map_free(&destinations);
    free(reserved.v);
    if( !ok ) tool_id_map_free(map);
    return ok;
}

static int map_id(const struct Tool_IdMap* map, int source)
{
    int dest = source;
    tool_id_map_lookup(map, source, &dest);
    return dest;
}

/* Raw rev-727 patches carry their sample archive ids internally, so the high
 * sample namespace must remain identity-mapped. Low sequence/loc samples may
 * collide with the destination's ordinary index-4 effects; allocate those in
 * a disjoint range so the runtime's index-4-first lookup remains unambiguous. */
static int map_samples_allocate(
    struct Tool_IdMap* map,
    const struct Import_Ints* samples,
    const struct Import_Manifest* m)
{
    struct Import_Ints remapped = {0};
    int ok = 1;
    for( int i = 0; ok && i < samples->n; i++ )
        if( samples->v[i] < m->sample_identity_min )
            ok = ints_add(&remapped, samples->v[i]);
    if( ok ) ok = map_allocate(map, &remapped, m->sample_base, m, "sample");
    for( int i = 0; ok && i < samples->n; i++ )
        if( samples->v[i] >= m->sample_identity_min )
            ok = tool_id_map_put(map, samples->v[i], samples->v[i]);
    free(remapped.v);
    if( !ok ) tool_id_map_free(map);
    return ok;
}

static int map_required(const struct Tool_IdMap* map, const char* kind, int source, int* out);

static int map_has_unique_destinations(const struct Tool_IdMap* map, const char* kind)
{
    for( int i = 0; i < map->count; i++ )
        for( int k = i + 1; k < map->count; k++ )
            if( map->entries[i].dest_id == map->entries[k].dest_id )
            {
                fprintf(
                    stderr,
                    "cachepack import: %s sources %d and %d both allocate destination %d\n",
                    kind,
                    map->entries[i].source_id,
                    map->entries[k].source_id,
                    map->entries[i].dest_id);
                return 0;
            }
    return 1;
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
                       const struct Import_Ints* songs,
                       const struct Import_Ints* samples,
                       const struct Import_Ints* patches,
                       const struct Tool_IdMap* npc_map,
                       const struct Tool_IdMap* obj_map,
                       const struct Tool_IdMap* loc_map,
                       const struct Tool_IdMap* spot_map,
                       const struct Tool_IdMap* model_map,
                       const struct Tool_IdMap* seq_map,
                       const struct Tool_IdMap* frame_map,
                       const struct Tool_IdMap* fm_map,
                       const struct Tool_IdMap* synth_map,
                       const struct Tool_IdMap* sample_map)
{
    if( !m->ledger[0] ) return 1;
    char path[1400];
    manifest_ledger_path(m, path, sizeof(path));
    int ok = 1;
    for( int i = 0; ok && i < m->npcs.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->npcs.v[i].name);
        ok = ledger_set(path, "npc", m->npcs.v[i].source_id, m->npcs.v[i].name,
                        map_id(npc_map, m->npcs.v[i].source_id), dest);
    }
    for( int i = 0; ok && i < m->objs.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->objs.v[i].name);
        ok = ledger_set(path, "obj", m->objs.v[i].source_id, m->objs.v[i].name,
                        map_id(obj_map, m->objs.v[i].source_id), dest);
    }
    for( int i = 0; ok && i < m->locs.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->locs.v[i].name);
        ok = ledger_set(path, "loc", m->locs.v[i].source_id, m->locs.v[i].name,
                        map_id(loc_map, m->locs.v[i].source_id), dest);
    }
    for( int i = 0; ok && i < m->spotanims.n; i++ )
    {
        char dest[128]; canonical_name(m, dest, sizeof(dest), m->spotanims.v[i].name);
        ok = ledger_set(path, "spotanim", m->spotanims.v[i].source_id,
                        m->spotanims.v[i].name,
                        map_id(spot_map, m->spotanims.v[i].source_id), dest);
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
    if( ok && (samples->n || songs->n) )
    {
        char dst[320];
        snprintf(dst, sizeof(dst), "%s_sample_setup_727", m->prefix);
        ok = ledger_set(path, "sample_setup", 0, "sample_setup_727",
                        m->sample_setup_dest, dst);
    }
    for( int i = 0; ok && i < songs->n; i++ )
    {
        char src[96], dst[320];
        snprintf(src, sizeof(src), "song_%d", songs->v[i]);
        snprintf(dst, sizeof(dst), "%s_song_%d", m->prefix, songs->v[i]);
        ok = ledger_set(path, "song", songs->v[i], src, songs->v[i], dst);
    }
    for( int i = 0; ok && i < samples->n; i++ )
    {
        char src[96], dst[320];
        snprintf(src, sizeof(src), "sample_%d", samples->v[i]);
        snprintf(dst, sizeof(dst), "%s_sample_%d", m->prefix, samples->v[i]);
        ok = ledger_set(path, "sample", samples->v[i], src,
                        map_id(sample_map, samples->v[i]), dst);
    }
    for( int i = 0; ok && i < patches->n; i++ )
    {
        char src[96], dst[320];
        snprintf(src, sizeof(src), "patch_%d", patches->v[i]);
        snprintf(dst, sizeof(dst), "%s_patch_%d", m->prefix, patches->v[i]);
        ok = ledger_set(path, "patch", patches->v[i], src, patches->v[i], dst);
    }
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

/* Pre-210 NPC opcode 102 stores only a prayer-headicon sprite index; its
 * archive is implicit. Modern OSRS stores an explicit archive/index pair.
 * Resolve that one well-known semantic dependency through the destination
 * sprite pack instead of dropping the icon or leaking a source-cache id. */
static int npc_copy_head_icons(
    const struct RSCache_Dat2ConfigNpc* source,
    struct RSCache_Dat2ConfigNpc* dest,
    const struct RSCache* to,
    int prayer_archive,
    int source_id)
{
    if( source->head_icon_count <= 0 ) return 1;
    if( !source->head_icon_archive_ids || !source->head_icon_sprite_index )
    {
        fprintf(stderr, "cachepack import: npc %d has malformed head icons\n", source_id);
        return 0;
    }
    dest->head_icon_archive_ids =
        malloc((size_t)source->head_icon_count * sizeof(*dest->head_icon_archive_ids));
    dest->head_icon_sprite_index =
        malloc((size_t)source->head_icon_count * sizeof(*dest->head_icon_sprite_index));
    if( !dest->head_icon_archive_ids || !dest->head_icon_sprite_index )
    {
        free(dest->head_icon_archive_ids);
        free(dest->head_icon_sprite_index);
        dest->head_icon_archive_ids = NULL;
        dest->head_icon_sprite_index = NULL;
        return 0;
    }
    dest->head_icon_count = source->head_icon_count;
    int modern =
        (RSCache_Dat2ConfigNpcFlags(to) & RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS) != 0;
    for( int i = 0; i < source->head_icon_count; i++ )
    {
        int archive = source->head_icon_archive_ids[i];
        if( archive < 0 && modern )
        {
            if( prayer_archive < 0 )
            {
                fprintf(
                    stderr,
                    "cachepack import: npc %d needs destination sprite headicons_prayer\n",
                    source_id);
                return 0;
            }
            archive = prayer_archive;
        }
        else if( archive >= 0 && modern )
        {
            fprintf(
                stderr,
                "cachepack import: npc %d references explicit source head-icon archive %d; "
                "sprite remapping is required\n",
                source_id,
                archive);
            return 0;
        }
        dest->head_icon_archive_ids[i] = archive;
        dest->head_icon_sprite_index[i] = source->head_icon_sprite_index[i];
    }
    return 1;
}

static int map_required(const struct Tool_IdMap* map, const char* kind, int source, int* out)
{
    if( source < 0 ) { *out = source; return 1; }
    if( tool_id_map_lookup(map, source, out) ) return 1;
    fprintf(stderr, "cachepack import: %s dependency %d was not allocated\n", kind, source);
    return 0;
}

static int archive_exists(struct Tool_Dat2Cache* src, enum RSCache_Dat2Table logical, int id)
{
    if( id < 0 ) return 0;
    int table = RSCache_Dat2DiskTableId(src->disk, logical);
    struct RSCache_Dat2DiskArchive* ar =
        table == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL :
        RSCache_Dat2DiskArchiveNewLoad(src->disk, table, id);
    int present = ar && RSCache_Dat2DiskArchiveInitMetadata(src->disk, ar);
    if( ar ) RSCache_Dat2DiskArchiveFree(ar);
    return present;
}

static int collect_audio_sound(struct Tool_Dat2Cache* src,
                               struct Import_Ints* synths,
                               struct Import_Ints* samples,
                               int id,
                               int force_vorbis)
{
    if( id < 0 ) return 1;
    if( force_vorbis )
    {
        if( archive_exists(src, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, id) )
            return ints_add(samples, id);
        fprintf(stderr,
                "cachepack import: Vorbis-selected sound %d is absent from source index 14\n",
                id);
        return 0;
    }
    if( archive_exists(src, RSCACHE_DAT2_TABLE_SOUND_EFFECTS, id) )
        return ints_add(synths, id);
    /* Loc opcodes 168/169 select the recorded-audio path but that type bit has
     * no OSRS239 LocType field. Existence is unambiguous in every imported
     * rev-727 record: index 4 wins when both tables happen to use an id, and an
     * index-14-only id is retained for the runtime fallback bridge. */
    if( archive_exists(src, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, id) )
        return ints_add(samples, id);
    fprintf(stderr, "cachepack import: referenced sound %d is absent from source indexes 4 and 14\n",
            id);
    return 0;
}

static int collect_sequence(struct Tool_Dat2Cache* src, int id,
                            struct Import_Ints* frames, struct Import_Ints* synths,
                            struct Import_Ints* samples)
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
    /* RS727 opcode 18 is AnimationDefinitions.aBool5928: it selects
     * MIDIInstrument/index 14 for every event. Preserve that lost type bit by
     * putting those ids into the sample closure; the destination runtime
     * resolves an index-4 miss through the copied index-14 lane. */
    for( int s = 0; ok && s < seq->frame_sounds.count; s++ )
        ok = collect_audio_sound(src, synths, samples, seq->frame_sounds.sounds[s].id,
                                 seq->rs2_727_vorbis_sounds);
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

static int collect_song_patches(struct Tool_Dat2Cache* src,
                                struct Import_Ints* patches,
                                int song_id)
{
    struct Tool_Bytes raw = {0};
    if( !tool_dat2_archive_bytes(src, RSCACHE_DAT2_TABLE_MUSIC_TRACKS, song_id, &raw) )
    {
        fprintf(stderr, "cachepack import: music track %d is absent\n", song_id);
        return 0;
    }
    struct RSCache_MusicSong* song =
        RSCache_MusicSongNewDecode((const char*)raw.data, raw.size);
    tool_bytes_free(&raw);
    if( !song )
    {
        fprintf(stderr, "cachepack import: music track %d did not decode\n", song_id);
        return 0;
    }
    int ok = 1;
    for( int i = 0; ok && i < song->patch_count; i++ )
        ok = ints_add(patches, song->patches[i].patch_id);
    RSCache_MusicSongFree(song);
    return ok;
}

static int collect_patch_samples(struct Tool_Dat2Cache* src,
                                 struct Import_Ints* synths,
                                 struct Import_Ints* samples,
                                 struct Import_Ints* pinned_synths,
                                 struct Import_Ints* pinned_samples,
                                 int patch_id)
{
    struct Tool_Bytes raw = {0};
    if( !tool_dat2_archive_bytes(src, RSCACHE_DAT2_TABLE_MUSIC_PATCHES, patch_id, &raw) )
    {
        fprintf(stderr, "cachepack import: music patch %d is absent\n", patch_id);
        return 0;
    }
    struct RSCache_MusicPatch* patch =
        RSCache_MusicPatchNewDecode((const char*)raw.data, raw.size);
    int exact = patch && patch->_consumed == raw.size;
    tool_bytes_free(&raw);
    if( !exact )
    {
        fprintf(stderr, "cachepack import: music patch %d did not decode exactly\n", patch_id);
        RSCache_MusicPatchFree(patch);
        return 0;
    }
    int ok = 1;
    for( int note = 0; ok && note < 128; note++ )
    {
        int id = RSCache_MusicPatchNoteSampleId(patch, note);
        if( id < 0 )
            continue;
        if( RSCache_MusicPatchNoteIsMusicSample(patch, note) )
            ok = collect_audio_sound(src, synths, samples, id, 1) &&
                 ints_add(pinned_samples, id);
        else
            ok = collect_audio_sound(src, synths, samples, id, 0) &&
                 ints_add(pinned_synths, id);
    }
    RSCache_MusicPatchFree(patch);
    return ok;
}

static int write_raw_audio_asset(const struct Import_Manifest* m,
                                 struct Tool_Dat2Cache* src,
                                 enum RSCache_Dat2Table table,
                                 int source_id,
                                 const char* directory,
                                 const char* stem,
                                 const char* extension)
{
    struct Tool_Bytes raw = {0};
    if( !tool_dat2_archive_bytes(src, table, source_id, &raw) )
    {
        fprintf(stderr, "cachepack import: %s %d is absent\n", stem, source_id);
        return 0;
    }
    char path[1500];
    snprintf(path, sizeof(path), "%s/%s/%s/%s_%s_%d.%s", m->to_tree, directory,
             m->lane, m->prefix, stem, source_id, extension);
    int ok = write_bytes(path, raw.data, (size_t)raw.size);
    tool_bytes_free(&raw);
    return ok;
}

static int write_sample_setup_asset(const struct Import_Manifest* m,
                                    struct Tool_Dat2Cache* src)
{
    struct Tool_Bytes raw = {0};
    if( !tool_dat2_archive_bytes(src, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, 0, &raw) )
    {
        fprintf(stderr, "cachepack import: source music-sample setup 0 is absent\n");
        return 0;
    }
    struct RSCache_VorbisSetup* setup =
        RSCache_VorbisSetupNewDecode((const char*)raw.data, raw.size);
    if( !setup )
    {
        fprintf(stderr, "cachepack import: source music-sample setup 0 did not decode\n");
        tool_bytes_free(&raw);
        return 0;
    }
    RSCache_VorbisSetupFree(setup);
    char path[1500];
    snprintf(path, sizeof(path), "%s/samples/%s/%s_sample_setup_727.sample",
             m->to_tree, m->lane, m->prefix);
    int ok = write_bytes(path, raw.data, (size_t)raw.size);
    tool_bytes_free(&raw);
    return ok;
}

static int verify_sample_assets(struct Tool_Dat2Cache* src, const struct Import_Ints* samples)
{
    struct Tool_Bytes setup_raw = {0};
    if( !tool_dat2_archive_bytes(src, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, 0, &setup_raw) )
        return 0;
    struct RSCache_VorbisSetup* setup =
        RSCache_VorbisSetupNewDecode((const char*)setup_raw.data, setup_raw.size);
    tool_bytes_free(&setup_raw);
    if( !setup )
        return 0;
    int ok = 1;
    for( int i = 0; ok && i < samples->n; i++ )
    {
        struct Tool_Bytes raw = {0};
        if( !tool_dat2_archive_bytes(
                src, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, samples->v[i], &raw) )
        {
            fprintf(stderr, "cachepack import: music sample %d is absent\n", samples->v[i]);
            ok = 0;
            break;
        }
        struct RSCache_AudioSample* decoded = RSCache_VorbisSampleNewDecode(
            setup, (const char*)raw.data, raw.size);
        tool_bytes_free(&raw);
        if( !decoded || decoded->sample_rate <= 0 || decoded->sample_count <= 0 )
        {
            fprintf(stderr, "cachepack import: music sample %d did not decode audibly\n",
                    samples->v[i]);
            ok = 0;
        }
        RSCache_AudioSampleFree(decoded);
    }
    RSCache_VorbisSetupFree(setup);
    return ok;
}

static int import_run(struct Import_Manifest* m, int apply)
{
    struct RSCache from, to;
    if( !tool_resolve_profile(m->from_rev, NULL, NULL, NULL, NULL, &from) ||
        !tool_resolve_profile(m->to_rev, NULL, NULL, NULL, NULL, &to) ) return 0;
    struct Tool_Dat2Cache src;
    if( !tool_dat2_open(m->from_cache, &from, &src) ) return 0;

    struct Import_Ints models = {0}, seqs = {0}, frames = {0}, framemaps = {0};
    struct Import_Ints synths = {0}, samples = {0}, songs = {0}, patches = {0};
    struct Import_Ints pinned_synths = {0}, pinned_samples = {0};
    struct Import_Ints npc_ids = {0}, obj_ids = {0}, loc_ids = {0};
    struct Import_Ints spot_ids = {0};
    for( int i = 0; i < m->models.n; i++ ) ints_add(&models, m->models.v[i].source_id);
    for( int i = 0; i < m->seqs.n; i++ ) ints_add(&seqs, m->seqs.v[i].source_id);
    for( int i = 0; i < m->synths.n; i++ ) ints_add(&synths, m->synths.v[i].source_id);
    for( int i = 0; i < m->samples.n; i++ ) ints_add(&samples, m->samples.v[i].source_id);
    for( int i = 0; i < m->songs.n; i++ ) ints_add(&songs, m->songs.v[i].source_id);
    for( int i = 0; i < m->patches.n; i++ ) ints_add(&patches, m->patches.v[i].source_id);
    for( int i = 0; i < m->npcs.n; i++ ) ints_add(&npc_ids, m->npcs.v[i].source_id);
    for( int i = 0; i < m->objs.n; i++ ) ints_add(&obj_ids, m->objs.v[i].source_id);
    for( int i = 0; i < m->spotanims.n; i++ ) ints_add(&spot_ids, m->spotanims.v[i].source_id);

    struct Tool_NeutralNpc* neutral = m->npcs.n ? calloc((size_t)m->npcs.n, sizeof(*neutral)) : NULL;
    struct RSCache_Dat2ConfigNpc** source_npcs =
        m->npcs.n ? calloc((size_t)m->npcs.n, sizeof(*source_npcs)) : NULL;
    struct RSCache_Dat2ConfigObj** objects =
        m->objs.n ? calloc((size_t)m->objs.n, sizeof(*objects)) : NULL;
    struct RSCache_Dat2ConfigSpotanim** spots =
        m->spotanims.n ? calloc((size_t)m->spotanims.n, sizeof(*spots)) : NULL;
    struct RSCache_Dat2ConfigLoc** locs = NULL;
    int ok = (!m->npcs.n || (neutral && source_npcs)) && (!m->objs.n || objects) &&
             (!m->spotanims.n || spots);

    for( int n = 0; ok && n < m->npcs.n; n++ )
    {
        int exact = 0;
        source_npcs[n] = tool_dat2_npc_load_checked(&src, m->npcs.v[n].source_id, &exact);
        if( !source_npcs[n] || !exact || !tool_neutral_npc_from_dat2(
                &src, source_npcs[n], m->npcs.v[n].source_id, &neutral[n]) )
        {
            fprintf(stderr, "cachepack import: npc %d did not decode or normalize exactly\n",
                    m->npcs.v[n].source_id);
            ok = 0;
        }
        for( int i = 0; ok && i < neutral[n].models_count; i++ )
            ok = ints_add(&models, neutral[n].models[i]);
        for( int i = 0; ok && i < neutral[n].chathead_models_count; i++ )
            ok = ints_add(&models, neutral[n].chathead_models[i]);
        for( int i = 0; ok && i < TOOL_ANIM_SLOT_COUNT; i++ )
            if( neutral[n].anim_present[i] ) ok = ints_add(&seqs, neutral[n].anim[i]);
        if( ok && !m->legacy_scape2009 )
        {
            int sounds[] = { source_npcs[n]->sound_idle, source_npcs[n]->sound_crawl,
                             source_npcs[n]->sound_walk, source_npcs[n]->sound_run };
            for( int i = 0; ok && i < 4; i++ )
                ok = collect_audio_sound(&src, &synths, &samples, sounds[i], 0);
        }
    }

    for( int i = 0; ok && i < m->objs.n; i++ )
    {
        uint8_t* raw = NULL; int size = 0;
        if( !record_load(&src, RSCACHE_TYPE_OBJ, m->objs.v[i].source_id, &raw, &size) )
        {
            fprintf(stderr, "cachepack import: cannot load obj %d\n", m->objs.v[i].source_id);
            ok = 0; break;
        }
        objects[i] = RSCache_Dat2ConfigObjNewDecodeProfile(&from, (char*)raw, size);
        free(raw);
        if( !objects[i] || objects[i]->_consumed != size )
        {
            fprintf(stderr, "cachepack import: obj %d did not decode exactly\n",
                    m->objs.v[i].source_id);
            ok = 0; break;
        }
        int ids[] = { objects[i]->inventory_model_id, objects[i]->male_model_0,
                      objects[i]->male_model_1, objects[i]->male_model_2,
                      objects[i]->female_model_0, objects[i]->female_model_1,
                      objects[i]->female_model_2, objects[i]->male_head_model,
                      objects[i]->male_head_model_2, objects[i]->female_head_model,
                      objects[i]->female_head_model_2 };
        for( size_t k = 0; ok && k < sizeof(ids) / sizeof(ids[0]); k++ )
            if( ids[k] > 0 ) ok = ints_add(&models, ids[k]);
    }

    for( int i = 0; ok && i < m->spotanims.n; i++ )
    {
        spots[i] = spotanim_load(&src, m->spotanims.v[i].source_id);
        if( !spots[i] ) { ok = 0; break; }
        ok = ints_add(&models, spots[i]->model);
        if( ok && spots[i]->anim >= 0 ) ok = ints_add(&seqs, spots[i]->anim);
    }

    /* Loc transforms are a real config dependency. Pull the full transitive
     * closure in and give implicit records deterministic source-id names. */
    for( int i = 0; ok && i < m->locs.n; i++ )
    {
        struct RSCache_Dat2ConfigLoc* loc = loc_load(&src, m->locs.v[i].source_id);
        if( !loc ) { ok = 0; break; }
        for( int s = 0; ok && s < loc->shapes_and_model_count; s++ )
            for( int k = 0; ok && k < loc->lengths[s]; k++ )
                ok = ints_add(&models, loc->models[s][k]);
        if( ok && loc->seq_id >= 0 ) ok = ints_add(&seqs, loc->seq_id);
        for( int k = 0; ok && k < loc->random_seq_id_count; k++ )
            ok = ints_add(&seqs, loc->random_seq_ids[k]);
        if( ok ) ok = collect_audio_sound(&src, &synths, &samples, loc->ambient_sound_id, 0);
        for( int k = 0; ok && k < loc->ambient_sound_id_count; k++ )
            ok = collect_audio_sound(&src, &synths, &samples, loc->ambient_sound_ids[k], 0);
        for( int k = 0; ok && k < loc->transform_count; k++ )
        {
            int dep = loc->transforms[k];
            if( dep < 0 || list_find(&m->locs, dep) >= 0 ) continue;
            char generated[96]; snprintf(generated, sizeof(generated), "loc_%d", dep);
            ok = list_add(&m->locs, dep, generated);
        }
        RSCache_Dat2ConfigLocFree(loc);
    }
    for( int i = 0; ok && i < m->locs.n; i++ ) ok = ints_add(&loc_ids, m->locs.v[i].source_id);
    if( ok && m->locs.n )
    {
        locs = calloc((size_t)m->locs.n, sizeof(*locs));
        if( !locs ) ok = 0;
    }
    for( int i = 0; ok && i < m->locs.n; i++ )
    {
        locs[i] = loc_load(&src, m->locs.v[i].source_id);
        if( !locs[i] ) ok = 0;
    }

    for( int i = 0; ok && i < seqs.n; i++ )
        ok = collect_sequence(&src, seqs.v[i], &frames, &synths, &samples);

    for( int i = 0; ok && i < songs.n; i++ )
        ok = collect_song_patches(&src, &patches, songs.v[i]);
    for( int i = 0; ok && i < patches.n; i++ )
        ok = collect_patch_samples(&src, &synths, &samples,
                                   &pinned_synths, &pinned_samples, patches.v[i]);
    if( ok && (songs.n || patches.n || samples.n) )
    {
        if( !m->preserve_audio_ids )
        {
            fprintf(stderr,
                    "cachepack import: audio bridge needs preserve_audio_ids=yes; "
                    "packed songs/patches carry ids internally\n");
            ok = 0;
        }
        else if( ints_contains(&samples, m->sample_setup_dest) )
        {
            fprintf(stderr,
                    "cachepack import: sample setup destination %d collides with a sample\n",
                    m->sample_setup_dest);
            ok = 0;
        }
        else
            ok = verify_sample_assets(&src, &samples);
    }

    for( int i = 0; ok && i < frames.n; i++ )
    {
        int table = RSCache_Dat2DiskTableId(src.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
        struct RSCache_Dat2DiskArchive* ar =
            RSCache_Dat2DiskArchiveNewLoad(src.disk, table, frames.v[i]);
        if( !ar || !RSCache_Dat2DiskArchiveInitMetadata(src.disk, ar) )
        { if( ar ) RSCache_Dat2DiskArchiveFree(ar); ok = 0; break; }
        struct RSCache_FileList* files =
            RSCache_FileListNewFromDecode(ar->data, ar->data_size, ar->file_count);
        if( !files ) { RSCache_Dat2DiskArchiveFree(ar); ok = 0; break; }
        for( int f = 0; ok && f < files->file_count; f++ )
        {
            int fm = RSCache_Dat2FrameFramemapIdFromFileProfile(
                &src.profile, files->files[f], files->file_sizes[f]);
            if( fm < 0 ) ok = 0;
            else ok = ints_add(&framemaps, fm);
        }
        RSCache_FileListFree(files); RSCache_Dat2DiskArchiveFree(ar);
    }

    struct Tool_IdMap npc_map = {0}, obj_map = {0}, loc_map = {0}, spot_map = {0};
    struct Tool_IdMap model_map = {0}, seq_map = {0}, frame_map = {0}, fm_map = {0};
    struct Tool_IdMap synth_map = {0}, sample_map = {0};
    ok = ok && map_allocate(&npc_map, &npc_ids, m->npc_base, m, "npc") &&
         map_allocate(&obj_map, &obj_ids, m->obj_base, m, "obj") &&
         map_allocate(&loc_map, &loc_ids, m->loc_base, m, "loc") &&
         map_allocate(&spot_map, &spot_ids, m->spotanim_base, m, "spotanim") &&
         map_allocate(&model_map, &models, m->model_base, m, "model") &&
         map_allocate(&seq_map, &seqs, m->seq_base, m, "seq") &&
         map_allocate(&frame_map, &frames, m->animset_base, m, "frame_archive") &&
         map_allocate(&fm_map, &framemaps, m->framemap_base, m, "framemap") &&
         map_allocate(&synth_map, &synths, m->synth_base, m, "synth") &&
         map_samples_allocate(&sample_map, &samples, m) &&
         map_has_unique_destinations(&npc_map, "npc") &&
         map_has_unique_destinations(&obj_map, "obj") &&
         map_has_unique_destinations(&loc_map, "loc") &&
         map_has_unique_destinations(&spot_map, "spotanim") &&
         map_has_unique_destinations(&model_map, "model") &&
         map_has_unique_destinations(&seq_map, "seq") &&
         map_has_unique_destinations(&frame_map, "frame_archive") &&
         map_has_unique_destinations(&fm_map, "framemap") &&
         map_has_unique_destinations(&synth_map, "synth") &&
         map_has_unique_destinations(&sample_map, "sample");
    for( int i = 0; ok && i < pinned_synths.n; i++ )
        if( map_id(&synth_map, pinned_synths.v[i]) != pinned_synths.v[i] )
        {
            fprintf(stderr,
                    "cachepack import: raw patch synth %d cannot be remapped without rewriting the patch\n",
                    pinned_synths.v[i]);
            ok = 0;
        }
    for( int i = 0; ok && i < pinned_samples.n; i++ )
        if( map_id(&sample_map, pinned_samples.v[i]) != pinned_samples.v[i] )
        {
            fprintf(stderr,
                    "cachepack import: raw patch sample %d cannot be remapped without rewriting the patch\n",
                    pinned_samples.v[i]);
            ok = 0;
        }
    if( ok )
        for( int i = 0; i < sample_map.count; i++ )
            if( sample_map.entries[i].dest_id == m->sample_setup_dest )
            {
                fprintf(stderr,
                        "cachepack import: sample setup destination %d collides with sample %d\n",
                        m->sample_setup_dest, sample_map.entries[i].source_id);
                ok = 0;
                break;
            }

    struct CP_Ctx emit; memset(&emit, 0, sizeof(emit)); emit.profile = to; emit.warn_limit = 20;
    snprintf(emit.srcdir, sizeof(emit.srcdir), "%s", m->to_tree);
    if( ok && !cp_names_load(&emit.names, m->to_tree) ) ok = 0;
    int prayer_headicon_archive = ok
        ? lc_pack_find(&emit.names.asset_packs[CP_ASSET_SPRITE], "headicons_prayer")
        : -1;

    struct LC_Pack model_pack = {0}, frame_pack = {0}, fm_pack = {0}, synth_pack = {0};
    struct LC_Pack song_pack = {0}, sample_pack = {0}, patch_pack = {0};
    snprintf(model_pack.type, sizeof(model_pack.type), "7_models");
    snprintf(frame_pack.type, sizeof(frame_pack.type), "0_animations");
    snprintf(fm_pack.type, sizeof(fm_pack.type), "1_skeletons");
    snprintf(synth_pack.type, sizeof(synth_pack.type), "4_soundeffects");
    snprintf(song_pack.type, sizeof(song_pack.type), "6_musictracks");
    snprintf(sample_pack.type, sizeof(sample_pack.type), "14_musicsamples");
    snprintf(patch_pack.type, sizeof(patch_pack.type), "15_musicpatches");
    for( int i = 0; ok && i < models.n; i++ )
    {
        char name[320]; snprintf(name, sizeof(name), "%s/%s_model_%d", m->lane, m->prefix, models.v[i]);
        ok = lc_pack_set(&model_pack, map_id(&model_map, models.v[i]), name);
    }
    for( int i = 0; ok && i < seqs.n; i++ )
    {
        char name[128]; export_or_closure_name(m, &m->seqs, seqs.v[i], "seq", name, sizeof(name));
        ok = lc_pack_set(&emit.names.alloc[CP_TYPE_SEQ], map_id(&seq_map, seqs.v[i]), name);
    }
    for( int i = 0; ok && i < frames.n; i++ )
    {
        char name[320]; snprintf(name, sizeof(name), "%s/%s_animset_%d", m->lane, m->prefix, frames.v[i]);
        ok = lc_pack_set(&frame_pack, map_id(&frame_map, frames.v[i]), name);
    }
    for( int i = 0; ok && i < framemaps.n; i++ )
    {
        char name[320]; snprintf(name, sizeof(name), "%s/%s_framemap_%d", m->lane, m->prefix, framemaps.v[i]);
        ok = lc_pack_set(&fm_pack, map_id(&fm_map, framemaps.v[i]), name);
    }
    for( int i = 0; ok && i < synths.n; i++ )
    {
        char name[320]; snprintf(name, sizeof(name), "%s/%s_synth_%d", m->lane, m->prefix, synths.v[i]);
        ok = lc_pack_set(&synth_pack, map_id(&synth_map, synths.v[i]), name);
    }
    if( ok && (samples.n || songs.n) )
    {
        char name[320];
        snprintf(name, sizeof(name), "%s/%s_sample_setup_727", m->lane, m->prefix);
        ok = lc_pack_set(&sample_pack, m->sample_setup_dest, name);
    }
    for( int i = 0; ok && i < samples.n; i++ )
    {
        char name[320];
        snprintf(name, sizeof(name), "%s/%s_sample_%d", m->lane, m->prefix, samples.v[i]);
        ok = lc_pack_set(&sample_pack, map_id(&sample_map, samples.v[i]), name);
    }
    for( int i = 0; ok && i < songs.n; i++ )
    {
        char name[320];
        snprintf(name, sizeof(name), "%s/%s_song_%d", m->lane, m->prefix, songs.v[i]);
        ok = lc_pack_set(&song_pack, songs.v[i], name);
    }
    for( int i = 0; ok && i < patches.n; i++ )
    {
        char name[320];
        snprintf(name, sizeof(name), "%s/%s_patch_%d", m->lane, m->prefix, patches.v[i]);
        ok = lc_pack_set(&patch_pack, patches.v[i], name);
    }
#define REGISTER_EXPORTS(field, cpkind, map) \
    for( int i = 0; ok && i < m->field.n; i++ ) { \
        char name[128]; canonical_name(m, name, sizeof(name), m->field.v[i].name); \
        ok = lc_pack_set(&emit.names.alloc[cpkind], \
                         map_id(map, m->field.v[i].source_id), name); \
    }
    REGISTER_EXPORTS(npcs, CP_TYPE_NPC, &npc_map);
    REGISTER_EXPORTS(objs, CP_TYPE_OBJ, &obj_map);
    REGISTER_EXPORTS(locs, CP_TYPE_LOC, &loc_map);
    REGISTER_EXPORTS(spotanims, CP_TYPE_SPOTANIM, &spot_map);
#undef REGISTER_EXPORTS

    printf("cachepack import (%s): npc=%d obj=%d loc=%d spotanim=%d model=%d seq=%d "
           "animset=%d framemap=%d synth=%d song=%d sample=%d patch=%d\n",
           apply ? "apply" : "dry-run",
           m->npcs.n, m->objs.n, m->locs.n, m->spotanims.n, models.n, seqs.n,
           frames.n, framemaps.n, synths.n, songs.n, samples.n, patches.n);

    if( apply && ok )
    {
        for( int i = 0; ok && i < models.n; i++ )
            ok = write_model_asset(m, &src, &model_map, models.v[i]);
        for( int i = 0; ok && i < framemaps.n; i++ )
            ok = write_framemap_asset(m, &src, &to, framemaps.v[i]);
        for( int i = 0; ok && i < frames.n; i++ )
            ok = write_frame_archive(m, &src, &to, &fm_map, frames.v[i]);
        for( int i = 0; ok && i < synths.n; i++ )
            ok = write_synth_asset(m, &src, synths.v[i]);
        if( ok && (samples.n || songs.n) )
            ok = write_sample_setup_asset(m, &src);
        for( int i = 0; ok && i < samples.n; i++ )
            ok = write_raw_audio_asset(m, &src, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES,
                                       samples.v[i], "samples", "sample", "sample");
        for( int i = 0; ok && i < songs.n; i++ )
            ok = write_raw_audio_asset(m, &src, RSCACHE_DAT2_TABLE_MUSIC_TRACKS,
                                       songs.v[i], "songs", "song", "jmid");
        for( int i = 0; ok && i < patches.n; i++ )
            ok = write_raw_audio_asset(m, &src, RSCACHE_DAT2_TABLE_MUSIC_PATCHES,
                                       patches.v[i], "patches", "patch", "patch");

        char configdir[1500], path[1550];
        snprintf(configdir, sizeof(configdir), "%s/%s/configs", m->to_tree, m->lane);
        mkdir_p(configdir);

        snprintf(path, sizeof(path), "%s/%s.seq", configdir, m->prefix);
        FILE* seqout = fopen(path, "wb");
        for( int i = 0; ok && seqout && i < seqs.n; i++ )
        {
            struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(&src, seqs.v[i]);
            if( !seq ) { ok = 0; break; }
            for( int f = 0; ok && f < seq->frame_count; f++ )
            {
                int old = (seq->frame_ids[f] >> 16) & 0xffff, nw = -1;
                ok = map_required(&frame_map, "frame archive", old, &nw);
                seq->frame_ids[f] = (nw << 16) | (seq->frame_ids[f] & 0xffff);
            }
            for( int f = 0; ok && f < seq->chat_frame_id_count; f++ ) if( seq->chat_frame_ids[f] >= 0 )
            {
                int old = (seq->chat_frame_ids[f] >> 16) & 0xffff, nw = -1;
                ok = map_required(&frame_map, "chat frame archive", old, &nw);
                seq->chat_frame_ids[f] = (nw << 16) | (seq->chat_frame_ids[f] & 0xffff);
            }
            for( int s = 0; ok && s < seq->frame_sounds.count; s++ )
            {
                int mapped_sound = -1;
                if( seq->rs2_727_vorbis_sounds )
                {
                    if( tool_id_map_lookup(&sample_map, seq->frame_sounds.sounds[s].id,
                                           &mapped_sound) )
                        seq->frame_sounds.sounds[s].id = mapped_sound;
                    else
                        ok = 0;
                }
                else if( tool_id_map_lookup(&synth_map, seq->frame_sounds.sounds[s].id,
                                            &mapped_sound) )
                    seq->frame_sounds.sounds[s].id = mapped_sound;
                else if( !tool_id_map_lookup(&sample_map, seq->frame_sounds.sounds[s].id,
                                             &mapped_sound) )
                {
                    fprintf(stderr,
                            "cachepack import: seq %d sound %d is in neither audio closure\n",
                            seqs.v[i], seq->frame_sounds.sounds[s].id);
                    ok = 0;
                }
                else
                    seq->frame_sounds.sounds[s].id = mapped_sound;
            }
            /* RS2 stores an empty hand in this unsigned field as 65535.  The
             * OSRS text codec expects the signed sentinel -1 instead. */
            if( seq->left_hand_item == 65535 ) seq->left_hand_item = -1;
            if( seq->right_hand_item == 65535 ) seq->right_hand_item = -1;
            int mapped = -1;
            if( seq->left_hand_item >= 0 && tool_id_map_lookup(&obj_map, seq->left_hand_item, &mapped) )
                seq->left_hand_item = mapped;
            if( seq->right_hand_item >= 0 && tool_id_map_lookup(&obj_map, seq->right_hand_item, &mapped) )
                seq->right_hand_item = mapped;
            uint32_t bound = RSCache_Dat2ConfigSequenceEncodeBound(seq);
            uint8_t* bytes = malloc(bound);
            uint32_t n = bytes ? RSCache_Dat2ConfigSequenceEncode(&to, seq, bytes, bound) : 0;
            char name[128]; export_or_closure_name(m, &m->seqs, seqs.v[i], "seq", name, sizeof(name));
            ok = ok && n && emit_config(&emit, CP_TYPE_SEQ, map_id(&seq_map, seqs.v[i]),
                                         name, bytes, (int)n, seqout);
            free(bytes); RSCache_Dat2ConfigSequenceFree(seq);
        }
        if( seqout ) fclose(seqout); else ok = 0;

        snprintf(path, sizeof(path), "%s/%s.npc", configdir, m->prefix);
        FILE* npcout = fopen(path, "wb");
        for( int i = 0; ok && npcout && i < m->npcs.n; i++ )
        {
            if( m->legacy_scape2009 )
            {
                free(neutral[i].retexture_to_find); free(neutral[i].retexture_to_replace);
                neutral[i].retexture_to_find = neutral[i].retexture_to_replace = NULL;
                neutral[i].retexture_count = 0;
            }
            for( int k = 0; k < neutral[i].models_count; k++ )
                neutral[i].models[k] = map_id(&model_map, neutral[i].models[k]);
            for( int k = 0; k < neutral[i].chathead_models_count; k++ )
                neutral[i].chathead_models[k] = map_id(&model_map, neutral[i].chathead_models[k]);
            for( int k = 0; k < TOOL_ANIM_SLOT_COUNT; k++ )
                if( neutral[i].anim_present[k] ) neutral[i].anim[k] = map_id(&seq_map, neutral[i].anim[k]);
            int dest_id = map_id(&npc_map, m->npcs.v[i].source_id);
            neutral[i].source_id = dest_id;
            struct RSCache_Dat2ConfigNpc* npc =
                tool_neutral_npc_to_dat2(&neutral[i], &to, 0, -1, NULL, NULL);
            if( npc )
                ok = npc_copy_head_icons(
                    source_npcs[i],
                    npc,
                    &to,
                    prayer_headicon_archive,
                    m->npcs.v[i].source_id);
            if( npc && !m->legacy_scape2009 )
            {
                int* dst[] = { &npc->sound_idle, &npc->sound_crawl, &npc->sound_walk, &npc->sound_run };
                int src_sound[] = { source_npcs[i]->sound_idle, source_npcs[i]->sound_crawl,
                                    source_npcs[i]->sound_walk, source_npcs[i]->sound_run };
                for( int s = 0; ok && s < 4; s++ )
                    if( src_sound[s] >= 0 )
                    {
                        int mapped = -1;
                        *dst[s] = tool_id_map_lookup(&synth_map, src_sound[s], &mapped)
                                      ? mapped : src_sound[s];
                    }
                npc->sound_radius = source_npcs[i]->sound_radius;
                npc->ambient_sound_volume = source_npcs[i]->ambient_sound_volume;
                npc->has_render_priority = source_npcs[i]->has_render_priority;
                npc->render_priority = source_npcs[i]->render_priority;
                memcpy(npc->stats, source_npcs[i]->stats, sizeof(npc->stats));
            }
            uint32_t bound = npc ? RSCache_Dat2ConfigNpcEncodeBound(npc) : 0;
            uint8_t* bytes = bound ? malloc(bound) : NULL;
            uint32_t n = bytes ? RSCache_Dat2ConfigNpcEncodeProfile(&to, npc, bytes, bound) : 0;
            char name[128]; canonical_name(m, name, sizeof(name), m->npcs.v[i].name);
            ok = ok && n && emit_config(&emit, CP_TYPE_NPC, dest_id,
                                         name, bytes, (int)n, npcout);
            free(bytes); RSCache_Dat2ConfigNpcFree(npc);
        }
        if( npcout ) fclose(npcout); else ok = 0;

        snprintf(path, sizeof(path), "%s/%s.obj", configdir, m->prefix);
        FILE* objout = fopen(path, "wb");
        for( int i = 0; ok && objout && i < m->objs.n; i++ )
        {
            int dest_id = map_id(&obj_map, m->objs.v[i].source_id);
#define REMAP_OBJ_MODEL(field) if( objects[i]->field > 0 ) objects[i]->field = map_id(&model_map, objects[i]->field)
            REMAP_OBJ_MODEL(inventory_model_id); REMAP_OBJ_MODEL(male_model_0);
            REMAP_OBJ_MODEL(male_model_1); REMAP_OBJ_MODEL(male_model_2);
            REMAP_OBJ_MODEL(female_model_0); REMAP_OBJ_MODEL(female_model_1);
            REMAP_OBJ_MODEL(female_model_2); REMAP_OBJ_MODEL(male_head_model);
            REMAP_OBJ_MODEL(male_head_model_2); REMAP_OBJ_MODEL(female_head_model);
            REMAP_OBJ_MODEL(female_head_model_2);
#undef REMAP_OBJ_MODEL
            int* refs[] = { &objects[i]->noted_id, &objects[i]->noted_template,
                            &objects[i]->bought_id, &objects[i]->bought_template_id,
                            &objects[i]->lend_id, &objects[i]->lend_template_id,
                            &objects[i]->placeholder_id, &objects[i]->placeholder_template_id };
            for( size_t r = 0; r < sizeof(refs) / sizeof(refs[0]); r++ )
            {
                int mapped = -1;
                if( **refs >= 0 && tool_id_map_lookup(&obj_map, **refs, &mapped) ) **refs = mapped;
            }
            for( int r = 0; r < 10; r++ )
            {
                int mapped = -1;
                if( objects[i]->count_obj[r] >= 0 &&
                    tool_id_map_lookup(&obj_map, objects[i]->count_obj[r], &mapped) )
                    objects[i]->count_obj[r] = mapped;
            }
            objects[i]->_id = dest_id;
            uint32_t bound = RSCache_Dat2ConfigObjEncodeBound(objects[i]);
            uint8_t* bytes = malloc(bound);
            uint32_t n = bytes ? RSCache_Dat2ConfigObjEncodeProfile(&to, objects[i], bytes, bound) : 0;
            char name[128]; canonical_name(m, name, sizeof(name), m->objs.v[i].name);
            ok = n && emit_config(&emit, CP_TYPE_OBJ, dest_id,
                                  name, bytes, (int)n, objout);
            free(bytes);
        }
        if( objout ) fclose(objout); else ok = 0;

        snprintf(path, sizeof(path), "%s/%s.spotanim", configdir, m->prefix);
        FILE* spotout = fopen(path, "wb");
        for( int i = 0; ok && spotout && i < m->spotanims.n; i++ )
        {
            int dest_id = map_id(&spot_map, m->spotanims.v[i].source_id);
            spots[i]->model = map_id(&model_map, spots[i]->model);
            if( spots[i]->anim >= 0 ) spots[i]->anim = map_id(&seq_map, spots[i]->anim);
            if( spots[i]->terrain_mode )
                fprintf(stderr, "cachepack import: spotanim %d terrain conformance has no OSRS config equivalent\n",
                        m->spotanims.v[i].source_id);
            uint32_t bound = RSCache_Dat2ConfigSpotanimEncodeBound(spots[i]);
            uint8_t* bytes = malloc(bound);
            uint32_t n = bytes ? RSCache_Dat2ConfigSpotanimEncodeRevision(
                to.revision, spots[i], bytes, bound) : 0;
            char name[128]; canonical_name(m, name, sizeof(name), m->spotanims.v[i].name);
            ok = n && emit_config(&emit, CP_TYPE_SPOTANIM, dest_id,
                                  name, bytes, (int)n, spotout);
            free(bytes);
        }
        if( spotout ) fclose(spotout); else ok = 0;

        snprintf(path, sizeof(path), "%s/%s.loc", configdir, m->prefix);
        FILE* locout = fopen(path, "wb");
        for( int i = 0; ok && locout && i < m->locs.n; i++ )
        {
            int dest_id = map_id(&loc_map, m->locs.v[i].source_id);
            for( int s = 0; s < locs[i]->shapes_and_model_count; s++ )
                for( int k = 0; k < locs[i]->lengths[s]; k++ )
                    locs[i]->models[s][k] = map_id(&model_map, locs[i]->models[s][k]);
            if( locs[i]->seq_id >= 0 ) locs[i]->seq_id = map_id(&seq_map, locs[i]->seq_id);
            for( int k = 0; k < locs[i]->random_seq_id_count; k++ )
                locs[i]->random_seq_ids[k] = map_id(&seq_map, locs[i]->random_seq_ids[k]);
            for( int k = 0; ok && k < locs[i]->transform_count; k++ ) if( locs[i]->transforms[k] >= 0 )
                ok = map_required(&loc_map, "loc transform", locs[i]->transforms[k], &locs[i]->transforms[k]);
            if( locs[i]->ambient_sound_id >= 0 )
            {
                int mapped = -1;
                if( tool_id_map_lookup(&synth_map, locs[i]->ambient_sound_id, &mapped) )
                    locs[i]->ambient_sound_id = mapped;
                else if( tool_id_map_lookup(&sample_map, locs[i]->ambient_sound_id, &mapped) )
                    locs[i]->ambient_sound_id = mapped;
                else
                    ok = 0;
            }
            for( int k = 0; ok && k < locs[i]->ambient_sound_id_count; k++ )
            {
                int mapped = -1;
                if( tool_id_map_lookup(&synth_map, locs[i]->ambient_sound_ids[k], &mapped) )
                    locs[i]->ambient_sound_ids[k] = mapped;
                else if( tool_id_map_lookup(&sample_map, locs[i]->ambient_sound_ids[k], &mapped) )
                    locs[i]->ambient_sound_ids[k] = mapped;
                else
                    ok = 0;
            }
            locs[i]->_id = dest_id;
            uint32_t bound = RSCache_Dat2ConfigLocEncodeBound(locs[i]);
            uint8_t* bytes = malloc(bound);
            uint32_t n = bytes ? RSCache_Dat2ConfigLocEncode(&to, locs[i], bytes, bound) : 0;
            char name[128]; canonical_name(m, name, sizeof(name), m->locs.v[i].name);
            ok = ok && n && emit_config(&emit, CP_TYPE_LOC, dest_id,
                                         name, bytes, (int)n, locout);
            free(bytes);
        }
        if( locout ) fclose(locout); else ok = 0;

#define SAVE_ALLOC_IF(field, kind) if( ok && m->field.n ) ok = save_alloc(&emit, kind, m->to_tree, m->lane)
        SAVE_ALLOC_IF(npcs, CP_TYPE_NPC); SAVE_ALLOC_IF(objs, CP_TYPE_OBJ);
        SAVE_ALLOC_IF(locs, CP_TYPE_LOC); SAVE_ALLOC_IF(spotanims, CP_TYPE_SPOTANIM);
#undef SAVE_ALLOC_IF
        if( ok && seqs.n ) ok = save_alloc(&emit, CP_TYPE_SEQ, m->to_tree, m->lane);
        if( ok && models.n ) ok = save_asset_pack(&model_pack, m->to_tree, m->lane, "7_models");
        if( ok && frames.n ) ok = save_asset_pack(&frame_pack, m->to_tree, m->lane, "0_animations");
        if( ok && framemaps.n ) ok = save_asset_pack(&fm_pack, m->to_tree, m->lane, "1_skeletons");
        if( ok && synths.n ) ok = save_asset_pack(&synth_pack, m->to_tree, m->lane, "4_soundeffects");
        if( ok && songs.n ) ok = save_asset_pack(&song_pack, m->to_tree, m->lane, "6_musictracks");
        if( ok && samples.n ) ok = save_asset_pack(&sample_pack, m->to_tree, m->lane, "14_musicsamples");
        if( ok && patches.n ) ok = save_asset_pack(&patch_pack, m->to_tree, m->lane, "15_musicpatches");
        if( ok && m->npcs.n ) ok = save_client_membership(m, "npc", &m->npcs, NULL, NULL);
        if( ok && m->objs.n ) ok = save_client_membership(m, "obj", &m->objs, NULL, NULL);
        if( ok && m->locs.n ) ok = save_client_membership(m, "loc", &m->locs, NULL, NULL);
        if( ok && m->spotanims.n ) ok = save_client_membership(m, "spotanim", &m->spotanims, NULL, NULL);
        if( ok && seqs.n ) ok = save_client_membership(m, "seq", NULL, &seqs, "seq");
        if( ok ) ok = save_ledger(m, &models, &seqs, &frames, &framemaps, &synths,
                                  &songs, &samples, &patches,
                                  &npc_map, &obj_map, &loc_map, &spot_map,
                                  &model_map, &seq_map, &frame_map, &fm_map,
                                  &synth_map, &sample_map);
    }

    for( int i = 0; i < m->npcs.n; i++ )
    {
        tool_neutral_npc_free(&neutral[i]);
        RSCache_Dat2ConfigNpcFree(source_npcs[i]);
    }
    for( int i = 0; i < m->objs.n; i++ ) RSCache_Dat2ConfigObjFree(objects[i]);
    for( int i = 0; i < m->spotanims.n; i++ ) RSCache_Dat2ConfigSpotanimFree(spots[i]);
    for( int i = 0; i < m->locs.n; i++ ) RSCache_Dat2ConfigLocFree(locs ? locs[i] : NULL);
    free(neutral); free(source_npcs); free(objects); free(spots); free(locs);
    free(models.v); free(seqs.v); free(frames.v); free(framemaps.v); free(synths.v);
    free(samples.v); free(songs.v); free(patches.v);
    free(pinned_synths.v); free(pinned_samples.v);
    free(npc_ids.v); free(obj_ids.v); free(loc_ids.v); free(spot_ids.v);
    tool_id_map_free(&npc_map); tool_id_map_free(&obj_map);
    tool_id_map_free(&loc_map); tool_id_map_free(&spot_map);
    tool_id_map_free(&model_map); tool_id_map_free(&seq_map); tool_id_map_free(&frame_map);
    tool_id_map_free(&fm_map); tool_id_map_free(&synth_map); tool_id_map_free(&sample_map);
    lc_pack_free(&model_pack); lc_pack_free(&frame_pack); lc_pack_free(&fm_pack);
    lc_pack_free(&synth_pack); lc_pack_free(&song_pack); lc_pack_free(&sample_pack);
    lc_pack_free(&patch_pack); cp_names_free(&emit.names); tool_dat2_close(&src);
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
