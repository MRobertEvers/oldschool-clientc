/*
 * sscompile — RuneScript source to script.dat / script.idx.
 *
 *   sscompile --src DIR --out DIR [--pack DIR] [--constants DIR]
 *
 * --src        directory of .rs2 sources, searched recursively
 * --out        where script.dat and script.idx are written
 * --pack       directory of id=name .pack files (defaults to <src>/../pack)
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
            "usage: sscompile --src DIR --out DIR [--pack DIR] [--constants DIR]\n");
}

int
main(int argc, char** argv)
{
    const char* src = NULL;
    const char* out = NULL;
    const char* pack = NULL;
    const char* constants = NULL;
    char pack_default[1024];
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSC_Diag diag;
    int symbol_count = 0;
    int constant_count = 0;
    int i;
    int status = 0;

    for( i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--src") == 0 && i + 1 < argc )
            src = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out = argv[++i];
        else if( strcmp(argv[i], "--pack") == 0 && i + 1 < argc )
            pack = argv[++i];
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

    if( !pack )
    {
        snprintf(pack_default, sizeof(pack_default), "%s/../pack", src);
        pack = pack_default;
    }
    if( !constants )
        constants = src;

    SSC_SymbolsInit(&symbols);
    symbol_count = SSC_SymbolsLoadPackDir(&symbols, pack);
    constant_count = SSC_SymbolsLoadConstantDir(&symbols, constants);

    if( symbol_count < 0 )
    {
        fprintf(stderr, "sscompile: no symbol packs at %s\n", pack);
        SSC_SymbolsFree(&symbols);
        return 1;
    }
    printf("symbols: %d from packs, %d constants\n", symbol_count,
           constant_count < 0 ? 0 : constant_count);

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
