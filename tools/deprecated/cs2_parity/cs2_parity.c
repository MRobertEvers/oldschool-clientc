#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "parity_exec.h"
#include "parity_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char const*
default_manifest_path(void)
{
    return "manifest.json";
}

static int
parse_clientscript_trailer_flag(char const* value)
{
    if( !value )
        return CLIENTSCRIPT_DECODE_TRAILER_MODERN;
    if( strcmp(value, "legacy") == 0 )
        return CLIENTSCRIPT_DECODE_TRAILER_LEGACY;
    return CLIENTSCRIPT_DECODE_TRAILER_MODERN;
}

static void
usage(char const* argv0)
{
    fprintf(
        stderr,
        "Usage: %s [--cs2-trailer modern|legacy] <cache_dir> exec <caseId> <out.json>\n"
        "       %s [--cs2-trailer modern|legacy] <cache_dir> sprite <caseId> <out_dir>\n"
        "       %s [--cs2-trailer modern|legacy] <cache_dir> tree <caseId> <out.json>\n"
        "       %s [--cs2-trailer modern|legacy] <cache_dir> all <out_dir>\n",
        argv0,
        argv0,
        argv0,
        argv0);
}

int
main(
    int argc,
    char** argv)
{
    int argi = 1;
    int clientscript_decode_flags = CLIENTSCRIPT_DECODE_TRAILER_MODERN;

    while( argi < argc && strcmp(argv[argi], "--cs2-trailer") == 0 )
    {
        if( argi + 1 >= argc )
        {
            usage(argv[0]);
            return 1;
        }
        clientscript_decode_flags = parse_clientscript_trailer_flag(argv[argi + 1]);
        argi += 2;
    }

    if( argc - argi < 3 )
    {
        usage(argv[0]);
        return 1;
    }

    parity_set_clientscript_decode_flags(clientscript_decode_flags);

    char const* cache_dir = argv[argi++];
    char const* cmd = argv[argi++];
    char const* manifest_path = default_manifest_path();

    if( parity_manifest_load(manifest_path) != 0 )
    {
        fprintf(stderr, "failed to load manifest %s\n", manifest_path);
        return 1;
    }

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache %s\n", cache_dir);
        return 1;
    }

    int rc = 0;

    if( strcmp(cmd, "exec") == 0 )
    {
        if( argc - argi < 2 )
        {
            usage(argv[0]);
            rc = 1;
        }
        else
        {
            struct ParityCs2Case const* cs_case = parity_manifest_find_cs2(argv[argi]);
            if( !cs_case )
            {
                fprintf(stderr, "unknown cs2 case %s\n", argv[argi]);
                rc = 1;
            }
            else if( parity_exec_case(cache, cs_case, argv[argi + 1]) != 0 )
            {
                fprintf(stderr, "exec failed for %s\n", argv[argi]);
                rc = 1;
            }
        }
    }
    else if( strcmp(cmd, "sprite") == 0 )
    {
        if( argc - argi < 2 )
        {
            usage(argv[0]);
            rc = 1;
        }
        else
        {
            struct ParitySpriteCase const* sp_case = parity_manifest_find_sprite(argv[argi]);
            if( !sp_case )
            {
                fprintf(stderr, "unknown sprite case %s\n", argv[argi]);
                rc = 1;
            }
            else if( parity_sprite_case(cache, sp_case, argv[argi + 1]) != 0 )
            {
                fprintf(stderr, "sprite failed for %s\n", argv[argi]);
                rc = 1;
            }
        }
    }
    else if( strcmp(cmd, "tree") == 0 )
    {
        if( argc - argi < 2 )
        {
            usage(argv[0]);
            rc = 1;
        }
        else
        {
            struct ParityTreeCase const* tree_case = parity_manifest_find_tree(argv[argi]);
            if( !tree_case )
            {
                fprintf(stderr, "unknown tree case %s\n", argv[argi]);
                rc = 1;
            }
            else if( parity_tree_case(cache, tree_case, argv[argi + 1]) != 0 )
            {
                fprintf(stderr, "tree failed for %s\n", argv[argi]);
                rc = 1;
            }
        }
    }
    else if( strcmp(cmd, "all") == 0 )
    {
        char const* out_dir = argv[argi];
        for( int i = 0; i < parity_manifest_cs2_count(); i++ )
        {
            struct ParityCs2Case const* cs_case = parity_manifest_cs2_at(i);
            char path[512];
            snprintf(path, sizeof(path), "%s/cs2_%s.json", out_dir, cs_case->id);
            if( parity_exec_case(cache, cs_case, path) != 0 )
                rc = 1;
        }
        for( int i = 0; i < parity_manifest_sprite_count(); i++ )
        {
            if( parity_sprite_case(cache, parity_manifest_sprite_at(i), out_dir) != 0 )
                rc = 1;
        }
        for( int i = 0; i < parity_manifest_tree_count(); i++ )
        {
            struct ParityTreeCase const* tree_case = parity_manifest_tree_at(i);
            char path[512];
            snprintf(path, sizeof(path), "%s/tree_%s.json", out_dir, tree_case->id);
            if( parity_tree_case(cache, tree_case, path) != 0 )
                rc = 1;
        }
    }
    else
    {
        usage(argv[0]);
        rc = 1;
    }

    RSCacheDat2Disk_Free(cache);
    return rc;
}
