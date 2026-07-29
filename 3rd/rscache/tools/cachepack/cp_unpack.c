#include "cachepack.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define cp_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define cp_mkdir(p) mkdir(p, 0755)
#endif

/*
 * The unpack driver — LostCity's engine/tools/unpack/config/Unpack.ts.
 *
 * The reference walks a fixed list of types, reads each one's `.idx`/`.dat` pair,
 * calls a per-type function that turns a record into lines, reorders those lines
 * for readability, and appends them to `all.<type>`. This does the same, with two
 * differences forced by the era: the records live in one archive per type rather
 * than in an idx/dat pair, and the names come from the cache instead of being
 * invented.
 *
 * Names are seeded for *every* type before *any* type is written. A loc names the
 * sequence it animates with, and that sequence's name has to already exist when
 * the loc is written — otherwise the loc would get `seq_1234` and the seq file,
 * written later, would call the same record `swarm_walk`, and the pack would then
 * hold both.
 *
 * ## Revision diffs (`--compare`)
 *
 * LostCity's Unpack.ts compares the new cache against the previous built pack and
 * writes only deltas under `scripts/_unpack/<rev>/`. Here the equivalent is:
 *
 *   - always rewrite `configs/all.<type>` from the *new* cache (tree matches bytes)
 *   - when `--compare` is set, also write under `configs/_unpack/<rev_name>/`:
 *       all.<type>         ids present only in the new cache
 *       all.<type>.merge   ids whose unpacked text differs (new block, then old)
 *       stale.<type>.pack  pack lines whose ids are gone from the new cache
 */

static int
ensure_dir(const char* path)
{
    struct stat st;
    if( stat(path, &st) == 0 )
        return S_ISDIR(st.st_mode) ? 0 : -1;
    return cp_mkdir(path);
}

/** mkdir -p for a path with at most a few components under srcdir. */
static int
ensure_dir_p(const char* path)
{
    char buf[1200];
    snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
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
 * Reorder a record's lines the way the reference does: identity first, then the
 * bulky repeated blocks, then everything else in source order.
 *
 * Purely for reading. `name` and `desc` are what someone scanning the file is
 * looking for, and a wall of forty `model` lines between them and the rest makes
 * the file unusable. The packer does not care about order.
 */
static void
reorder(struct CP_Lines* lines)
{
    char** head = NULL;
    char** models = NULL;
    char** recols = NULL;
    char** rest = NULL;
    int nhead = 0, nmodels = 0, nrecols = 0, nrest = 0;

    head = malloc((size_t)lines->count * sizeof(char*));
    models = malloc((size_t)lines->count * sizeof(char*));
    recols = malloc((size_t)lines->count * sizeof(char*));
    rest = malloc((size_t)lines->count * sizeof(char*));
    if( !head || !models || !recols || !rest )
    {
        free(head);
        free(models);
        free(recols);
        free(rest);
        return;
    }

    for( int i = 0; i < lines->count; i++ )
    {
        char* line = lines->lines[i];
        if( strncmp(line, "name=", 5) == 0 || strncmp(line, "desc=", 5) == 0 )
            head[nhead++] = line;
        else if( strncmp(line, "model", 5) == 0 || strncmp(line, "shape", 5) == 0 ||
                 strncmp(line, "head", 4) == 0 )
            models[nmodels++] = line;
        else if( strncmp(line, "recol", 5) == 0 || strncmp(line, "retex", 5) == 0 )
            recols[nrecols++] = line;
        else
            rest[nrest++] = line;
    }

    int w = 0;
    for( int i = 0; i < nhead; i++ )
        lines->lines[w++] = head[i];
    for( int i = 0; i < nmodels; i++ )
        lines->lines[w++] = models[i];
    for( int i = 0; i < nrecols; i++ )
        lines->lines[w++] = recols[i];
    for( int i = 0; i < nrest; i++ )
        lines->lines[w++] = rest[i];

    free(head);
    free(models);
    free(recols);
    free(rest);
}

static int
lines_equal(
    const struct CP_Lines* a,
    const struct CP_Lines* b)
{
    if( a->count != b->count )
        return 0;
    for( int i = 0; i < a->count; i++ )
    {
        if( strcmp(a->lines[i], b->lines[i]) != 0 )
            return 0;
    }
    return 1;
}

static int
unpack_record(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    int id,
    const uint8_t* record,
    int size,
    struct CP_Lines* lines)
{
    const struct CP_Type* type = cp_type(type_id);
    cp_lines_clear(lines);
    if( !type->unpack(ctx, id, record, size, lines) )
        return 0;
    reorder(lines);
    return 1;
}

static void
report_stale_names(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    const struct CP_Group* group,
    const char* unpack_dir)
{
    const struct CP_Type* type = cp_type(type_id);
    struct LC_Pack* pack = &ctx->names.packs[type_id];
    int stale = 0;
    int wrote = 0;
    FILE* out = NULL;
    char path[1400];

    for( int id = 0; id < pack->capacity; id++ )
    {
        if( !pack->names || !pack->names[id] || !pack->names[id][0] )
            continue;
        if( cp_group_find_id(group, id) >= 0 )
            continue;
        if( unpack_dir && !out )
        {
            snprintf(path, sizeof(path), "%s/stale.%s.pack", unpack_dir, type->name);
            out = fopen(path, "wb");
            if( out )
            {
                wrote = 1;
                fprintf(out,
                        "// Pack lines whose ids are absent from the new cache.\n"
                        "// Names are preserved (scripts may still refer to them);\n"
                        "// review and remove once nothing references them.\n\n");
            }
        }
        if( out )
            fprintf(out, "%d=%s\n", id, pack->names[id]);
        stale++;
    }
    if( out )
        fclose(out);
    if( !stale )
        return;
    if( wrote )
        printf("            %6d stale pack names -> configs/_unpack/%s/stale.%s.pack\n", stale,
               ctx->rev_name[0] ? ctx->rev_name : "?", type->name);
    else
        printf("            %6d stale pack names (ids gone from cache; names kept)\n", stale);
}

static int
unpack_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id)
{
    const struct CP_Type* type = cp_type(type_id);

    struct CP_Group group;
    if( !cp_group_open(ctx, type_id, &group) )
    {
        printf("  %-11s absent\n", type->name);
        return 1;
    }

    struct CP_Group compare_group;
    int have_compare = 0;
    memset(&compare_group, 0, sizeof(compare_group));
    if( ctx->compare_open )
    {
        have_compare = cp_group_open_disk(ctx, &ctx->compare, type_id, &compare_group);
        if( !have_compare )
            printf("  %-11s compare: absent in --compare cache\n", type->name);
    }

    char path[1400];
    snprintf(path, sizeof(path), "%s/configs/all.%s", ctx->srcdir, type->name);
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        fprintf(stderr, "cachepack: cannot write %s: %s\n", path, strerror(errno));
        cp_group_free(&group);
        if( have_compare )
            cp_group_free(&compare_group);
        return 0;
    }

    fprintf(out, "// Unpacked by cachepack from a %s cache.\n", type->name);
    if( type->flags & CP_TYPE_LOSSY )
        fprintf(out,
                "// This type is lossy: the decoder consumes fields it does not store, so a\n"
                "// repack is a valid record with less in it than the source had.\n");
    if( type->flags & CP_TYPE_NO_ENCODER )
        fprintf(out, "// Read-only: rscache has no encoder for this type, so `pack` skips it.\n");
    fputc('\n', out);

    FILE* new_out = NULL;
    FILE* merge_out = NULL;
    char unpack_dir[1200];
    unpack_dir[0] = '\0';
    if( have_compare && ctx->rev_name[0] )
    {
        snprintf(unpack_dir, sizeof(unpack_dir), "%s/configs/_unpack/%s", ctx->srcdir,
                 ctx->rev_name);
        if( ensure_dir_p(unpack_dir) != 0 )
        {
            fprintf(stderr, "cachepack: cannot create %s\n", unpack_dir);
            fclose(out);
            cp_group_free(&group);
            cp_group_free(&compare_group);
            return 0;
        }
    }

    struct CP_Lines lines;
    struct CP_Lines compare_lines;
    cp_lines_init(&lines);
    cp_lines_init(&compare_lines);

    int written = 0;
    int n_new = 0;
    int n_merge = 0;
    for( int i = 0; i < group.count; i++ )
    {
        int id = group.ids ? group.ids[i] : i;
        int size = 0;
        const uint8_t* record = cp_group_record(&group, i, &size);
        if( !record )
            continue;

        if( !unpack_record(ctx, type_id, id, record, size, &lines) )
        {
            fprintf(stderr, "cachepack: %s %d failed to unpack\n", type->name, id);
            continue;
        }
        const char* name = cp_name_ensure(ctx, type_id, id);
        cp_lines_write(&lines, name, out);
        written++;

        if( !have_compare )
            continue;

        int cmp_index = cp_group_find_id(&compare_group, id);
        if( cmp_index < 0 )
        {
            if( !new_out )
            {
                snprintf(path, sizeof(path), "%s/all.%s", unpack_dir, type->name);
                new_out = fopen(path, "wb");
                if( new_out )
                    fprintf(new_out,
                            "// Ids present in the new cache but not the --compare cache.\n\n");
            }
            if( new_out )
            {
                cp_lines_write(&lines, name, new_out);
                n_new++;
            }
            continue;
        }

        int cmp_size = 0;
        const uint8_t* cmp_record = cp_group_record(&compare_group, cmp_index, &cmp_size);
        if( !cmp_record ||
            !unpack_record(ctx, type_id, id, cmp_record, cmp_size, &compare_lines) )
            continue;

        if( lines_equal(&lines, &compare_lines) )
            continue;

        if( !merge_out )
        {
            snprintf(path, sizeof(path), "%s/all.%s.merge", unpack_dir, type->name);
            merge_out = fopen(path, "wb");
            if( merge_out )
                fprintf(merge_out,
                        "// Records whose unpacked text differs from the --compare cache.\n"
                        "// Each pair is: new block, then old block (LostCity Unpack.ts shape).\n\n");
        }
        if( merge_out )
        {
            fprintf(merge_out, "// --------\n");
            cp_lines_write(&lines, name, merge_out);
            cp_lines_write(&compare_lines, name, merge_out);
            n_merge++;
        }
    }

    cp_lines_free(&lines);
    cp_lines_free(&compare_lines);
    fclose(out);
    if( new_out )
        fclose(new_out);
    if( merge_out )
        fclose(merge_out);

    printf("  %-11s %6d records -> configs/all.%s\n", type->name, written, type->name);
    if( have_compare )
    {
        if( n_new )
            printf("            %6d new ids     -> configs/_unpack/%s/all.%s\n", n_new,
                   ctx->rev_name, type->name);
        if( n_merge )
            printf("            %6d changed     -> configs/_unpack/%s/all.%s.merge\n", n_merge,
                   ctx->rev_name, type->name);
        report_stale_names(ctx, type_id, &group, unpack_dir[0] ? unpack_dir : NULL);
        cp_group_free(&compare_group);
    }
    else
    {
        /* Without --compare there is still value in knowing pack lines that
         * point at ids the cache no longer carries. */
        report_stale_names(ctx, type_id, &group, NULL);
    }

    cp_group_free(&group);
    return 1;
}

static void
write_meta(struct CP_Ctx* ctx)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/meta.ini", ctx->srcdir);
    FILE* out = fopen(path, "wb");
    if( !out )
        return;
    /* So `pack` does not have to be told again which cache this came from —
     * packing with a different profile writes records the target client
     * misreads, and the profile is not recoverable from the text. */
    fprintf(out, "[cache]\n");
    fprintf(out, "game = %d\n", ctx->profile.game);
    fprintf(out, "epoch = %d\n", ctx->profile.epoch);
    fprintf(out, "revision = %d\n", ctx->profile.revision);
    fprintf(out, "quirks = %u\n", ctx->profile.quirks);
    if( ctx->rev_name[0] )
        fprintf(out, "rev_name = %s\n", ctx->rev_name);
    fclose(out);
}

int
cp_unpack_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel)
{
    char dir[1200];
    if( ensure_dir(ctx->srcdir) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create %s\n", ctx->srcdir);
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s/configs", ctx->srcdir);
    if( ensure_dir(dir) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create %s\n", dir);
        return 0;
    }

    printf("Seeding names from the cache's gameval table...\n");
    cp_names_seed_from_cache(ctx);

    int named = 0;
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        if( ctx->names.from_gameval[i] )
            named++;
    }
    printf("  %d of %d types carry content names; the rest use <type>_<id>\n", named,
           CP_TYPE_COUNT);
    printf("  existing pack names are kept (re-unpack never renames)\n");

    if( ctx->compare_open )
        printf("Comparing against previous cache; deltas -> configs/_unpack/%s/\n",
               ctx->rev_name[0] ? ctx->rev_name : "?");

    printf("Unpacking configs into %s\n", ctx->srcdir);
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        if( !sel->all && !(sel->mask & (1u << i)) )
            continue;
        if( !unpack_type(ctx, i) )
            return 0;
    }

    if( !cp_names_save(&ctx->names, ctx->srcdir) )
        return 0;
    write_meta(ctx);

    printf("Done. %d short decodes, %d unresolved names.\n", ctx->warn_short_decode,
           ctx->warn_unresolved_name);
    if( ctx->compare_open )
        printf("Review configs/_unpack/%s/ before absorbing server overlays for the new rev.\n",
               ctx->rev_name[0] ? ctx->rev_name : "?");
    return 1;
}
