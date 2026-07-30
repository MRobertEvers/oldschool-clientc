/*
 * sscompile — RuneScript source to script.dat / script.idx.
 *
 *   sscompile --src DIR --out DIR [--pack DIR]... [--constants DIR]
 *
 * --src        directory of .rs2 sources, searched recursively
 * --out        where script.dat and script.idx are written
 * --pack       directory of id=name .pack files (defaults to <src>/../pack).
 *              Repeatable, and later directories are loaded after earlier ones —
 *              a content tree splits its names between the ones the cache holds
 *              and the ones only the server has.
 * --constants  root to search for .constant files (defaults to <src>)
 *
 * Sources are compiled in sorted path order so script ids are stable across
 * machines. That matters more than it looks: a gosub is compiled to a script
 * *id*, so an unstable ordering silently repoints every call in the pack.
 */

#include "ssc.h"

#include <stdio.h>
#include <string.h>

static void
usage(void)
{
    fprintf(stderr,
            "usage: sscompile --src DIR --out DIR [--pack DIR]... [--constants DIR]\n");
}

int
main(int argc, char** argv)
{
    const char* src = NULL;
    const char* out = NULL;
    const char* packs[8];
    int pack_count = 0;
    const char* constants = NULL;
    char pack_default[1024];
    char configs_default[1024];
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSC_Diag diag;
    int symbol_count = 0;
    int component_count = 0;
    int constant_count = 0;
    int dbcolumn_count = 0;
    int i;
    int status = 0;

    for( i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--src") == 0 && i + 1 < argc )
            src = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out = argv[++i];
        else if( strcmp(argv[i], "--pack") == 0 && i + 1 < argc )
        {
            if( pack_count == (int)(sizeof(packs) / sizeof(packs[0])) )
            {
                fprintf(stderr, "sscompile: too many --pack directories\n");
                return 2;
            }
            packs[pack_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--constants") == 0 && i + 1 < argc )
            constants = argv[++i];
        else
        {
            usage();
            return 2;
        }
    }

    if( !src || !out )
    {
        usage();
        return 2;
    }

    if( pack_count == 0 )
    {
        /*
         * Both levels of index, because a tree keeps them in different places:
         * `pack/` names the archives of each cache index, `configs/` names the
         * records inside them. `--src` points at `server/scripts`, so the tree
         * root is two levels up.
         */
        snprintf(pack_default, sizeof(pack_default), "%s/../../pack", src);
        packs[pack_count++] = pack_default;
        snprintf(configs_default, sizeof(configs_default), "%s/../../configs", src);
        packs[pack_count++] = configs_default;
    }
    if( !constants )
        constants = src;

    SSC_SymbolsInit(&symbols);
    for( i = 0; i < pack_count; i++ )
    {
        int loaded = SSC_SymbolsLoadPackDir(&symbols, packs[i]);

        /*
         * A missing --pack directory warns rather than fails.
         *
         * It used to be fatal, which looked careful and was not: a content tree
         * splits its names across several directories and the *set* of them
         * changes as the tree is reorganised, so one that has not been created
         * yet stopped the build for every tree, including the ones that never
         * had it. Nothing can pass silently either way — a name that does not
         * resolve is still a hard compile error a few lines below, so the only
         * thing tolerating this loses is a worse diagnostic for a typo'd path.
         */
        if( loaded < 0 )
        {
            fprintf(stderr, "sscompile: no symbol packs at %s (skipping)\n", packs[i]);
            continue;
        }
        symbol_count += loaded;
    }
    /*
     * Components, after the packs because they compose against the interface ids
     * those load. There is no `pack/component.pack`: a component's name lives in
     * `interfaces/<name>.compack` and its id is `(interface << 16) | child`.
     *
     * The tree root is two levels above `--src`, which points at server/scripts.
     */
    {
        char content_root[1024];

        snprintf(content_root, sizeof(content_root), "%s/../..", src);
        component_count = SSC_SymbolsLoadComponentDir(&symbols, content_root);
    }
    constant_count = SSC_SymbolsLoadConstantDir(&symbols, constants);
    dbcolumn_count = SSC_SymbolsLoadDbTableDir(&symbols, constants);
    SSC_SymbolsSeedBuiltins(&symbols);

    printf("symbols: %d from packs, %d components, %d constants, %d db columns\n",
           symbol_count, component_count, constant_count < 0 ? 0 : constant_count,
           dbcolumn_count < 0 ? 0 : dbcolumn_count);

    compiler = SSC_New(&symbols);
    if( !compiler )
    {
        fprintf(stderr, "sscompile: out of memory\n");
        SSC_SymbolsFree(&symbols);
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if( !SSC_CompileDir(compiler, src, &diag) )
    {
        if( diag.file[0] )
            fprintf(stderr, "%s:%d: %s\n", diag.file, diag.line, diag.message);
        else
            fprintf(stderr, "sscompile: %s\n", diag.message);
        status = 1;
    }
    else if( !SSC_Write(compiler, out, &diag) )
    {
        fprintf(stderr, "sscompile: %s\n", diag.message);
        status = 1;
    }
    else
    {
        printf("compiled %d scripts to %s/script.dat\n", SSC_ScriptCount(compiler), out);
    }

    SSC_Free(compiler);
    SSC_SymbolsFree(&symbols);
    return status;
}
