#include "luac_sidecar_command_dispatch.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static int
ensure_blobs_dir(void)
{
#if defined(_WIN32)
    if( _mkdir("..\\blobs") == 0 )
        return 0;
    return (errno == EEXIST) ? 0 : -1;
#else
    if( mkdir("../blobs", 0755) == 0 )
        return 0;
    return (errno == EEXIST) ? 0 : -1;
#endif
}

int
LuaCSidecar_BlobStore(
    const char* name_utf8,
    const void* data,
    size_t size)
{
    if( !name_utf8 || (size > 0 && !data) )
        return -1;

    if( ensure_blobs_dir() != 0 )
        return -1;

    char path[256];
    if( snprintf(path, sizeof(path), "../blobs/%s.bin", name_utf8) >= (int)sizeof(path) )
        return -1;

    FILE* fp = fopen(path, "wb");
    if( !fp )
        return -1;
    if( size > 0 && fwrite(data, 1, size, fp) != size )
    {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

int
LuaCSidecar_BlobRead(
    const char* name_utf8,
    void** data_out,
    int* size_out)
{
    *data_out = NULL;
    *size_out = 0;
    if( !name_utf8 )
        return -1;

    char path[256];
    if( snprintf(path, sizeof(path), "../blobs/%s.bin", name_utf8) >= (int)sizeof(path) )
        return -1;

    FILE* fp = fopen(path, "rb");
    if( !fp )
        return -1;

    if( fseek(fp, 0, SEEK_END) != 0 )
    {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if( sz < 0 )
    {
        fclose(fp);
        return -1;
    }
    if( fseek(fp, 0, SEEK_SET) != 0 )
    {
        fclose(fp);
        return -1;
    }

    void* buf = NULL;
    if( sz > 0 )
    {
        buf = malloc((size_t)sz);
        if( !buf )
        {
            fclose(fp);
            return -1;
        }
        if( fread(buf, 1, (size_t)sz, fp) != (size_t)sz )
        {
            free(buf);
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);

    *data_out = buf;
    *size_out = (int)sz;
    return 0;
}
