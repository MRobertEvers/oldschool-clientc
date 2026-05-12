#include "platforms/browser2/luajs_cacheio.h"

#include <stdlib.h>

EMSCRIPTEN_KEEPALIVE
int32_t*
luajs_LuaCacheIO_flatten_dat1_triplets(
    struct LuaCacheIORequestList* list,
    int* out_triplet_count)
{
    if( out_triplet_count )
        *out_triplet_count = 0;
    if( !list || list->count <= 0 )
        return NULL;

    int n = 0;
    for( int i = 0; i < list->count; i++ )
    {
        if( list->items[i].format == LUA_CACHEIO_FORMAT_DAT1 )
            n++;
    }
    if( n <= 0 )
        return NULL;

    int32_t* buf = (int32_t*)malloc((size_t)n * 3 * sizeof(int32_t));
    if( !buf )
        return NULL;

    int w = 0;
    for( int i = 0; i < list->count; i++ )
    {
        struct LuaCacheIORequest* r = &list->items[i];
        if( r->format != LUA_CACHEIO_FORMAT_DAT1 )
            continue;
        buf[w++] = (int32_t)r->table_id;
        buf[w++] = (int32_t)r->archive_id;
        buf[w++] = (int32_t)r->flags;
    }
    if( out_triplet_count )
        *out_triplet_count = n;
    return buf;
}
