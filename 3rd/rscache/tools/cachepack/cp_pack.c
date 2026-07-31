#include "cachepack.h"

#include "checksum.h"
#include "cp_fields.h"
#include "cp_merge.h"
#include "cp_walk.h"

#include "archive.h"
#include "cache_edit.h"
#include "cache_write.h"
#include "dat2disk.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#define cp_mkdir(p) _mkdir(p)
#else
#include <unistd.h>
#define cp_mkdir(p) mkdir(p, 0755)
#endif

/*
 * The pack driver — LostCity's engine/tools/pack/config/PackShared.ts `packConfigs`.
 *
 * The reference reads every `.npc` / `.loc` / ... under the source tree, parses each
 * `[name]` block, packs it into a `.dat`/`.idx` pair, and finally writes the whole
 * config jagfile into the cache. Same shape here, with the container of the era:
 * one archive per type inside config table 2, one file per record id, written
 * through RSCache_Dat2Edit so the reference table's CRCs, sizes and child lists all
 * move with it.
 *
 * ## Why this writes into a copy of a cache rather than building one from nothing
 *
 * A cache is far more than its configs — models, sprites, maps, scripts, sounds and
 * a dozen other tables that this tool leaves alone. Emitting only the config table
 * would produce a directory the client cannot boot. So `pack` starts from a base
 * cache, exactly as the reference's `cache.write(0, 2, config)` writes one archive
 * into an existing cache file and leaves the rest untouched. Use `--base` to copy
 * first; without it the cache at `--out` is edited in place.
 *
 * ## Ordering
 *
 * Types are packed in register order, and the register puts the referenced types
 * first, because a `.loc` naming a sequence needs that sequence's pack line loaded.
 * Pack lines all load up front, so the order only matters for reporting.
 */

/** Names are the id authority: a `[name]` with no pack line has no id to write to. */
static int
resolve_id(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* debugname,
    int* out_id)
{
    int id = cp_name_find(ctx, type, debugname);
    if( id >= 0 )
    {
        *out_id = id;
        return 1;
    }
    fprintf(
        stderr,
        "cachepack: %s [%s] has no id — add a line to configs/all.%s.compack\n",
        cp_type(type)->name,
        debugname,
        cp_type(type)->name);
    return 0;
}

struct CP_PackStats
{
    int records;
    int bytes;
    int failed;
    /** The server pack's own counters — see the section below. */
    int server_records;
    int server_fields;
    int server_bytes;
};

/* ---- the server pack ----------------------------------------------------- */

/*
 * `server/pack/` — the half of a record the client cache cannot hold.
 *
 * A cache npc record says what the client needs and nothing about hitpoints,
 * respawn rate or hunt mode, because no client opcode exists for them. Until now
 * those reached the server by being re-parsed out of the `.npc` files under
 * `server/scripts` at every boot, which is the "two tools write a derived cache
 * and they do not compose" problem of `docs/CONTENT_ARCHITECTURE.md` §3.5.
 *
 * So `pack` now emits both halves from the *same* merged record: the client band
 * into the target cache, and the server band into a second dat2 under the content
 * tree. `src/net/mock/mock230_servercodec.c` reads that band back; the opcodes and
 * widths on both sides come from `fields/<type>.ini` and from nowhere else.
 *
 * ## Addressed as (config kind, record id)
 *
 * One archive per record, in the idx numbered by the type's config kind — the same
 * two coordinates the client cache uses, so a server record is found by the same
 * arithmetic as the client one it overlays. There is no reference table: nothing
 * needs to enumerate the pack, and `RSCache_Dat2DiskWriteArchive` writes none. The
 * per-archive header (`cp_fields.h`) carries the version and CRC that a reference
 * table would otherwise have supplied.
 *
 * ## Rebuilt whole, never appended to
 *
 * The container appends and orphans: an archive rewritten in place leaves its old
 * sectors behind, and a record *deleted* from the tree keeps its old archive
 * forever. Both are wrong for an output that is purely derived, so the dat2 and
 * its idx files are removed before the run. That is also why a `--types` run
 * produces a partial server pack, which is said out loud rather than left to be
 * discovered.
 */

/** mkdir -p, for the two components under srcdir. */
static int
ensure_dir(const char* path)
{
    struct stat st;

    if( stat(path, &st) == 0 )
        return S_ISDIR(st.st_mode) ? 0 : -1;
    return cp_mkdir(path);
}

static int
ensure_dir_p(const char* path)
{
    char buf[1200];
    size_t len;

    snprintf(buf, sizeof(buf), "%s", path);
    len = strlen(buf);
    for( size_t i = 1; i < len; i++ )
    {
        if( buf[i] != '/' )
            continue;
        buf[i] = '\0';
        if( ensure_dir(buf) != 0 )
            return -1;
        buf[i] = '/';
    }
    return ensure_dir(path);
}

/**
 * Clear the server pack so the run rebuilds it from nothing.
 *
 * Targeted rather than a recursive delete: the `.dat2` and the idx of every config
 * kind this tool knows, and nothing else. A tree may keep other things under
 * `server/`, and a packer that removes a directory it did not create is a packer
 * that eventually removes the wrong one.
 */
static void
server_pack_clear(const char* dir)
{
    char path[1200];
    int group_count = 0;
    const struct CP_ServerGroup* groups = cp_server_groups(&group_count);

    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", dir);
    remove(path);
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, cp_type(i)->config_kind);
        remove(path);
    }
    for( int i = 0; i < group_count; i++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, groups[i].group);
        remove(path);
    }
}

/** Per-register-field tallies, so an unresolved value is reported once per field
 *  rather than once per record — 800 door locs would otherwise print 800 lines. */
struct CP_FieldTally
{
    int written;
    int unresolved;
    int out_of_range;
    char sample[64];
};

/**
 * The text stating `name` on `rec`, or NULL.
 *
 * Two spellings, because the two layers state the same field differently. Rank 0
 * is the machine export and writes `param=<name>,<kind>,<value>`; rank 1 is
 * LostCity's grammar and writes `param=<name>,<value>`. A first-class
 * `<name>=<value>` line wins over either, since that is how the authored layer
 * spells the fields the cache has no param for at all (`hitpoints`, `respawnrate`).
 */
static const char*
merged_value(
    const struct CP_MergedRecord* rec,
    const char* name,
    char* scratch,
    size_t scratch_size)
{
    for( int i = 0; i < rec->count; i++ )
    {
        if( strcmp(rec->lines[i].key, name) == 0 )
            return rec->lines[i].value;
    }
    for( int i = 0; i < rec->count; i++ )
    {
        const char* value;
        const char* comma;
        const char* second;

        if( strcmp(rec->lines[i].key, "param") != 0 )
            continue;
        value = rec->lines[i].value;
        comma = strchr(value, ',');
        if( !comma || (size_t)(comma - value) != strlen(name) ||
            strncmp(value, name, strlen(name)) != 0 )
            continue;

        /* `,int,` / `,str,` / `,long,` is the machine export's type column. Only
         * those three, so an authored two-field row whose *value* happens to start
         * with a comma-free word is not mistaken for one. */
        second = strchr(comma + 1, ',');
        if( second )
        {
            size_t kind = (size_t)(second - comma - 1);

            if( (kind == 3 && strncmp(comma + 1, "int", 3) == 0) ||
                (kind == 3 && strncmp(comma + 1, "str", 3) == 0) ||
                (kind == 4 && strncmp(comma + 1, "long", 4) == 0) )
            {
                snprintf(scratch, scratch_size, "%s", second + 1);
                return scratch;
            }
        }
        snprintf(scratch, scratch_size, "%s", comma + 1);
        return scratch;
    }
    return NULL;
}

/**
 * A decimal literal, or a name in the namespace the register declares.
 *
 * `cp_resolve_ref` is deliberately not used: it warns per call site, and here the
 * interesting report is per field over the whole type. A field with no `ref` and a
 * symbolic value is not an error — `huntmode = aggressive` is an engine enum with
 * no cache namespace — it is a declared gap, and the tally is how it stays visible.
 */
static int
resolve_field_value(
    struct CP_Ctx* ctx,
    const struct CP_Field* field,
    const char* text,
    int* out)
{
    int ref_type;
    int id;

    if( cp_parse_int(text, out) )
        return 1;
    if( !field->ref[0] )
        return 0;
    ref_type = cp_type_by_name(field->ref);
    if( ref_type < 0 )
        return 0;
    id = cp_name_find(ctx, (enum CP_TypeId)ref_type, text);
    if( id < 0 )
        return 0;
    *out = id;
    return 1;
}

/**
 * Write one type's server bands.
 *
 * Runs off the *merged* record, not off `found[0]`: the fields here exist only in
 * the authored layer, which is the whole reason the merge was built.
 */
static int
pack_server_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    const struct CP_MergeSet* merged,
    const char* server_dir,
    struct CP_PackStats* stats)
{
    const struct CP_Type* type = cp_type(type_id);
    struct CP_Fields fields;
    struct CP_FieldTally tally[CP_FIELDS_MAX];
    int unnamed = 0;
    int bad_ref = 0;

    if( cp_fields_load(&fields, ctx->srcdir, type->name) <= 0 )
        return 1; /* nothing declared: nothing to write, and that is a valid tree */
    if( cp_fields_check(&fields) != 0 )
    {
        fprintf(stderr, "cachepack: fields/%s.ini is not a register the server codec can read\n",
                type->name);
        return 0;
    }

    /* `ref` names a cachepack config type, and that is checked here rather than in
     * cp_fields.c so that file stays free of the type register. A misspelling is
     * fatal: every symbolic value for that field would silently fall through to
     * "unresolved" and the pack would look merely incomplete. */
    for( int i = 0; i < fields.count; i++ )
    {
        if( fields.entries[i].ref[0] && cp_type_by_name(fields.entries[i].ref) < 0 )
        {
            fprintf(stderr, "cachepack: fields/%s.ini: [%s.%s] ref = `%s` is not a config type\n",
                    type->name, type->name, fields.entries[i].name, fields.entries[i].ref);
            bad_ref++;
        }
    }
    if( bad_ref )
        return 0;

    memset(tally, 0, sizeof(tally));
    if( ensure_dir_p(server_dir) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create %s\n", server_dir);
        return 0;
    }

    for( int r = 0; r < merged->count; r++ )
    {
        const struct CP_MergedRecord* rec = &merged->records[r];
        struct CP_ServerBand band;
        uint8_t archive[CP_SERVER_BAND_MAX + CP_SERVER_PACK_HEADER];
        uint8_t container[CP_SERVER_BAND_MAX + CP_SERVER_PACK_HEADER + 16];
        uint32_t payload;
        uint32_t framed;
        int id = 0;

        cp_server_band_init(&band);
        for( int f = 0; f < fields.band_count; f++ )
        {
            const struct CP_Field* field = &fields.entries[f];
            char scratch[512];
            const char* text = merged_value(rec, field->name, scratch, sizeof(scratch));
            int value = 0;

            /*
             * Presence is the criterion — never a comparison against a default,
             * and above all never against zero. `death_drop` defaults to -1 and obj
             * 0 is a real obj; the reader keeps "absent" and "present and zero"
             * apart precisely so the authored record can say either. The writer's
             * job is to preserve that distinction, and it has no defaults record to
             * derive it from anyway.
             */
            if( !text )
                continue;
            if( !resolve_field_value(ctx, field, text, &value) )
            {
                if( !tally[f].unresolved )
                    snprintf(tally[f].sample, sizeof(tally[f].sample), "%s", text);
                tally[f].unresolved++;
                continue;
            }
            if( !cp_server_band_put(&band, field, value) )
            {
                if( !tally[f].out_of_range )
                    snprintf(tally[f].sample, sizeof(tally[f].sample), "%d", value);
                tally[f].out_of_range++;
                continue;
            }
            tally[f].written++;
        }
        if( band.stated == 0 )
            continue; /* the record states nothing this register knows */

        if( !cp_server_band_finish(&band) )
            continue;

        id = cp_name_find(ctx, type_id, rec->debugname);
        if( id < 0 )
        {
            cp_warn(ctx, &ctx->warn_unresolved_name,
                    "%s [%s] states server fields but has no id — add a line to "
                    "configs/all.%s.compack",
                    type->name, rec->debugname, type->name);
            unnamed++;
            continue;
        }

        payload = cp_server_archive_build(&band, archive, sizeof(archive));
        if( !payload )
            continue;
        /*
         * Stored, not compressed. A band is tens of bytes; gzip's header alone is
         * larger than most of them, and the container's own length fields already
         * bound the read.
         */
        framed = RSCache_ArchiveEncode(container, sizeof(container), archive, payload,
                                       RSCACHE_ARCHIVE_COMPRESSION_NONE, NULL);
        if( !framed )
            continue;
        if( RSCache_Dat2DiskWriteArchive(server_dir, type->config_kind, id, container,
                                         (int)framed) != 0 )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to write its server band\n", type->name,
                    rec->debugname);
            stats->failed++;
            continue;
        }
        stats->server_records++;
        stats->server_bytes += (int)framed;
        stats->server_fields += band.stated;
    }

    if( stats->server_records )
        printf("  %-11s server pack: %d record(s), %d field(s), %d bytes in idx%d\n", type->name,
               stats->server_records, stats->server_fields, stats->server_bytes,
               type->config_kind);
    for( int f = 0; f < fields.count; f++ )
    {
        const struct CP_Field* field = &fields.entries[f];

        if( tally[f].unresolved )
            printf("  %-11s   %s: %d value(s) not written — `%s` is a name and the register "
                   "declares no `ref` namespace for it\n",
                   type->name, field->name, tally[f].unresolved, tally[f].sample);
        if( tally[f].out_of_range )
            printf("  %-11s   %s: %d value(s) not written — %s does not fit u%d\n", type->name,
                   field->name, tally[f].out_of_range, tally[f].sample, (int)field->wire);
    }
    if( unnamed )
        printf("  %-11s   %d record(s) with server fields have no id\n", type->name, unnamed);
    return 1;
}

/* ---- the client view of a merged record ---------------------------------- */

/*
 * The other half of the split: what `cp_npc.c` is allowed to see.
 *
 * A merged record holds both bands at once. Handing it straight to the client
 * encoder would offer it `hitpoints` and `huntmode`, which it would report as
 * unknown keys and drop; handing it only rank 0 would lose the authored records
 * entirely. The register is what separates them, per key:
 *
 *   native   the encoder's own key — passed through untouched
 *   param:N  not a key at all to the encoder; folded into the record's param
 *            table under that param's *name*
 *   drop     server-only; never reaches the encoder
 *   error    the encoder must refuse, loudly, at build time
 *
 * A key the register does not mention is passed through, so a type with no
 * `fields/<type>.ini` behaves exactly as it did before this existed. That is what
 * keeps 15 of the 20 config types byte-identical without declaring anything.
 */
struct CP_ClientView
{
    struct CP_Config config;
    /** Lines this view synthesised and therefore owns. The rest point into the
     *  merged record, which outlives the view. */
    char** owned;
    int owned_count;
    int owned_capacity;
};

static void
client_view_free(struct CP_ClientView* view)
{
    for( int i = 0; i < view->owned_count; i++ )
        free(view->owned[i]);
    free(view->owned);
    free(view->config.lines);
    memset(view, 0, sizeof(*view));
}

static int
view_push(
    struct CP_ClientView* view,
    char* key,
    char* value,
    int owns_value)
{
    if( view->config.count == view->config.capacity )
    {
        int next = view->config.capacity ? view->config.capacity * 2 : 32;
        struct CP_ConfigLine* grown =
            (struct CP_ConfigLine*)realloc(view->config.lines, (size_t)next * sizeof(*grown));

        if( !grown )
            return 0;
        view->config.lines = grown;
        view->config.capacity = next;
    }
    if( owns_value )
    {
        if( view->owned_count == view->owned_capacity )
        {
            int next = view->owned_capacity ? view->owned_capacity * 2 : 16;
            char** grown = (char**)realloc(view->owned, (size_t)next * sizeof(*grown));

            if( !grown )
                return 0;
            view->owned = grown;
            view->owned_capacity = next;
        }
        view->owned[view->owned_count++] = value;
    }
    view->config.lines[view->config.count].key = key;
    view->config.lines[view->config.count].value = value;
    view->config.lines[view->config.count].line_no = 0;
    view->config.count++;
    return 1;
}

/** The param name a `param=<name>,...` line addresses. Mirrors cp_merge.c's own
 *  `map_subkey`, which is static there for the same reason it is static here. */
static void
map_subkey(
    const char* value,
    char* out,
    size_t out_size)
{
    const char* comma = strchr(value, ',');
    size_t length = comma ? (size_t)(comma - value) : strlen(value);

    if( length >= out_size )
        length = out_size - 1;
    memcpy(out, value, length);
    out[length] = '\0';
}

/** Does the record already state `param=<name>,...` itself? */
static int
states_param(
    const struct CP_MergedRecord* rec,
    const char* name)
{
    size_t length = strlen(name);

    for( int i = 0; i < rec->count; i++ )
    {
        const char* value = rec->lines[i].value;

        if( strcmp(rec->lines[i].key, "param") != 0 )
            continue;
        if( strncmp(value, name, length) == 0 && value[length] == ',' )
            return 1;
    }
    return 0;
}

static int
client_view_build(
    struct CP_Ctx* ctx,
    const struct CP_Fields* fields,
    const struct CP_MergedRecord* rec,
    struct CP_ClientView* view,
    int* out_projected,
    int* out_errors)
{
    memset(view, 0, sizeof(*view));
    view->config.debugname = rec->debugname;

    for( int i = 0; i < rec->count; i++ )
    {
        const struct CP_Field* field = cp_fields_find(fields, rec->lines[i].key);

        /*
         * A `param=<name>,...` line is looked up by the *param name*, not by the
         * key `param`.
         *
         * `obj` states exactly one authored field and states it that way:
         * `param=levelrequire,attack,60`. The register declares `[obj.levelrequire]
         * client = drop`, and without this the filter would see the key `param`,
         * find no declaration, and hand `cp_obj.c` a line whose middle column is a
         * stat name where a param kind belongs.
         */
        if( !field && strcmp(rec->lines[i].key, "param") == 0 )
        {
            char subkey[128];

            map_subkey(rec->lines[i].value, subkey, sizeof(subkey));
            field = cp_fields_find(fields, subkey);
            /* A projected field states the param *is* how it reaches the client, so
             * the line stays. Only `drop` and `error` strip it. */
            if( field && field->client == CP_FIELD_CLIENT_PARAM )
                field = NULL;
        }

        if( field )
        {
            if( field->client == CP_FIELD_CLIENT_ERROR )
            {
                fprintf(stderr,
                        "cachepack: %s [%s]: `%s` is declared `client = error` and the record "
                        "states it — the cache cannot express this field\n",
                        fields->type, rec->debugname, field->name);
                (*out_errors)++;
                continue;
            }
            if( field->client != CP_FIELD_CLIENT_NATIVE )
                continue; /* param:N is injected below; drop never reaches the encoder */
        }
        if( !view_push(view, rec->lines[i].key, rec->lines[i].value, 0) )
            return 0;
    }

    /*
     * Then the projection.
     *
     * Skipped when the record states the param itself: `configs/all.npc` already
     * carries `param=attackrate,int,4` for 2,196 npcs, and injecting a second
     * entry for the same param would write the map twice. The record's own
     * statement is also the *merged* one, so an authored override has already won
     * by the time this runs.
     */
    for( int f = 0; f < fields->count; f++ )
    {
        const struct CP_Field* field = &fields->entries[f];
        char scratch[512];
        const char* text;
        char* line;
        int value = 0;
        int param_id;
        char kind;

        if( field->client != CP_FIELD_CLIENT_PARAM || !field->param_name[0] )
            continue;
        text = merged_value(rec, field->name, scratch, sizeof(scratch));
        if( !text || states_param(rec, field->param_name) )
            continue;
        if( !resolve_field_value(ctx, field, text, &value) )
            continue; /* already tallied by the band pass */

        param_id = cp_name_find(ctx, CP_TYPE_PARAM, field->param_name);
        if( param_id < 0 )
        {
            cp_warn(ctx, &ctx->warn_unresolved_name,
                    "%s [%s]: `client = param:%s` names no param in configs/all.param.compack",
                    fields->type, rec->debugname, field->param_name);
            continue;
        }
        kind = cp_param_type_of(ctx, param_id);

        line = (char*)malloc(600);
        if( !line )
            return 0;
        /* The machine spelling, three fields. `cp_parse_param` accepts the
         * two-field authored one too, but writing the kind here keeps the
         * projection independent of whether the param's own record loaded. */
        snprintf(line, 600, "%s,%s,%d", field->param_name, kind == 's' ? "str" : "int", value);
        if( !view_push(view, (char*)"param", line, 1) )
        {
            free(line);
            return 0;
        }
        (*out_projected)++;
    }
    return 1;
}

/**
 * Write the name tables of the namespaces the cache has no table for.
 *
 * `stat` and `category` are not records the client ignores — they are types it
 * has no concept of. They have lived as `pack/<ns>.pack` text re-read at every
 * boot, which is the same problem the server band removed for npc, so they get
 * the same destination: an archive in `server/pack`, at a group id from the
 * reserved space `cp_fields.h` justifies.
 *
 * Flat rather than an opcode band, because that is what they are. A band is a
 * per-record overlay carrying fields; these are one small table of `id -> name`
 * with no fields at all, so the payload is the table and the `kind` byte in the
 * header says which grammar it is.
 */
static int
pack_server_names(
    struct CP_Ctx* ctx,
    const char* server_dir)
{
    int group_count = 0;
    const struct CP_ServerGroup* groups = cp_server_groups(&group_count);

    for( int g = 0; g < group_count; g++ )
    {
        char path[1200];
        struct LC_Pack pack;
        int* ids;
        const char** names;
        int count = 0;
        uint8_t* table;
        uint32_t table_size;
        uint8_t* archive;
        uint32_t payload;
        uint8_t* container;
        uint32_t framed;
        uint32_t bound;

        snprintf(path, sizeof(path), "%s/pack/%s.pack", ctx->srcdir, groups[g].name);
        memset(&pack, 0, sizeof(pack));
        if( !lc_pack_load(&pack, path, groups[g].name, 1) || pack.max < 0 )
        {
            /* Absent is a normal state, not a failure: a tree that has not named
             * a namespace yet simply has no table to emit. */
            lc_pack_free(&pack);
            continue;
        }

        ids = (int*)malloc(sizeof(*ids) * (size_t)(pack.max + 1));
        names = (const char**)malloc(sizeof(*names) * (size_t)(pack.max + 1));
        if( !ids || !names )
        {
            free(ids);
            free(names);
            lc_pack_free(&pack);
            return 0;
        }
        /* Sparse in, sparse out. An id the pack does not list gets no entry —
         * the same rule `lc_pack_save_sparse` follows, and the reason the table
         * carries its ids rather than implying them from position. */
        for( int id = 0; id <= pack.max; id++ )
        {
            if( !pack.names[id] )
                continue;
            ids[count] = id;
            names[count] = pack.names[id];
            count++;
        }
        if( count == 0 )
        {
            free(ids);
            free(names);
            lc_pack_free(&pack);
            continue;
        }

        bound = 2;
        for( int i = 0; i < count; i++ )
            bound += 4 + (uint32_t)strlen(names[i]) + 1;
        table = (uint8_t*)malloc(bound);
        archive = (uint8_t*)malloc(bound + CP_SERVER_PACK_HEADER);
        container = (uint8_t*)malloc(bound + CP_SERVER_PACK_HEADER + 16);
        if( !table || !archive || !container )
        {
            free(table);
            free(archive);
            free(container);
            free(ids);
            free(names);
            lc_pack_free(&pack);
            return 0;
        }

        table_size = cp_server_names_encode(ids, names, count, table, bound);
        payload = table_size
                      ? cp_server_archive_build_payload(
                            CP_SERVER_PAYLOAD_NAMES, table, table_size, archive,
                            bound + CP_SERVER_PACK_HEADER)
                      : 0;
        framed = payload ? RSCache_ArchiveEncode(container,
                                                 bound + CP_SERVER_PACK_HEADER + 16, archive,
                                                 payload, RSCACHE_ARCHIVE_COMPRESSION_NONE, NULL)
                         : 0;
        if( !framed ||
            RSCache_Dat2DiskWriteArchive(server_dir, groups[g].group, 0, container, (int)framed) !=
                0 )
        {
            fprintf(stderr, "cachepack: %s: failed to write its name table\n", groups[g].name);
            free(table);
            free(archive);
            free(container);
            free(ids);
            free(names);
            lc_pack_free(&pack);
            return 0;
        }
        printf("  %-11s server pack: %d name(s), %d bytes in idx%d\n", groups[g].name, count,
               (int)framed, groups[g].group);
        free(table);
        free(archive);
        free(container);
        free(ids);
        free(names);
        lc_pack_free(&pack);
    }
    return 1;
}

static int
pack_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    struct RSCache_Dat2Edit* edit,
    int config_table,
    const char* server_dir,
    struct CP_PackStats* stats)
{
    const struct CP_Type* type = cp_type(type_id);
    memset(stats, 0, sizeof(*stats));

    if( type->flags & CP_TYPE_NO_ENCODER )
    {
        printf("  %-11s skipped (no encoder; source records kept)\n", type->name);
        return 1;
    }

    /*
     * Found by walking, not by constructing a path.
     *
     * `configs/all.<type>` was the only place a config record could live, and the
     * packer knew that as a `snprintf`. Discovery by extension is what lets a
     * second layer exist at all — the `server/scripts` tree states the same types in
     * the same grammar — so the path stops being knowledge the packer holds and
     * becomes something the tree answers.
     *
     * Still one file per type at this phase: `configs/` is the only root, so this
     * finds exactly what the `snprintf` found and the packed bytes are unchanged.
     * That is the bar for the change.
     */
    const char* found[CP_PACK_MAX_SOURCES];
    int found_count = cp_walk_find(&ctx->walk, type->name, found, CP_PACK_MAX_SOURCES);
    struct CP_MergeSet merged;
    struct CP_Fields fields;
    int projected = 0;
    int server_only = 0;
    int view_errors = 0;
    int ok = 1;

    if( found_count <= 0 )
    {
        /* A type the source tree does not carry is not an error: a partial unpack
         * is a normal thing to want, and the base cache still holds those records. */
        printf("  %-11s no source file\n", type->name);
        return 1;
    }

    /*
     * Merge every layer into one record set, then split it in two.
     *
     * Both encoders now run from the *same* merged record — the shape
     * `opcode_codec.h` describes and LostCity's
     * `ConfigDatIdx = { client, server }` has always had. What separates them is
     * the field register and nothing else: `client_view_build` hands the client
     * encoder the `native` keys plus the `param:N` projections, and the server
     * band takes the fields with a `server = opcode:` row.
     *
     * A record that exists only at rank 1 is therefore written for the first
     * time. That is what `configs/all.enum` has been missing: seven enums the
     * server allocated from base 5995 and nothing ever encoded.
     */
    memset(&merged, 0, sizeof(merged));
    for( int i = 0; i < found_count && ok; i++ )
    {
        struct CP_ConfigFile layer;

        if( !cp_config_file_load(&layer, found[i]) )
            continue;
        ok = cp_merge_add(&merged, &layer, i == 0 ? 0 : 1, found[i]);
        cp_config_file_free(&layer);
    }
    if( !ok )
    {
        cp_merge_free(&merged);
        return 0;
    }
    if( found_count > 1 )
        printf("  %-11s %d record(s), %d overlaid by %d file(s)\n", type->name, merged.count,
               merged.overlaid_count, found_count - 1);
    if( server_dir && !pack_server_type(ctx, type_id, &merged, server_dir, stats) )
    {
        cp_merge_free(&merged);
        return 0;
    }

    cp_fields_load(&fields, ctx->srcdir, type->name);

    /* One buffer for every record. 64 KB is comfortably past the largest config
     * record in any cache measured (the widest loc is under 2 KB); an encoder that
     * needs more returns 0 rather than overrunning, and that is reported. */
    uint8_t* buffer = malloc(64 * 1024);
    if( !buffer )
    {
        cp_merge_free(&merged);
        return 0;
    }

    for( int i = 0; i < merged.count; i++ )
    {
        const struct CP_MergedRecord* rec = &merged.records[i];
        struct CP_ClientView view;
        int id = 0;
        uint32_t size;

        /*
         * A record no cache layer states is *new*, and new records are opt-in.
         *
         * `records = client` in the type's register is the opt-in. Without it a
         * block that exists only under `server/scripts` is taken for a server
         * table wearing a config type's grammar, which is what the seven authored
         * enums are: `bank_tabs` and `worn_slots` are read by
         * `mock230_content_enum` and no client script has ever heard of them.
         * Writing them would add records with no reader.
         */
        if( rec->origin_rank > 0 && !fields.records_client )
        {
            server_only++;
            continue;
        }
        if( !resolve_id(ctx, type_id, rec->debugname, &id) )
        {
            stats->failed++;
            continue;
        }
        if( !client_view_build(ctx, &fields, rec, &view, &projected, &view_errors) )
        {
            client_view_free(&view);
            stats->failed++;
            continue;
        }
        size = type->pack(ctx, id, &view.config, buffer, 64 * 1024);
        client_view_free(&view);
        if( size == 0 )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to encode\n", type->name, rec->debugname);
            stats->failed++;
            continue;
        }
        if( !RSCache_Dat2EditPutFile(edit, config_table, type->config_kind, id, buffer, size) )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to stage\n", type->name, rec->debugname);
            stats->failed++;
            continue;
        }
        stats->records++;
        stats->bytes += (int)size;
    }

    free(buffer);
    cp_merge_free(&merged);

    printf("  %-11s %6d records, %d bytes%s\n", type->name, stats->records, stats->bytes,
           stats->failed ? " (with failures)" : "");
    if( projected )
        printf("  %-11s   %d param(s) projected from the field register\n", type->name,
               projected);
    if( server_only )
        printf("  %-11s   %d record(s) the tree adds are server-only "
               "(no `records = client` in fields/%s.ini)\n",
               type->name, server_only, type->name);
    if( view_errors )
    {
        /* `client = error` is the disposition whose whole purpose is to fail the
         * build rather than let a field vanish into the gap between the two
         * encoders. Honouring it anywhere but here would defeat it. */
        fprintf(stderr, "cachepack: %s: %d field(s) declared `client = error` were stated\n",
                type->name, view_errors);
        return 0;
    }
    return 1;
}

int
cp_pack_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* base_cache_dir,
    const char* out_cache_dir)
{
    if( base_cache_dir )
    {
        printf("Copying %s -> %s\n", base_cache_dir, out_cache_dir);
        if( tool_copy_cache_dir(base_cache_dir, out_cache_dir) != 0 )
        {
            fprintf(stderr, "cachepack: failed to copy the base cache\n");
            return 0;
        }
    }

    if( !tool_dat2_open(out_cache_dir, &ctx->profile, &ctx->cache) )
        return 0;
    ctx->cache_open = true;

    int config_table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_CONFIGS);
    if( config_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        fprintf(stderr, "cachepack: the target cache has no config table\n");
        return 0;
    }

    struct RSCache_Dat2Edit* edit = RSCache_Dat2EditNew(ctx->cache.disk);
    if( !edit )
        return 0;

    /*
     * One walk for the whole run, over two roots at two ranks. `configs/` is the
     * machine export; `server/scripts` is what a person authored on top of it, and
     * a later rank overlays an earlier one.
     *
     * The two layers state the same types in *different grammars* —
     * `configs/all.npc` uses the key names `cp_npc.c` encodes, while `lumbridge.npc`
     * uses LostCity's (`hitpoints=`, `respawnrate=`, `huntmode=`), which no cache
     * record has a field for. That is now the split rather than the obstacle: the
     * client encoder still reads only rank 0, and the keys it cannot express are
     * routed through the field register into the server pack.
     */
    {
        static const char* const ROOTS[] = { "configs", "server/scripts" };
        static const int RANKS[] = { 0, 1 };

        cp_walk_tree(&ctx->walk, ctx->srcdir, ROOTS, RANKS, 2);
    }

    /*
     * The server pack is rebuilt from nothing on every run, so it is cleared
     * before the first type rather than per type — a type whose records all lost
     * their server fields must end up with no archives, not with yesterday's.
     */
    char server_dir[1200];
    snprintf(server_dir, sizeof(server_dir), "%s/server/pack", ctx->srcdir);
    server_pack_clear(server_dir);
    if( !sel->all )
        printf("Note: --types restricts the server pack too; %s will hold only the "
               "selected types\n",
               server_dir);

    /*
     * The param types, before the type loop and not lazily.
     *
     * The register packs `loc` and `npc` before `param`, so a record stating
     * `param=death_drop,bones` is encoded while the param table would still be
     * empty — and `bones` is only obj 526 because `death_drop` is a `namedobj`.
     */
    printf("Typed %d param(s) from the tree\n", cp_param_types_load(ctx));

    printf("Packing configs from %s\n", ctx->srcdir);
    int total = 0, failed = 0, server_total = 0;
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        if( !sel->all && !(sel->mask & (1u << i)) )
            continue;
        struct CP_PackStats stats;
        if( !pack_type(ctx, i, edit, config_table, server_dir, &stats) )
        {
            RSCache_Dat2EditFree(edit);
            return 0;
        }
        total += stats.records;
        failed += stats.failed;
        server_total += stats.server_records;
    }

    /*
     * After the types, and unconditional on `--types`: `stat` and `category` are
     * not config types, so no selection mask can name them and restricting the
     * config pass has no bearing on whether their names are current.
     */
    if( !pack_server_names(ctx, server_dir) )
    {
        RSCache_Dat2EditFree(edit);
        return 0;
    }

    if( total == 0 )
    {
        fprintf(stderr, "cachepack: nothing to write\n");
        RSCache_Dat2EditFree(edit);
        return 0;
    }

    printf("Committing %d records into %s\n", total, out_cache_dir);
    bool ok = RSCache_Dat2EditCommit(edit, out_cache_dir);
    RSCache_Dat2EditFree(edit);
    if( !ok )
    {
        fprintf(stderr, "cachepack: commit failed\n");
        return 0;
    }

    printf("Done. %d records written, %d failed, %d unknown keys, %d unresolved names.\n", total,
           failed, ctx->warn_unknown_key, ctx->warn_unresolved_name);
    if( server_total )
        printf("Server pack: %d record(s) in %s\n", server_total, server_dir);
    else
        printf("Server pack: nothing declared — no fields/<type>.ini states a "
               "`server = opcode:...` row for a record the tree carries\n");
    /* A failure here is a record the target cache still holds in its old form, not
     * a corrupt one — but the caller should know the pack was partial. */
    return failed == 0;
}

/* ---- verify -------------------------------------------------------------- */

/*
 * The standing bar for every codec in this library: decode a record, write it back,
 * and compare. Here the trip is longer — record -> text -> record — so it measures
 * the text layer as well as the codecs.
 *
 * Two columns, for the same reason the library's own round-trip suite reports two:
 * a mismatch at *identical length* is a re-encoding (opcode order, or an aliased
 * opcode collapsed to its lowest spelling), while a mismatch at a different length
 * is a field that did not survive. High same-length with low exact is expected and
 * harmless; low both is a loss worth chasing.
 */
int
cp_verify_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* digest_dir)
{
    printf("Seeding names from the cache's gameval table...\n");
    cp_names_seed_from_cache(ctx);
    cp_names_report_coverage(ctx);

    printf("%-11s %8s   %8s %8s %8s   %8s %8s\n", "type", "records", "exact", "same-len",
           "differ", "codec-ex", "lost-here");

    uint8_t* buffer = malloc(64 * 1024);
    if( !buffer )
        return 0;
    uint8_t* codec_buffer = malloc(64 * 1024);
    if( !codec_buffer )
    {
        free(buffer);
        return 0;
    }
    int any_loss = 0;
    int text_regression = 0;

    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        if( !sel->all && !(sel->mask & (1u << t)) )
            continue;
        const struct CP_Type* type = cp_type(t);
        if( type->flags & CP_TYPE_NO_ENCODER )
        {
            printf("%-11s %8s   (decode only)\n", type->name, "-");
            continue;
        }
        cp_codec_fn codec = cp_codec_roundtrip(t);

        /*
         * A per-record digest of the bytes `pack` would write.
         *
         * Captured now, while the packer's output is still a pure function of
         * `configs/all.<type>`. Once a server overlay is merged in before encoding,
         * the only way to show that an *unoverlaid* record did not move is to
         * compare against a snapshot taken before the merge existed — a golden
         * captured afterwards proves nothing.
         *
         * Per record, not a whole-cache hash: a whole-cache hash says a byte moved
         * and not which record moved it, and the assertion this feeds is set
         * containment over record ids.
         */
        FILE* digests = NULL;
        if( digest_dir )
        {
            char path[1200];

            snprintf(path, sizeof(path), "%s/%s.digests", digest_dir, type->name);
            digests = fopen(path, "wb");
            if( !digests )
                fprintf(stderr, "cachepack: cannot write %s\n", path);
        }

        struct CP_Group group;
        if( !cp_group_open(ctx, t, &group) )
            continue;

        int records = 0, exact = 0, same_len = 0, differ = 0;
        int codec_exact = 0, lost_here = 0;
        struct CP_Lines lines;
        cp_lines_init(&lines);

        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;

            /* The baseline: what the library's own codec manages on this record. */
            int codec_ok = 0;
            if( codec )
            {
                uint32_t cw = codec(ctx, record, size, codec_buffer, 64 * 1024);
                codec_ok = cw == (uint32_t)size && memcmp(codec_buffer, record, (size_t)size) == 0;
                codec_exact += codec_ok;
            }

            cp_lines_clear(&lines);
            if( !type->unpack(ctx, id, record, size, &lines) )
                continue;

            /*
             * Round-trip through the *text*, not through the struct: serialising
             * the lines and parsing them back is what makes this a test of the
             * escaping and the key names rather than only of the codecs.
             */
            size_t text_size = 0;
            char* text = cp_lines_to_string(&lines, cp_name_ensure(ctx, t, id), &text_size);
            if( !text )
                break;

            struct CP_ConfigFile file;
            int parsed = cp_config_file_load_memory(&file, text, text_size, type->name);
            free(text);
            if( !parsed || file.count != 1 )
            {
                if( parsed )
                    cp_config_file_free(&file);
                differ++;
                records++;
                continue;
            }

            uint32_t written = type->pack(ctx, id, &file.configs[0], buffer, 64 * 1024);
            records++;
            if( digests && written )
                fprintf(digests, "%d=%08x\n", id,
                        RSCache_Crc32Buffer(buffer, (size_t)written));
            int text_ok = written == (uint32_t)size &&
                          memcmp(buffer, record, (size_t)size) == 0;
            if( text_ok )
                exact++;
            else if( written == (uint32_t)size )
                same_len++;
            else
                differ++;
            /* The number that matters: a record the codec reproduced and the text
             * did not is this tool's fault, and nothing else in the report
             * distinguishes it from the library's own documented losses. */
            if( codec_ok && !text_ok )
                lost_here++;
            cp_config_file_free(&file);
        }

        if( digests )
            fclose(digests);
        cp_lines_free(&lines);
        cp_group_free(&group);

        printf("%-11s %8d   %8d %8d %8d   %8d %8d%s\n", type->name, records, exact, same_len,
               differ, codec_exact, lost_here, lost_here ? "  <-- text layer" : "");
        if( differ > 0 )
            any_loss = 1;
        if( lost_here > 0 )
            text_regression = 1;
    }

    free(codec_buffer);
    free(buffer);

    if( any_loss )
        printf("\nRecords in the `differ` column changed length: a field did not survive the\n"
               "trip. `same-len` is a re-encoding (opcode order or an aliased opcode) and\n"
               "costs nothing.\n");
    printf("\n`codec-ex` is what the library's own decode->encode manages on the same\n"
           "records, with no text in between; `lost-here` counts records it reproduced\n"
           "byte-exactly and the text did not. That column is the one this tool owns,\n"
           "and it should be zero — everything else is the library's, and measured.\n");

    /* A non-zero lost-here column is a failure of this tool, so it fails the run.
     * The other columns are the library's fidelity and are reported, not judged. */
    return !text_regression;
}
