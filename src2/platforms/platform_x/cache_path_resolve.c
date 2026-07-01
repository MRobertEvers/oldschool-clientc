#include "cache_path_resolve.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool
dir_has_dat2_cache(char const* dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", dir);
    FILE* f = fopen(path, "rb");
    if( !f )
        return false;
    fclose(f);
    return true;
}

static bool
is_3draster_repo_root(char const* dir)
{
    char path[512];
    struct stat st;

    snprintf(path, sizeof(path), "%s/src2", dir);
    if( stat(path, &st) != 0 || !S_ISDIR(st.st_mode) )
        return false;

    snprintf(path, sizeof(path), "%s/cache.kronos", dir);
    if( stat(path, &st) != 0 || !S_ISDIR(st.st_mode) )
        return false;

    return dir_has_dat2_cache(path);
}

char const*
cache_path_resolve_kronos_repo(void)
{
    static char resolved[512];
    char probe[512];

    if( !getcwd(probe, sizeof(probe)) )
        return NULL;

    for( int depth = 0; depth < 12; depth++ )
    {
        if( is_3draster_repo_root(probe) )
        {
            snprintf(resolved, sizeof(resolved), "%s/cache.kronos", probe);
            return resolved;
        }

        char* slash = strrchr(probe, '/');
        if( !slash || slash == probe )
            break;
        *slash = '\0';
    }

    return NULL;
}
