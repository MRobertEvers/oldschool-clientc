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
 */

static int
ensure_dir(const char* path)
{
    struct stat st;
    if( stat(path, &st) == 0 )
        return S_ISDIR(st.st_mode) ? 0 : -1;
    return cp_mkdir(path);
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

    char path[1200];
    snprintf(path, sizeof(path), "%s/configs/all.%s", ctx->srcdir, type->name);
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        fprintf(stderr, "cachepack: cannot write %s: %s\n", path, strerror(errno));
        cp_group_free(&group);
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

    struct CP_Lines lines;
    cp_lines_init(&lines);

    int written = 0;
    for( int i = 0; i < group.count; i++ )
    {
        int id = group.ids ? group.ids[i] : i;
        int size = 0;
        const uint8_t* record = cp_group_record(&group, i, &size);
        if( !record )
            continue;

        cp_lines_clear(&lines);
        if( !type->unpack(ctx, id, record, size, &lines) )
        {
            fprintf(stderr, "cachepack: %s %d failed to unpack\n", type->name, id);
            continue;
        }
        reorder(&lines);
        cp_lines_write(&lines, cp_name_ensure(ctx, type_id, id), out);
        written++;
    }

    cp_lines_free(&lines);
    fclose(out);
    cp_group_free(&group);
    printf("  %-11s %6d records -> configs/all.%s\n", type->name, written, type->name);
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
    return 1;
}
