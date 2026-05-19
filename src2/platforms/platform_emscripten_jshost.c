#include "platform_emscripten_jshost.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "../scripting/libtorirs_scriptapi.h"
#include "platform_x/cachelib_serialized.h"
#include "src/osrs/rscache/archive.h"
#include "src/osrs/rscache/cache.h"
#include "src/osrs/rscache/cache_dat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EMSCRIPTEN_KEEPALIVE
double
LibToriPlatformEmscripten_JSHost_ScriptValueAsJSNumber(struct LibToriRS_ScriptValue* value)
{
    if( !value )
        return 0.0;
    switch( value->kind )
    {
    case LIBTORIRS_SCRIPT_VALUE_INT:
        return (double)value->u.intval.value;
    case LIBTORIRS_SCRIPT_VALUE_ANY:
        return (double)(uintptr_t)value->u.anyval.value;
    case LIBTORIRS_SCRIPT_VALUE_VOID:
    default:
        return 0.0;
    }
}

EMSCRIPTEN_KEEPALIVE
char*
LibToriPlatformEmscripten_JSHost_ScriptValueAsJSString(struct LibToriRS_ScriptValue* value)
{
    if( !value )
        return NULL;
    if( value->kind != LIBTORIRS_SCRIPT_VALUE_ANY )
        return NULL;
    if( !value->u.anyval.value )
        return NULL;
    return (char*)value->u.anyval.value;
}

EMSCRIPTEN_KEEPALIVE
char*
LibToriPlatformEmscripten_JSHost_ScriptGetName(struct LibToriRS_Script* script)
{
    printf("ToriPlatformEmscripten_JSHost_ScriptGetName: %s\n", script->name);
    if( !script )
        return NULL;
    return script->name;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptGetNameLength(struct LibToriRS_Script* script)
{
    if( !script )
        return 0;
    return strlen(script->name);
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptGetIsInline(struct LibToriRS_Script* script)
{
    if( !script )
        return 0;
    return script->is_inline ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptArgs*
LibToriPlatformEmscripten_JSHost_ScriptGetArgs(struct LibToriRS_Script* script)
{
    return LibToriRS_ScriptGetArgs(script);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptFree(struct LibToriRS_Script* script)
{
    struct LibToriRS_ScriptArgs* args = LibToriRS_ScriptGetArgs(script);
    if( !args )
        return;
    LibToriRS_ScriptArgsReset(args);
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptQueue*
LibToriPlatformEmscripten_JSHost_GetScriptQueue(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetScriptQueue(instance);
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Instance*
LibToriPlatformEmscripten_JSHost_GetInstancePtr(struct LibToriRS_Instance* instance)
{
    return instance;
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Script*
LibToriPlatformEmscripten_JSHost_ScriptQueuePop(struct LibToriRS_ScriptQueue* queue)
{
    return LibToriRS_ScriptQueuePop(queue);
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueue*
LibToriPlatformEmscripten_JSHost_LuaHost_GetIOQueue(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetIOQueue(instance);
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueueItem*
LibToriPlatformEmscripten_JSHost_IOQueuePop(struct LibToriRS_Instance* instance)
{
    return LibToriRS_IOQueuePopReadPtr(LibToriRS_GetIOQueue(instance));
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetArchiveId(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item)
{
    if( !instance || !item )
        return 0;
    return item->archive_id;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetTableId(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item)
{
    if( !instance || !item )
        return 0;
    return item->table_id;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetFlags(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item)
{
    if( !instance || !item )
        return 0;
    return item->flags;
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptQueueIsEmpty(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return true;
    return LibToriRS_ScriptQueueIsEmpty(LibToriRS_GetScriptQueue(instance));
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileFetch(struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileLoad(struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(instance);
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IOQueueGetCount(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return 0;
    struct LibToriRS_IOQueue* io_queue = LibToriRS_GetIOQueue(instance);
    if( !io_queue )
        return 0;
    return io_queue->count;
}

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueueItem*
LibToriPlatformEmscripten_JSHost_IOQueueGetItemByIndex(
    struct LibToriRS_Instance* instance,
    int index)
{
    if( !instance )
        return NULL;
    struct LibToriRS_IOQueue* io_queue = LibToriRS_GetIOQueue(instance);
    if( !io_queue )
        return NULL;
    if( index < 0 || index >= io_queue->count )
        return NULL;
    return &io_queue->items[index];
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_IOQueueItemResolve(
    struct LibToriRS_IOQueueItem* item,
    void* data_ptr)
{
    if( !item )
        return;
    item->data = data_ptr;
    item->is_resolved = true;
}

EMSCRIPTEN_KEEPALIVE
void*
LibToriPlatformEmscripten_JSHost_Malloc(int size)
{
    if( size <= 0 )
        return NULL;
    return malloc(size);
}

EMSCRIPTEN_KEEPALIVE
struct CacheDatArchive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveNewFromBuffer(
    int table_id,
    int archive_id,
    void* data_ptr,
    int data_size)
{
    if( !data_ptr || data_size <= 0 )
        return NULL;

    struct CacheDatArchive* archive = malloc(sizeof(struct CacheDatArchive));
    if( !archive )
        return NULL;

    memset(archive, 0, sizeof(struct CacheDatArchive));
    archive->data = (char*)data_ptr;
    archive->data_size = data_size;
    archive->table_id = table_id;
    archive->archive_id = archive_id;
    archive->file_count = 0;
    archive->format = ARCHIVE_FORMAT_DAT_MULTIFILE;

    return archive;
}

EMSCRIPTEN_KEEPALIVE
struct CacheArchive*
LibToriPlatformEmscripten_JSHost_CacheArchiveDeserialize(
    void* buffer,
    int size)
{
    return cachelib_cache_archive_deserialize(buffer, size);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheArchiveFree(
    struct CacheArchive* archive)
{
    cachelib_cache_archive_free(archive);
}

EMSCRIPTEN_KEEPALIVE
struct CacheDatArchive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveDeserialize(
    void* buffer,
    int size)
{
    return cachelib_cache_dat_archive_deserialize(buffer, size);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheDatArchiveFree(
    struct CacheDatArchive* archive)
{
    cachelib_cache_dat_archive_free(archive);
}
