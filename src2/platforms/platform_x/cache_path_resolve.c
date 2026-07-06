#include "cache_path_resolve.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool
cache_path_has_dat2(char const* dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", dir);
    FILE* f = fopen(path, "rb");
    if( !f )
        return false;
    fclose(f);
    return true;
}

bool
cache_path_same_dir(char const* a, char const* b)
{
    if( !a || !b )
        return false;
    if( strcmp(a, b) == 0 )
        return true;

    char ra[512];
    char rb[512];
    char const* ca = realpath(a, ra);
    char const* cb = realpath(b, rb);
    if( ca && cb )
        return strcmp(ca, cb) == 0;
    return false;
}

static bool
is_3draster_repo_root(char const* dir)
{
    char path[512];
    struct stat st;

    snprintf(path, sizeof(path), "%s/src2", dir);
    if( stat(path, &st) != 0 || !S_ISDIR(st.st_mode) )
        return false;

    return true;
}

static char const*
cache_path_resolve_repo_subdir(char const* subdir)
{
    static char resolved[512];
    char probe[512];

    if( !getcwd(probe, sizeof(probe)) )
        return NULL;

    for( int depth = 0; depth < 12; depth++ )
    {
        if( is_3draster_repo_root(probe) )
        {
            snprintf(resolved, sizeof(resolved), "%s/%s", probe, subdir);
            if( cache_path_has_dat2(resolved) )
                return resolved;
            return NULL;
        }

        char* slash = strrchr(probe, '/');
        if( !slash || slash == probe )
            break;
        *slash = '\0';
    }

    return NULL;
}

char const*
cache_path_resolve_kronos_repo(void)
{
    return cache_path_resolve_repo_subdir("cache.kronos");
}

char const*
cache_path_resolve_osrs_repo(void)
{
    return cache_path_resolve_repo_subdir("cache");
}

char const*
cache_path_resolve_xrsps_osrs237(void)
{
    static char resolved[512];
    char probe[512];

    if( !getcwd(probe, sizeof(probe)) )
        return NULL;

    for( int depth = 0; depth < 12; depth++ )
    {
        if( is_3draster_repo_root(probe) )
        {
            snprintf(
                resolved,
                sizeof(resolved),
                "%s/../xrsps-typescript/caches/osrs-237_2026-03-25",
                probe);
            if( cache_path_has_dat2(resolved) )
                return resolved;
            return NULL;
        }

        char* slash = strrchr(probe, '/');
        if( !slash || slash == probe )
            break;
        *slash = '\0';
    }

    return NULL;
}
