#include "platform.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#include <fcntl.h>
#include <io.h>
#include <windows.h>

void
Plat_ListDirectory(const char* directory, void* context, Plat_DirEntryFn callback)
{
    char pattern[MAX_PATH * 2];
    WIN32_FIND_DATAA data;
    HANDLE handle;

    assert(directory);
    assert(callback);

    snprintf(pattern, sizeof(pattern), "%s\\*", directory);
    handle = FindFirstFileA(pattern, &data);
    if( handle == INVALID_HANDLE_VALUE )
        return;

    do
    {
        char path[MAX_PATH * 2];

        if( strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0 )
            continue;
        snprintf(path, sizeof(path), "%s\\%s", directory, data.cFileName);
        callback(context, path, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while( FindNextFileA(handle, &data) );

    FindClose(handle);
}

void
Plat_UseBinaryStdio(void)
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
}

char
Plat_PathSeparator(void)
{
    return '\\';
}

int
Plat_IsPathSeparator(char c)
{
    return c == '\\' || c == '/';
}

#else /* POSIX */

#include <dirent.h>
#include <sys/stat.h>

void
Plat_ListDirectory(const char* directory, void* context, Plat_DirEntryFn callback)
{
    DIR* handle;
    struct dirent* entry;

    assert(directory);
    assert(callback);

    handle = opendir(directory);
    if( !handle )
        return;

    while( (entry = readdir(handle)) != NULL )
    {
        char path[4096];
        struct stat info;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        /* d_type is not on every filesystem (it reports DT_UNKNOWN on some),
         * so the answer comes from stat rather than from the dirent. */
        if( stat(path, &info) != 0 )
            continue;
        callback(context, path, S_ISDIR(info.st_mode) ? 1 : 0);
    }
    closedir(handle);
}

void
Plat_UseBinaryStdio(void)
{
    /* POSIX has one mode. */
}

char
Plat_PathSeparator(void)
{
    return '/';
}

int
Plat_IsPathSeparator(char c)
{
    return c == '/';
}

#endif
