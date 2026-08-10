#include "cachepack.h"

#include "cp_assets.h"
#include "cp_register.h"

#include "dat2disk.h"
#include "filelist.h"
#include "reference_table.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define cp_mkdir(p) _mkdir(p)
#else
#define cp_mkdir(p) mkdir(p, 0755)
#endif

static int
compare_string_ptrs(
    const void* lhs,
    const void* rhs)
{
    const char* const* left = (const char* const*)lhs;
    const char* const* right = (const char* const*)rhs;
    return strcmp(*left, *right);
}

/** Merge one imported allocation file without giving either collision a winner. */
static int
merge_ported_alloc(
    struct LC_Pack* target,
    const char* path,
    const char* type)
{
    struct LC_Pack layer;

    if( !lc_pack_load(&layer, path, type, 0) )
        return 0;
    if( layer.malformed )
    {
        fprintf(stderr, "cachepack: %s has %d malformed allocation line(s)\n", path,
                layer.malformed);
        lc_pack_free(&layer);
        return 0;
    }

    for( int id = 0; id < layer.max; id++ )
    {
        const char* name = id < layer.capacity && layer.names ? layer.names[id] : NULL;
        const char* at_id =
            id < target->capacity && target->names ? target->names[id] : NULL;
        int at_name;

        if( !name )
            continue;
        at_name = lc_pack_find(target, name);
        if( at_id && strcmp(at_id, name) != 0 )
        {
            fprintf(stderr,
                    "cachepack: %s %d is `%s` in an earlier allocation layer and `%s` "
                    "in %s — imported allocation ids must be disjoint\n",
                    type, id, at_id, name, path);
            lc_pack_free(&layer);
            return 0;
        }
        if( at_name >= 0 && at_name != id )
        {
            fprintf(stderr,
                    "cachepack: `%s` is %s %d in an earlier allocation layer and %s %d "
                    "in %s — one imported name cannot mean two ids\n",
                    name, type, at_name, type, id, path);
            lc_pack_free(&layer);
            return 0;
        }
        if( !at_id && !lc_pack_set(target, id, name) )
        {
            lc_pack_free(&layer);
            return 0;
        }
    }

    lc_pack_free(&layer);
    return 1;
}

int
cp_names_load_ported_allocs(
    struct CP_Names* names,
    const char* srcdir)
{
    char root[1200];
    DIR* handle;
    struct dirent* entry;
    char** lanes = NULL;
    int lane_count = 0;
    int lane_capacity = 0;
    int ok = 1;

    snprintf(root, sizeof(root), "%s/ported", srcdir);
    handle = opendir(root);
    if( !handle )
        return 1; /* A content tree with no imported lanes is ordinary. */

    while( (entry = readdir(handle)) != NULL )
    {
        char path[1400];
        struct stat info;
        char* copy;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        if( stat(path, &info) != 0 || (info.st_mode & S_IFDIR) == 0 )
            continue;
        if( lane_count == lane_capacity )
        {
            int next = lane_capacity ? lane_capacity * 2 : 8;
            char** grown = (char**)realloc(lanes, (size_t)next * sizeof(*grown));

            if( !grown )
            {
                ok = 0;
                break;
            }
            lanes = grown;
            lane_capacity = next;
        }
        copy = strdup(entry->d_name);
        if( !copy )
        {
            ok = 0;
            break;
        }
        lanes[lane_count++] = copy;
    }
    closedir(handle);

    /* Never let readdir order decide which path a collision diagnostic names. */
    qsort(lanes, (size_t)lane_count, sizeof(*lanes), compare_string_ptrs);
    for( int lane = 0; ok && lane < lane_count; lane++ )
    {
        for( int type_id = 0; ok && type_id < CP_TYPE_COUNT; type_id++ )
        {
            const char* type = cp_type(type_id)->name;
            char path[1600];
            struct stat info;

            snprintf(path, sizeof(path), "%s/%s/pack/%s.alloc", root, lanes[lane], type);
            if( stat(path, &info) != 0 )
                continue;
            if( (info.st_mode & S_IFREG) == 0 )
            {
                fprintf(stderr, "cachepack: imported allocation is not a regular file: %s\n",
                        path);
                ok = 0;
                break;
            }
            ok = merge_ported_alloc(&names->alloc[type_id], path, type);
        }
    }

    for( int i = 0; i < lane_count; i++ )
        free(lanes[i]);
    free(lanes);
    if( !ok )
        return 0;

    /* Hold the imported layer against the cache member index too. The ordinary
     * loader performs this check before this function is called; repeat it after
     * the new layer exists instead of weakening that diagnostic. */
    for( int type_id = 0; type_id < CP_TYPE_COUNT; type_id++ )
    {
        const struct CP_Type* type = cp_type(type_id);
        const struct LC_Pack* pack = &names->packs[type_id];
        const struct LC_Pack* alloc = &names->alloc[type_id];

        for( int id = 0; id < alloc->max; id++ )
        {
            const char* name =
                id < alloc->capacity && alloc->names ? alloc->names[id] : NULL;

            if( !name )
                continue;
            if( id < pack->capacity && pack->names && pack->names[id] )
            {
                fprintf(stderr,
                        "cachepack: %s %d is bound in both configs/all.%s.compack (`%s`) "
                        "and an imported pack/%s.alloc (`%s`) — the layers must be disjoint\n",
                        type->name, id, type->name, pack->names[id], type->name, name);
                return 0;
            }
            if( lc_pack_find(pack, name) >= 0 )
            {
                fprintf(stderr,
                        "cachepack: `%s` is named by both configs/all.%s.compack (%s %d) "
                        "and an imported pack/%s.alloc (%s %d) — the layers must be disjoint\n",
                        name, type->name, type->name, lc_pack_find(pack, name), type->name,
                        type->name, id);
                return 0;
            }
        }
    }
    return 1;
}

/**
 * Where a config type's member index lives: `configs/all.<type>.compack`.
 *
 * A config record is a *file* of a config archive — `[swarm_walk]` is file 0 of
 * archive 12 — so the file binding `0=swarm_walk` is a member index, the same kind
 * of thing as `interfaces/bankmain.compack` and `textures/texture_0.compack`. It
 * used to be `configs/all.seq.compack`, which put it beside the archive-level packs and gave
 * it their extension, so `pack/` held both levels of index under one name and only
 * the reader's knowledge told them apart.
 *
 * Beside the archive it indexes, like every other member index.
 */
void
cp_config_member_index(
    char* out,
    size_t out_size,
    const char* srcdir,
    const char* type)
{
    snprintf(out, out_size, "%s/configs/all.%s.compack", srcdir, type);
}

int
cp_names_load(
    struct CP_Names* names,
    const char* srcdir)
{
    memset(names, 0, sizeof(*names));

    /* What the tree says about who owns each namespace. Consulted only when
     * *writing*; loading is unconditional. */
    cp_register_load(srcdir);
    /* A tree whose declaration contradicts the codec tables is refused here
     * rather than acted on: either answer silently loses something, and which
     * one is not recoverable afterwards. */
    if( cp_register_check() != 0 )
    {
        fprintf(stderr, "cachepack: content.ini disagrees with the codec tables; refusing\n");
        return 0;
    }

    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        const struct CP_Type* t = cp_type(i);
        char path[1200];
        cp_config_member_index(path, sizeof(path), srcdir, t->name);
        if( !lc_pack_load(&names->packs[i], path, t->name, 1) )
        {
            fprintf(stderr, "cachepack: failed to read %s\n", path);
            return 0;
        }
        /* The server's allocation ledger, layered beside the member index.
         * Absence is the common case — a namespace the server never allocated
         * into has no file — and `lc_pack_load` reports it by zeroing the
         * struct. Loaded into `alloc`, never `packs`: the save and gameval
         * paths walk `packs` alone, which is what keeps a server name out of
         * the machine-owned files by construction. */
        snprintf(path, sizeof(path), "%s/pack/%s.alloc", srcdir, t->name);
        lc_pack_load(&names->alloc[i], path, t->name, 1);

        /* The two layers must be disjoint — in ids and in names. A line bound
         * in both is how a server name creeps back into the machine-owned file
         * (and from there into `--gamevals`, the client cache's own symbol
         * table), so it is a load error for every mode rather than a silent
         * shadow — the same rule validate_name_layers stated for the old
         * `names/` layer, and validate_symbols states in the runtime. */
        for( int id = 0; id < names->alloc[i].max; id++ )
        {
            const char* aname = (id < names->alloc[i].capacity && names->alloc[i].names)
                                    ? names->alloc[i].names[id]
                                    : NULL;
            if( !aname )
                continue;
            if( id < names->packs[i].capacity && names->packs[i].names &&
                names->packs[i].names[id] )
            {
                fprintf(stderr,
                        "cachepack: %s %d is bound in both configs/all.%s.compack (`%s`) "
                        "and pack/%s.alloc (`%s`) — the layers must be disjoint\n",
                        t->name, id, t->name, names->packs[i].names[id], t->name, aname);
                return 0;
            }
            if( lc_pack_find(&names->packs[i], aname) >= 0 )
            {
                fprintf(stderr,
                        "cachepack: `%s` is named by both configs/all.%s.compack (%s %d) "
                        "and pack/%s.alloc (%s %d) — the layers must be disjoint\n",
                        aname, t->name, t->name, lc_pack_find(&names->packs[i], aname),
                        t->name, t->name, id);
                return 0;
            }
        }
    }

    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        const struct CP_Asset* asset = cp_asset(i);
        char path[1200];
        snprintf(path, sizeof(path), "%s/pack/%s.pack", srcdir, asset->pack);
        if( !lc_pack_load(&names->asset_packs[i], path, asset->filler, 1) )
        {
            fprintf(stderr, "cachepack: failed to read %s\n", path);
            return 0;
        }
    }

    /* The one namespace a config *field* refers to that is not a config type —
     * see `CP_Names.category`. A tree with no table is not an error:
     * `lc_pack_load` reports absence by zeroing the struct, and every lookup then
     * misses and says so at its own call site. */
    {
        char path[1200];
        snprintf(path, sizeof(path), "%s/pack/category.pack", srcdir);
        lc_pack_load(&names->category, path, "category", 1);
    }
    return 1;
}

int
cp_names_save(
    const struct CP_Names* names,
    const char* srcdir)
{
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s/pack", srcdir);
    cp_mkdir(dir);
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        char path[1200];

        /*
         * Every record, named or not.
         *
         * This was sparse: a record the cache does not name is emitted as the block
         * `[mapelement_0]`, so `0=mapelement_0` restates the header and used to be
         * left out — 93,000 of this tree's 306,818 index lines. The id was then
         * recoverable only by *parsing the name back*, which is the same mistake the
         * asset tables made when a model's id lived in `models/npc/goblin.model` and
         * nowhere else. Six config types had no index file at all as a result.
         *
         * An index that omits what it can re-derive is an index with a second,
         * unwritten rule. The order is explicit now, for every record.
         *
         * Writing them cannot lose an authored name or its prose: a save is a merge
         * (`lc_pack.h`), the in-memory pack was loaded from this same file, and
         * `cp_name_ensure` only ever fills an id that has no name. That is what makes
         * it safe to drop the `cp_register_may_write_pack` gate here — the gate was
         * against *regenerating* a hand-written file, and adding a line for an id
         * that has none is not that. `configs/all.param.compack`'s 58-line header
         * survives, which is the case that put the gate there.
         */
        cp_config_member_index(path, sizeof(path), srcdir, cp_type(i)->name);
        if( !lc_pack_save(&names->packs[i], path) )
        {
            fprintf(stderr, "cachepack: failed to write %s\n", path);
            return 0;
        }
    }
    /*
     * Asset packs are written **in full**, and neither the sparse rule nor the
     * register gate applies to them. Both exclusions were wrong, and together they
     * were a data-loss bug rather than untidiness.
     *
     * For a config type the pack file is an *overlay* on an index that exists
     * anyway: `configs/all.<type>` carries a `[name]` block per record, so a
     * `param_2633` line stores nothing the file does not already say, and 2,598 of
     * them buried the 36 names that mean something. Omitting filler there is
     * right.
     *
     * For an asset table the pack file **is** the index. There is no second file
     * listing the archives, and `cp_assets_import` walks the pack precisely
     * because the pack is the id authority. So:
     *
     *   - Sparse deleted the file outright when every name was `<ns>_<id>` filler,
     *     which is every unnamed table. `pack/font.pack`, `pack/model.pack`,
     *     `pack/map.pack` and a dozen more simply did not exist, and
     *     `cachepack pack --assets` then wrote *zero* archives for each of them —
     *     silently, because a pack with no lines has nothing to walk.
     *   - The register gate refused to write a namespace declared `authored`,
     *     which is most asset tables. But `names = authored` is a claim about who
     *     chooses a *name*, not about who records an *id*, and the two live in one
     *     file. Adding a line for an id that has none cannot destroy an authored
     *     name: a save merges, and `seed_pack_from_gameval` never renames an id the
     *     pack already lists. The gate was protecting against a hazard the writer
     *     no longer has.
     *
     * The cost is real and worth stating: `pack/model.pack` is 61,615 lines. That
     * is what an explicit index over 61,615 models costs, and the alternative was
     * an index that lived in the filenames — which for a renamed model
     * (`models/npc/goblin.model`) does not encode the id at all.
     */
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        /* An asset pack with nothing in it means that table was never exported.
         * Writing an empty file would suggest the table is empty, which is a
         * different claim. */
        if( names->asset_packs[i].max == 0 )
            continue;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s.pack", dir, cp_asset(i)->pack);
        if( !lc_pack_save(&names->asset_packs[i], path) )
        {
            fprintf(stderr, "cachepack: failed to write %s\n", path);
            return 0;
        }
    }

    /*
     * The config table's archive index.
     *
     * Every other cache index has a pack naming its archives; index 2's archives are
     * the config *groups*, and nothing named them — `configs/all.npc.compack` names the records
     * inside archive 9, not the archive. So the twenty groups were the only archives
     * in the cache with no index entry anywhere, and "which archive is a seq in?" was
     * answerable only by reading `cp_types.c`.
     *
     * Written from the same table the decoders are driven by, so it cannot drift from
     * what the tool actually reads.
     */
    {
        struct LC_Pack configs;
        char path[1200];

        snprintf(path, sizeof(path), "%s/2_configs.pack", dir);
        if( !lc_pack_load(&configs, path, "configs", 1) )
        {
            fprintf(stderr, "cachepack: failed to read %s\n", path);
            return 0;
        }
        /* Only when the file has none: a save lets the caller's comments win, so
         * setting this unconditionally would overwrite an edited header every run —
         * which is how `configs/all.param.compack` lost its 58 lines the first time. */
        if( !configs.preamble )
            configs.preamble = strdup(
                "// The archives of cache index 2, the config table. Each one holds the\n"
                "// records of a single config type, and `pack/<type>.pack` names those\n"
                "// records — this file names the archives they live in.\n"
                "//\n"
                "// The id is what the client addresses the group by, which is why they are\n"
                "// not consecutive: the gaps are groups this revision has no decoder for.\n"
                "\n");
        for( int i = 0; i < CP_TYPE_COUNT; i++ )
        {
            const struct CP_Type* type = cp_type(i);

            if( type->config_kind >= 0 )
                lc_pack_set(&configs, type->config_kind, type->name);
        }
        if( !lc_pack_save(&configs, path) )
        {
            fprintf(stderr, "cachepack: failed to write %s\n", path);
            lc_pack_free(&configs);
            return 0;
        }
        lc_pack_free(&configs);
    }
    return 1;
}

const char*
cp_component_name(
    struct CP_Ctx* ctx,
    int interface_id,
    int child_id)
{
    const struct LC_Pack* pack = &ctx->names.components;
    int id = (interface_id << 16) | child_id;

    if( interface_id < 0 || child_id < 0 || id < 0 || id >= pack->capacity || !pack->names )
        return NULL;
    return pack->names[id];
}

void
cp_names_free(struct CP_Names* names)
{
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
        lc_pack_free(&names->packs[i]);
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
        lc_pack_free(&names->asset_packs[i]);
    lc_pack_free(&names->category);
    lc_pack_free(&names->components);
    for( int t = 0; t < names->dbtable_count; t++ )
    {
        for( int c = 0; c < names->dbtable_column_count[t]; c++ )
            free(names->dbtable_columns[t][c]);
        free(names->dbtable_columns[t]);
    }
    free(names->dbtable_columns);
    free(names->dbtable_column_count);
    names->dbtable_columns = NULL;
    names->dbtable_column_count = NULL;
    names->dbtable_count = 0;
}

/*
 * A gameval name is content-authored and mostly already in the shape a pack file
 * wants, but nothing guarantees it: the format is a free string. A name carrying a
 * space, an `=` or a `]` would produce a pack line or a block header that reads
 * back as something else, so it is normalised here rather than trusted.
 */
static void
sanitise_name(
    const char* in,
    int in_len,
    char* out,
    int out_size)
{
    int w = 0;
    for( int i = 0; i < in_len && w < out_size - 1; i++ )
    {
        unsigned char c = (unsigned char)in[i];
        if( isalnum(c) || c == '_' || c == '.' || c == '+' || c == '-' )
            out[w++] = (char)c;
        else
            out[w++] = '_';
    }
    out[w] = '\0';
}

/**
 * Make `base` unique within `pack` by appending `i2`, `i3`, ... on collision.
 *
 * Duplicate gameval names do occur (the index names two records the same thing),
 * and a pack file that binds one name to two ids loses whichever it reads second.
 * The suffix is the same shape LostCity's unpacker uses for the same problem.
 */
static void
uniquify(
    struct LC_Pack* pack,
    char* name,
    int name_size)
{
    if( lc_pack_find(pack, name) < 0 )
        return;
    char base[240];
    snprintf(base, sizeof(base), "%s", name);
    for( int i = 2; i < 10000; i++ )
    {
        snprintf(name, (size_t)name_size, "%si%d", base, i);
        if( lc_pack_find(pack, name) < 0 )
            return;
    }
}

/* ---- the dbtable column names ------------------------------------------- */

/**
 * Take ownership of one table's column names, growing the table array to reach it.
 *
 * Called once per dbtable record during the archive-10 seed. A second call for the
 * same id replaces the first, which only happens if a cache lists an id twice.
 */
static void
cp_db_columns_set(
    struct CP_Names* names,
    int table_id,
    char** columns,
    int column_count)
{
    if( table_id < 0 )
    {
        for( int i = 0; i < column_count; i++ )
            free(columns[i]);
        free(columns);
        return;
    }
    if( table_id >= names->dbtable_count )
    {
        int want = table_id + 1;
        char*** grown = (char***)realloc(names->dbtable_columns,
                                         (size_t)want * sizeof(*names->dbtable_columns));
        int* grown_counts =
            (int*)realloc(names->dbtable_column_count, (size_t)want * sizeof(*grown_counts));

        if( grown )
            names->dbtable_columns = grown;
        if( grown_counts )
            names->dbtable_column_count = grown_counts;
        if( !grown || !grown_counts )
        {
            for( int i = 0; i < column_count; i++ )
                free(columns[i]);
            free(columns);
            return;
        }
        for( int i = names->dbtable_count; i < want; i++ )
        {
            names->dbtable_columns[i] = NULL;
            names->dbtable_column_count[i] = 0;
        }
        names->dbtable_count = want;
    }
    for( int i = 0; i < names->dbtable_column_count[table_id]; i++ )
        free(names->dbtable_columns[table_id][i]);
    free(names->dbtable_columns[table_id]);
    names->dbtable_columns[table_id] = columns;
    names->dbtable_column_count[table_id] = column_count;
}

const char*
cp_db_column_name(
    const struct CP_Names* names,
    int table_id,
    int column)
{
    if( !names || table_id < 0 || table_id >= names->dbtable_count )
        return NULL;
    if( column < 0 || column >= names->dbtable_column_count[table_id] )
        return NULL;
    return names->dbtable_columns[table_id][column];
}

/**
 * The name out of one gameval record, for the archives that are not flat strings.
 *
 * Archive 10 (dbtable) is the second nested archive in the table, and nothing knew
 * it. A record is a keyed sequence, terminated by a 0 byte:
 *
 *     u8 key; cstring text     key 1      the table's name
 *                              key n >= 2 the name of column (n - 2)
 *
 * Read flat by `sanitise_name` — every byte outside `[A-Za-z0-9_.+-]` collapsing to
 * `_` — table 0 came out as
 * `_quest__id__sortname__displayname__release_type__…`, truncated at 255 characters
 * by the caller's buffer. That is not a derived name and not a bad name; it is
 * `quest` plus its 49 column names with the framing bytes turned into underscores,
 * and it was the spelling every dbtable in the tree answered to.
 *
 * Verified over cache.osrs239's 246 records: all 246 parse to exactly this shape
 * with keys 2..N ascending and contiguous, a single terminator and no trailing
 * bytes; the flat read of the same bytes reproduces all 246 previous pack lines
 * character for character; and no column any of the 246 config records declares,
 * or any of 422 clientscripts reads through `(table << 12) | (column << 4)`, sits
 * above the column count this decode yields.
 *
 * `out_columns`/`out_column_count` may be NULL when only the name is wanted;
 * otherwise they receive a freshly allocated array of freshly allocated names,
 * which the caller owns. A column key that skips a position is a decode failure
 * rather than a hole, because the position *is* the column id.
 *
 * Returns 0 when the record does not have this shape, so the caller falls back to
 * the flat read rather than inventing a name.
 */
static int
keyed_gameval_name(
    const char* record,
    int size,
    char* out,
    int out_size,
    char*** out_columns,
    int* out_column_count)
{
    const unsigned char* p = (const unsigned char*)record;
    int at = 0;
    int last_key = 0;
    char** columns = NULL;
    int column_count = 0;
    int named = 0;

    if( out_columns )
    {
        *out_columns = NULL;
        *out_column_count = 0;
    }

    while( at < size )
    {
        int key = p[at++];
        int start;

        if( key == 0 )
            break;
        if( key <= last_key )
            goto fail; /* keys are ascending; anything else is not this format */
        last_key = key;
        start = at;
        while( at < size && p[at] != 0 )
            at++;
        if( at >= size )
            goto fail; /* unterminated: not this format */
        if( key == 1 )
        {
            sanitise_name(record + start, at - start, out, out_size);
            named = out[0] != '\0';
        }
        else if( out_columns )
        {
            char name[256];
            char** grown;

            /* Key n names column n-2, so the key *is* the position. A gap would
             * silently shift every later column onto the wrong types. */
            if( key - 2 != column_count )
                goto fail;
            sanitise_name(record + start, at - start, name, sizeof(name));
            grown = (char**)realloc(columns, (size_t)(column_count + 1) * sizeof(*columns));
            if( !grown )
                goto fail;
            columns = grown;
            columns[column_count] = strdup(name);
            if( !columns[column_count] )
                goto fail;
            column_count++;
        }
        at++; /* the terminator */
    }
    if( !named )
        goto fail;
    if( out_columns )
    {
        *out_columns = columns;
        *out_column_count = column_count;
    }
    return 1;

fail:
    for( int i = 0; i < column_count; i++ )
        free(columns[i]);
    free(columns);
    return 0;
}

/**
 * Seed one pack from one gameval archive.
 *
 * `ids`/`id_count` is the ascending list of ids the *records* actually use — a
 * config group's record ids, or an asset table's archive ids. It is what makes
 * the claim checkable: the archive-id-to-kind mapping is recorded nowhere in
 * the cache, so "archive 14 names interfaces" is an assertion until 969 of its
 * 969 ids turn out to be interfaces that exist.
 *
 * Returns 1 when the archive was trusted and the pack seeded.
 */
static int
seed_pack_from_gameval(
    struct CP_Ctx* ctx,
    int gv_table,
    int gameval_archive,
    const char* kind,
    struct LC_Pack* pack,
    const int* ids,
    int id_count)
{
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, gv_table, gameval_archive);
    if( !archive )
        return 0;
    if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    /*
     * Verify before trusting. An archive naming some other kind fails
     * immediately and loudly; a partially-stale one (a handful of ids the group
     * has since dropped) is tolerated, because that is what a live index looks
     * like and refusing it would throw away every good name with it.
     */
    int matched = 0;
    if( ids && id_count > 0 )
    {
        for( int f = 0; f < files->file_count; f++ )
        {
            int fid = archive->file_ids ? archive->file_ids[f] : f;
            int lo = 0, hi = id_count - 1;
            while( lo <= hi )
            {
                int mid = (lo + hi) / 2;
                if( ids[mid] == fid )
                {
                    matched++;
                    break;
                }
                if( ids[mid] < fid )
                    lo = mid + 1;
                else
                    hi = mid - 1;
            }
        }
    }

    int total = files->file_count;
    if( total == 0 || matched * 10 < total * 9 )
    {
        fprintf(stderr,
                "cachepack: gameval archive %d does not name %s (%d/%d ids match) — "
                "using %s_<id>\n",
                gameval_archive, kind, matched, total, kind);
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    for( int f = 0; f < files->file_count; f++ )
    {
        int fid = archive->file_ids ? archive->file_ids[f] : f;
        char name[256];
        int keyed = 0;

        if( files->file_sizes[f] <= 0 )
            continue;

        /*
         * Archive 10's records are keyed, not flat, and the decode runs *before*
         * the name-wins check below rather than after it.
         *
         * The columns are not a name, so the rule that protects an authored name
         * must not also suppress them: a tree that already names its tables — which
         * after one unpack is every tree — would otherwise skip the record whole and
         * come back with no column names at all.
         */
        if( gameval_archive == 10 )
        {
            char** columns = NULL;
            int column_count = 0;

            keyed = keyed_gameval_name(files->files[f], files->file_sizes[f], name,
                                       sizeof(name), &columns, &column_count);
            if( keyed && columns )
                cp_db_columns_set(&ctx->names, fid, columns, column_count);
            else
                free(columns);
        }

        /*
         * A name already in the pack wins over the gameval, so re-unpacking a
         * cache over a tree never renames anything. On a first unpack that is
         * every id; thereafter it is whatever a previous unpack of the same
         * cache already wrote, which is the same answer.
         *
         * With one file per namespace this rule is also what protects an
         * *authored* name: `varp 843` reads `varp_weapon_category  // cache:
         * randomhitsound` because this world repurposed the id, and a re-seed
         * leaves it alone rather than putting the cache's name back.
         */
        if( fid >= 0 && fid < pack->capacity && pack->names && pack->names[fid] )
            continue;
        if( !keyed )
            sanitise_name(files->files[f], files->file_sizes[f], name, sizeof(name));
        if( !name[0] )
            continue;
        uniquify(pack, name, sizeof(name));
        lc_pack_set(pack, fid, name);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return 1;
}

/*
 * Gameval archive 14 is not a flat name list — it names interfaces *and their
 * components*, and one record holds both:
 *
 *     <interface name> \0
 *     repeat:  u16 child_id   <component name> \0
 *     0xffff
 *
 * Reading it as a flat string produces exactly what you would expect —
 * `messagebox...safezone...continue...` as one 63-character "name" — which is
 * how it read before this function existed.
 *
 * Across cache.osrs239 it decodes to 969 interfaces and 26,519 components with
 * no malformed record, and it settles a question the tree had been answering
 * with a *different server's* table: the cache says `bankmain:items` is child
 * 12, and the imported name for child 13 called it `bankmain_items`. Child 13
 * is `scrollbar`.
 *
 * Components are written straight out rather than kept in `CP_Names`, because
 * they are not a cache table: no config record references a component, so
 * nothing in cachepack ever resolves one. The file exists for the server and
 * the script compiler, and being wholly derived from the cache it is safe to
 * regenerate every time.
 */
static int
seed_interface_names(
    struct CP_Ctx* ctx,
    int gv_table,
    const char* srcdir)
{
    const struct CP_Asset* asset = cp_asset(CP_ASSET_INTERFACE);
    struct LC_Pack* pack = &ctx->names.asset_packs[CP_ASSET_INTERFACE];

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->cache.disk, gv_table, asset->gameval_archive);
    if( !archive )
        return 0;
    if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->cache.disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    struct LC_Pack* components = &ctx->names.components;
    memset(components, 0, sizeof(*components));
    snprintf(components->type, sizeof(components->type), "%s", "component");

    int interfaces = 0;
    int coms = 0;
    int malformed = 0;
    /* This record's component names so far, for the per-interface uniquify below.
     * Reused across records to keep the allocation out of the loop. */
    char** seen = NULL;
    int seen_count = 0;
    int seen_capacity = 0;

    for( int f = 0; f < files->file_count; f++ )
    {
        int iface = archive->file_ids ? archive->file_ids[f] : f;
        const unsigned char* bytes = (const unsigned char*)files->files[f];
        int len = files->file_sizes[f];
        int at = 0;

        for( int c = 0; c < seen_count; c++ )
            free(seen[c]);
        seen_count = 0;

        if( len <= 0 )
            continue;

        while( at < len && bytes[at] )
            at++;
        char iface_name[256];
        sanitise_name((const char*)bytes, at, iface_name, sizeof(iface_name));
        if( !iface_name[0] )
            continue;
        at++;

        /* Re-seeded, but never renamed: a previous unpack of the same cache
         * already wrote this, and an authored rename in the same file must
         * survive the re-seed rather than be put back. */
        if( !(iface >= 0 && iface < pack->capacity && pack->names && pack->names[iface]) )
        {
            char unique[256];
            snprintf(unique, sizeof(unique), "%s", iface_name);
            uniquify(pack, unique, sizeof(unique));
            lc_pack_set(pack, iface, unique);
        }
        interfaces++;

        while( at + 2 <= len )
        {
            int child = (bytes[at] << 8) | bytes[at + 1];
            if( child == 0xffff )
                break;
            at += 2;
            int start = at;
            while( at < len && bytes[at] )
                at++;

            char child_name[256];
            sanitise_name((const char*)bytes + start, at - start, child_name, sizeof(child_name));
            at++;
            if( !child_name[0] )
                continue;

            /*
             * `<interface>:<child>`, which is LostCity's own spelling for the
             * same thing — and the qualification is not decoration: 96 of these
             * interfaces have a component called `universe` and 40 have one
             * called `frame`.
             *
             * Qualifying by interface is not enough on its own, though: one
             * interface can name two of its own children the same thing. In
             * cache.osrs239 `cws_doomsayer` does it nine times — two components
             * each called `fairy_rings`, `genie_cave`, `mort_myre` and so on — and
             * a pack file that binds one name to two ids resolves to whichever the
             * loader reads first. So the same `i2`/`i3` suffix the interface names
             * already use applies here, which makes the second one addressable
             * rather than shadowed.
             */
            char full[520];
            snprintf(full, sizeof(full), "%s:%s", iface_name, child_name);
            /*
             * Uniquified against *this interface's* children rather than the whole
             * pack, which is not a shortcut — it is the same answer. Every name
             * here is `<iface>:<child>` and the interface names are themselves
             * unique, so two children can only collide inside one record. Scanning
             * all 26,491 instead would be 350 million string compares for the same
             * result.
             */
            for( int attempt = 2; attempt < 10000; attempt++ )
            {
                int taken = 0;
                for( int c = 0; c < seen_count; c++ )
                {
                    if( strcmp(seen[c], full) == 0 )
                    {
                        taken = 1;
                        break;
                    }
                }
                if( !taken )
                    break;
                snprintf(full, sizeof(full), "%s:%si%d", iface_name, child_name, attempt);
            }
            if( seen_count == seen_capacity )
            {
                int next = seen_capacity ? seen_capacity * 2 : 64;
                char** grown = realloc(seen, (size_t)next * sizeof(char*));
                if( grown )
                {
                    seen = grown;
                    seen_capacity = next;
                }
            }
            if( seen_count < seen_capacity )
            {
                seen[seen_count] = strdup(full);
                if( seen[seen_count] )
                    seen_count++;
            }
            lc_pack_set(components, (iface << 16) | child, full);
            coms++;
        }
        if( at + 2 > len || ((bytes[at] << 8) | bytes[at + 1]) != 0xffff )
            malformed++;
    }

    for( int c = 0; c < seen_count; c++ )
        free(seen[c]);
    free(seen);

    if( malformed )
        fprintf(stderr, "cachepack: %d of %d interface name records did not end cleanly\n",
                malformed, files->file_count);

    /*
     * Not written anywhere of its own: these names become the block names in each
     * interface's `.compack`, which is already the index over exactly these members.
     * `pack/component.pack` was a second index over the same thing, and the global
     * id it keyed on is `(interface << 16) | child` — recoverable from the two files
     * that remain, which is how the client composes it in the first place.
     */
    printf("  %-11s %6d interfaces, %d components -> interfaces/<name>.compack\n",
           "gameval", interfaces, coms);
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return 1;
}

void
cp_names_seed_from_cache(struct CP_Ctx* ctx)
{
    if( !ctx->cache_open )
        return;
    int gv_table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_GAMEVALS);
    if( gv_table == RSCACHE_DAT2_DISK_TABLE_ABSENT || !ctx->cache.disk->tables[gv_table] )
    {
        fprintf(stderr, "cachepack: no gameval table; names will be <type>_<id>\n");
        return;
    }

    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        const struct CP_Type* type = cp_type(t);
        if( type->gameval_archive < 0 )
            continue;

        int* ids = NULL;
        int id_count = 0;
        cp_record_ids(ctx, t, &ids, &id_count);
        if( seed_pack_from_gameval(ctx, gv_table, type->gameval_archive, type->name,
                                   &ctx->names.packs[t], ids, id_count) )
            ctx->names.from_gameval[t] = true;
        free(ids);
    }

    /*
     * The asset kinds the cache also names.
     *
     * Archives 11, 12 and 14 name songs, sprites and interfaces, and nothing
     * read them until now — so `pack/interface.pack` was 934 lines of
     * `interface_<id>` with 34 names copied from another server's table
     * spliced in, when the cache says `toplevel_osrs_stretch` for 161 and
     * `bankmain` for 12 itself.
     *
     * An asset's ids are its table's archive ids, which is the list that makes
     * the claim checkable in exactly the way a config group's record ids do.
     */
    for( int a = 0; a < CP_ASSET_COUNT; a++ )
    {
        const struct CP_Asset* asset = cp_asset(a);
        if( asset->gameval_archive < 0 )
            continue;

        /* The one structured archive: it carries each interface's components
         * alongside its name, so it needs its own reader. */
        if( a == CP_ASSET_INTERFACE )
        {
            seed_interface_names(ctx, gv_table, ctx->srcdir);
            continue;
        }

        int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
        if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
            continue;
        struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table_id];
        if( !rt )
            continue;

        seed_pack_from_gameval(ctx, gv_table, asset->gameval_archive, asset->pack,
                               &ctx->names.asset_packs[a], rt->ids, rt->id_count);
    }

    /*
     * Then the names that come from the *configs* rather than from a name table.
     *
     * A model has no gameval archive, so the only evidence of what it is is the
     * record that references it: `cp_assets_name_models` walks the npc, obj, loc,
     * idk and spotanim groups and names each model after its first claimant, which
     * is how a model lands at `models/npc/goblin.model`. Maps are named from their
     * coordinates the same way.
     *
     * This used to run only inside `cp_assets_export`, and that was the second half
     * of the index bug. The export wrote `models/npc/goblin.model` and the sparse
     * write then deleted `pack/model.pack`, so the name→id mapping existed *only*
     * in a path that does not contain the id — and 53,390 of osrs239's 61,615
     * models became unpackable. Deriving the names whenever the index is built,
     * rather than only when files are written, is what makes that recoverable:
     * they come back from the configs on the next unpack.
     */
    cp_assets_name_models(ctx);
    cp_assets_name_maps(ctx);
    cp_assets_name_worldmap(ctx);

    /*
     * Then a line for **every** archive id in every asset table, named or not.
     *
     * The pack file is an asset table's *index*, and an index has to list what
     * exists. Leaving the unnamed ids out looked harmless because a filler name is
     * a function of the id — but it is only recoverable from a *filename*, and two
     * things break that:
     *
     *   - a renamed asset has no id in its path at all. `cp_assets_name_models`
     *     names models after the configs that reference them, so a model lands at
     *     `models/npc/goblin.model` and nothing but a pack line says it is 1234.
     *   - the sparse write deletes a file whose every line is filler, which is
     *     every table the cache does not name. `pack/model.pack`, `pack/font.pack`
     *     and `pack/map.pack` therefore did not exist — and `cp_assets_import`
     *     walks the pack, so `cachepack pack --assets` wrote *zero* archives for
     *     each of them, silently, because there was nothing to walk.
     *
     * Runs after the gameval pass on purpose: `cp_asset_name_ensure` does not
     * rename, so filling first would lock every sprite to `sprite_<id>` and shut
     * the cache's own names out.
     */
    for( int a = 0; a < CP_ASSET_COUNT; a++ )
    {
        const struct CP_Asset* asset = cp_asset(a);
        int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
        if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
            continue;
        struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table_id];
        if( !rt )
            continue;
        struct LC_Pack* pack = &ctx->names.asset_packs[a];
        int filled = 0;

        for( int i = 0; i < rt->id_count; i++ )
        {
            int id = rt->ids[i];
            if( id >= 0 && id < pack->capacity && pack->names && pack->names[id] )
                continue;
            /*
             * `lc_pack_set` rather than `cp_asset_name_ensure`, because the latter
             * uniquifies and `uniquify` is a linear scan of the pack: over 61,615
             * models that is 1.9 billion string compares for an answer already
             * known. A `<ns>_<id>` name cannot collide with another one — it
             * encodes the id — and a *real* name that happens to look like one is
             * caught at boot by the duplicate-name check rather than here.
             */
            char name[256];
            snprintf(name, sizeof(name), "%s_%d", asset->filler, id);
            lc_pack_set(pack, id, name);
            filled++;
        }
        if( filled )
            printf("  %-11s %6d unnamed %s ids indexed\n", "index", filled, asset->pack);
    }
}

/*
 * How much of this cache has a name someone chose.
 *
 * A useful number precisely because it is mostly small. For the namespaces the
 * cache names itself it is near 100% and says nothing; for `model`, `synth` and
 * `map` the pack file *is* the name table rather than an overlay on one, so the
 * ratio is a literal progress metric — 61,615 models, and however many of them
 * anyone has bothered to identify.
 *
 * Filler is excluded on purpose: `model_1234` is the id spelled twice and
 * counting it would report 100% coverage of a table nobody has looked at.
 */
void
cp_names_report_coverage(struct CP_Ctx* ctx)
{
    if( !ctx->cache_open )
        return;

    printf("Naming coverage (names someone chose / records in the cache)\n");
    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        int* ids = NULL;
        int count = 0;
        cp_record_ids(ctx, t, &ids, &count);
        free(ids);
        if( count <= 0 )
            continue;
        int named = lc_pack_named_count(&ctx->names.packs[t]);
        printf("  %-16s %7d / %7d  %3d%%%s\n", cp_type(t)->name, named, count,
               count ? named * 100 / count : 0,
               ctx->names.from_gameval[t] ? "  (from the cache's own table)" : "");
    }
    for( int a = 0; a < CP_ASSET_COUNT; a++ )
    {
        const struct CP_Asset* asset = cp_asset(a);
        int table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, asset->table);
        if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT || !ctx->cache.disk->tables[table_id] )
            continue;
        int count = ctx->cache.disk->tables[table_id]->id_count;
        if( count <= 0 )
            continue;
        int named = lc_pack_named_count(&ctx->names.asset_packs[a]);
        printf("  %-16s %7d / %7d  %3d%%%s\n", asset->pack, named, count,
               count ? named * 100 / count : 0,
               asset->gameval_archive >= 0 ? "  (from the cache's own table)" : "");
    }
}

const char*
cp_name_get(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int id)
{
    /* The member index first, then the allocation ledger. The two never bind
     * the same id — an alloc id is past the cache's high-water mark by
     * construction, and the migration is checked — so this is a union, not a
     * precedence. */
    struct LC_Pack* pack = &ctx->names.packs[type];
    struct LC_Pack* alloc = &ctx->names.alloc[type];

    if( id >= 0 && id < pack->capacity && pack->names && pack->names[id] )
        return pack->names[id];
    if( id >= 0 && id < alloc->capacity && alloc->names )
        return alloc->names[id];
    return NULL;
}

const char*
cp_name_ensure(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int id)
{
    const char* existing = cp_name_get(ctx, type, id);
    if( existing )
        return existing;
    char name[256];
    snprintf(name, sizeof(name), "%s_%d", cp_type(type)->name, id);
    uniquify(&ctx->names.packs[type], name, sizeof(name));
    lc_pack_set(&ctx->names.packs[type], id, name);
    return cp_name_get(ctx, type, id);
}

int
cp_name_find(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* name)
{
    /*
     * The index, and only the index.
     *
     * This used to fall back to reading the id out of the name — `param_2633` is a
     * function of 2633, so it resolved without a line. That made the *spelling of a
     * block header* load-bearing: rename `[param_2633]` and the record silently moves
     * or disappears, and an index that omits every such record is not an index.
     *
     * Every record has a line now (`cp_names_save`), so a miss here is a real miss
     * and is reported as one rather than guessed at.
     */
    int id = lc_pack_find(&ctx->names.packs[type], name);
    if( id >= 0 )
        return id;
    return lc_pack_find(&ctx->names.alloc[type], name);
}

int
cp_name_find_alloc(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* name)
{
    return lc_pack_find(&ctx->names.alloc[type], name);
}

/* ---- asset names -------------------------------------------------------- */

const char*
cp_asset_name_get(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id)
{
    struct LC_Pack* pack = &ctx->names.asset_packs[asset];

    if( id < 0 || id >= pack->capacity || !pack->names )
        return NULL;
    return pack->names[id];
}

void
cp_asset_name_set(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id,
    const char* name)
{
    char unique[300];
    snprintf(unique, sizeof(unique), "%s", name);
    uniquify(&ctx->names.asset_packs[asset], unique, sizeof(unique));
    lc_pack_set(&ctx->names.asset_packs[asset], id, unique);
}

const char*
cp_asset_name_ensure(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id)
{
    const char* existing = cp_asset_name_get(ctx, asset, id);
    if( existing )
        return existing;
    char name[256];
    snprintf(name, sizeof(name), "%s_%d", cp_asset(asset)->filler, id);
    cp_asset_name_set(ctx, asset, id, name);
    return cp_asset_name_get(ctx, asset, id);
}

int
cp_asset_name_find(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    const char* name)
{
    int id = lc_pack_find(&ctx->names.asset_packs[asset], name);

    if( id >= 0 )
        return id;
    /* `interface_412.if` on disk is interface 412 by construction, so the file
     * repacks with no pack line behind it. */
    return lc_pack_synthetic_id(cp_asset(asset)->filler, name);
}

/* ---- emitting the gamevals back ------------------------------------------ */

/*
 * `docs/CONTENT_PACK_PLAN.md` §5.5 — write `pack/<ns>.pack` back into the cache's
 * own symbol table, so the cache is self-describing.
 *
 * Layer 0 came from here in the first place, and nothing stops the trip running
 * the other way: a gameval archive is a FileList whose file id is the record id
 * and whose contents are the name string. Anything pointed at the cache alone
 * then recovers your names without the content tree beside it.
 *
 * Four things worth knowing before reading the code, all from §5.5:
 *
 *   **Not a faithful round trip.** `sanitise_name` collapses anything outside
 *   `[A-Za-z0-9_.+-]` to `_` and `uniquify` appends `i2` on a collision, so
 *   regenerating overwrites Jagex's original strings with normalised ones. Fine
 *   for a cache you own, and another reason the pristine original stays archived.
 *
 *   **Archive 14 is nested** — one record holds an interface's name *and* every
 *   component as `u16 child` + name pairs. Emitting it means re-merging two
 *   sources back into that shape, so it is skipped and said out loud rather than
 *   written flat, which would destroy 26,491 component names.
 *
 *   **Sparse in, sparse out.** Filler names are not stored in the pack, so an
 *   unnamed id simply gets no file. The reference table's child list carries the
 *   ids, which is what makes that work without a dense array.
 *
 *   **The 90% verification becomes vacuous.** `seed_pack_from_gameval` rejects an
 *   archive when fewer than 90% of its ids are real record ids; an archive we
 *   generated passes by construction. Do not read a later seed as validation.
 *
 * The client never opens this table — nothing outside cachepack does — so this
 * cannot break a boot, which is exactly why §5.5 calls it safe to add late.
 */
static int
emit_gameval_archive(
    struct CP_Ctx* ctx,
    const char* out_cache_dir,
    int table_id,
    int archive_id,
    const char* ns,
    const struct LC_Pack* pack,
    int* out_dirty)
{
    struct RSCache_FileList list;
    int* ids;
    int count = 0;
    uint8_t* payload;
    uint32_t bound;
    uint32_t written;
    uint8_t* container;
    uint32_t container_size;
    int rc;

    /* Same rule as pack_server_names: `max` is one past the highest id, and a
     * pack that loaded nothing has a NULL `names`. */
    if( !pack->names || pack->max <= 0 )
        return 0;

    memset(&list, 0, sizeof(list));
    ids = (int*)calloc((size_t)pack->max + 1, sizeof(*ids));
    list.files = (char**)calloc((size_t)pack->max + 1, sizeof(*list.files));
    list.file_sizes = (int*)calloc((size_t)pack->max + 1, sizeof(*list.file_sizes));
    if( !ids || !list.files || !list.file_sizes )
    {
        free(ids);
        free(list.files);
        free(list.file_sizes);
        return 0;
    }

    for( int id = 0; id < pack->max; id++ )
    {
        if( !pack->names[id] )
            continue;
        ids[count] = id;
        /* The name without its terminator: the archive stores the bytes, and the
         * reader takes the length from the size table. */
        list.files[count] = pack->names[id];
        list.file_sizes[count] = (int)strlen(pack->names[id]);
        count++;
    }
    list.file_count = count;
    if( count == 0 )
    {
        free(ids);
        free(list.files);
        free(list.file_sizes);
        return 0;
    }

    bound = RSCache_FileListEncodeBound(&list);
    payload = (uint8_t*)malloc(bound ? bound : 1);
    if( !payload )
    {
        free(ids);
        free(list.files);
        free(list.file_sizes);
        return 0;
    }
    written = RSCache_FileListEncode(&list, payload, bound);
    /* The members are borrowed from the pack, so only the two index arrays here
     * are ours to release — `RSCache_FileListFree` would free the pack's names. */
    free(list.files);
    free(list.file_sizes);
    if( written == 0 )
    {
        free(ids);
        free(payload);
        return 0;
    }

    bound = RSCache_ArchiveEncodeBound(written, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
    container = (uint8_t*)malloc(bound ? bound : 1);
    if( !container )
    {
        free(ids);
        free(payload);
        return 0;
    }
    container_size = RSCache_ArchiveEncode(container, bound, payload, written,
                                           RSCACHE_ARCHIVE_COMPRESSION_GZIP, NULL);
    free(payload);
    if( container_size == 0 )
    {
        free(ids);
        free(container);
        return 0;
    }

    rc = RSCache_Dat2DiskWriteArchive(out_cache_dir, table_id, archive_id, container,
                                      (int)container_size);
    if( rc == 0 )
        cp_reference_sync(ctx, table_id, archive_id, container, (int)container_size, ids, count,
                          out_dirty);
    free(container);
    free(ids);
    if( rc != 0 )
    {
        fprintf(stderr, "cachepack: gameval archive %d (%s) failed to write\n", archive_id, ns);
        return 0;
    }
    printf("  %-11s %6d name(s) -> gameval archive %d\n", ns, count, archive_id);
    return 1;
}

int
cp_names_emit_gamevals(
    struct CP_Ctx* ctx,
    const char* out_cache_dir)
{
    int table_id;
    int dirty = 0;
    int archives = 0;

    if( !ctx->cache_open )
        return 1;
    table_id = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_GAMEVALS);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        /* Absent in the pre-dat2 epoch, and a normal state rather than a fault.
         * This is a property of the REVISION, so it is the only thing that can
         * legitimately skip the emit. */
        printf("Gamevals: this revision has no symbol table; nothing emitted\n");
        return 1;
    }
    /*
     * `!disk->tables[table_id]` used to skip here too, and that conflated the
     * revision not having a gameval table with this cache not having one YET.
     * disk->tables is populated from the .idxN present when the cache was
     * opened, so on a cache packed from the tree alone it is empty for every
     * table — and the emit silently did nothing, leaving idx24 the one table a
     * from-scratch bake never produced.
     *
     * Nothing needs to be created here: the writes below go through
     * cp_reference_sync, which builds the reference table when the cache has
     * none (see cp_reference_ensure).
     */

    printf("Emitting gamevals from the pack files\n");
    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        const struct CP_Type* type = cp_type(t);

        if( type->gameval_archive < 0 )
            continue;
        /*
         * Archive 10 is nested for the same reason archive 14 is: a dbtable's record
         * carries the table's name *and* every one of its column names, keyed (see
         * `keyed_gameval_name`). Writing the pack's single name flat replaces that
         * with one string and drops ~3,000 column names — the only place in the
         * cache they exist. Refused rather than half-written, exactly as 14 is below.
         *
         * This was reachable: `dbtable` is a CP_Type with a gameval archive, so
         * `pack --gamevals` wrote it, and what it wrote was the mangled flat name
         * truncated at 255 characters.
         */
        if( type->gameval_archive == 10 )
        {
            printf("  %-11s skipped — archive 10 is keyed (table name + column names) and "
                   "this writes flat records only\n",
                   type->name);
            continue;
        }
        archives += emit_gameval_archive(ctx, out_cache_dir, table_id, type->gameval_archive,
                                         type->name, &ctx->names.packs[t], &dirty);
    }
    for( int a = 0; a < CP_ASSET_COUNT; a++ )
    {
        const struct CP_Asset* asset = cp_asset(a);

        if( asset->gameval_archive < 0 )
            continue;
        /*
         * Archive 14 names interfaces *and* their components in one nested
         * record. Writing the interface names flat would drop 26,491 component
         * names that nothing else in the cache carries, so it is refused rather
         * than half-written — the one archive this cannot regenerate.
         */
        if( asset->gameval_archive == 14 )
        {
            printf("  %-11s skipped — archive 14 is nested (interface + components) and this "
                   "writes flat records only\n",
                   asset->pack);
            continue;
        }
        archives += emit_gameval_archive(ctx, out_cache_dir, table_id, asset->gameval_archive,
                                         asset->pack, &ctx->names.asset_packs[a], &dirty);
    }

    if( dirty && !cp_reference_write(ctx, out_cache_dir, table_id) )
    {
        fprintf(stderr, "cachepack: the gameval reference table failed to write\n");
        return 0;
    }
    printf("Gamevals: %d archive(s) written\n", archives);
    return 1;
}
