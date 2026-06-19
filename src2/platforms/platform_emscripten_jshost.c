#include "platform_emscripten_jshost.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "../scripting/libtorirs_scriptapi.h"
#include "platform_x/cachelib_serialized.h"
#include "osrs/rscache/shared/rscache_shared_archive.h"
#include "osrs/rscache/dat2disk/rscache_dat2disk.h"
#include "osrs/rscache/dat1disk/rscache_dat1disk.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
int
LibToriPlatformEmscripten_JSHost_ScriptGetArgCount(struct LibToriRS_Script* script)
{
    if( !script )
        return 0;
    return script->args.count;
}
EMSCRIPTEN_KEEPALIVE
void*
LibToriPlatformEmscripten_JSHost_ScriptGetArg(
    struct LibToriRS_Script* script,
    int index)
{
    if( !script )
        return NULL;
    return &script->args.values[index];
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptValueAsInt(struct LibToriRS_ScriptValue* scriptValue)
{
    assert(scriptValue);
    assert(scriptValue->kind == LIBTORIRS_SCRIPT_VALUE_INT);
    return scriptValue->u.intval.value;
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
    return item->u.cache.archive_id;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetTableId(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item)
{
    if( !instance || !item )
        return 0;
    return item->u.cache.table_id;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetFlags(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item)
{
    if( !instance || !item )
        return 0;
    return item->u.cache.flags;
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
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    return LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    LibToriRS_ScriptAPI_Dat1_TexturesFetch(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    return LibToriRS_ScriptAPI_Dat1_TexturesLoad(instance, io_queue);
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
    item->error_code = 0;
    item->status = TORIRSIO_STAT_DONE;
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_IOQueueItemError(
    struct LibToriRS_IOQueueItem* item,
    int error_code)
{
    if( !item )
        return;
    item->status = TORIRSIO_STAT_DONE;
    item->error_code = error_code;
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
struct RSCacheDat1Disk_Archive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveNewFromBuffer(
    int table_id,
    int archive_id,
    void* data_ptr,
    int data_size)
{
    if( !data_ptr || data_size <= 0 )
        return NULL;

    struct RSCacheDat1Disk_Archive* archive = malloc(sizeof(struct RSCacheDat1Disk_Archive));
    if( !archive )
        return NULL;

    memset(archive, 0, sizeof(struct RSCacheDat1Disk_Archive));
    archive->data = (char*)data_ptr;
    archive->data_size = data_size;
    archive->table_id = table_id;
    archive->archive_id = archive_id;
    archive->file_count = 0;
    archive->format = RSCacheShared_ArchiveFormat_DatMultifile;

    return archive;
}

EMSCRIPTEN_KEEPALIVE
struct RSCacheDat2Disk_Archive*
LibToriPlatformEmscripten_JSHost_CacheArchiveDeserialize(
    void* buffer,
    int size)
{
    return cachelib_cache_archive_deserialize(buffer, size);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheArchiveFree(struct RSCacheDat2Disk_Archive* archive)
{
    cachelib_RSCacheDat2Disk_ArchiveFree(archive);
}

EMSCRIPTEN_KEEPALIVE
struct RSCacheDat1Disk_Archive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveDeserialize(
    void* buffer,
    int size)
{
    return cachelib_cache_dat_archive_deserialize(buffer, size);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheDatArchiveFree(struct RSCacheDat1Disk_Archive* archive)
{
    cachelib_RSCacheDat1Disk_ArchiveFree(archive);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetchNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id,
    struct LibToriRS_IOQueue* io_queue)
{
    LibToriRS_ScriptAPI_Dat1_ModelFetch(instance, model_id, io_queue);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetchScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id,
    struct LibToriRS_IOQueue* io_queue)
{
    assert(model_id && model_id->kind == LIBTORIRS_SCRIPT_VALUE_INT);
    int model_int = model_id->u.intval.value;
    LibToriRS_ScriptAPI_Dat1_ModelFetch(instance, model_int, io_queue);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    return LibToriRS_ScriptAPI_Dat1_ModelLoad(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkTerrainFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue)
{
    LibToriRS_ScriptAPI_Dat1_MapChunkTerrainFetch(instance, mapx, mapz, io_queue);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkSceneryFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue)
{
    LibToriRS_ScriptAPI_Dat1_MapChunkSceneryFetch(instance, mapx, mapz, io_queue);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkTerrainLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    return LibToriRS_ScriptAPI_Dat1_MapChunkTerrainLoad(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkSceneryLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    return LibToriRS_ScriptAPI_Dat1_MapChunkSceneryLoad(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_Init(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Game_ModelViewer_Init(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_Init(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Game_Runescape_Init(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_BuildWorld(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Game_Runescape_BuildWorld(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModelNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel(instance, model_id);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModelScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id)
{
    assert(model_id && model_id->kind == LIBTORIRS_SCRIPT_VALUE_INT);
    int model_int = model_id->u.intval.value;
    LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel(instance, model_int);
}

EMSCRIPTEN_KEEPALIVE
struct GameHandle*
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_GetGameHandle(
    struct LibToriRS_Instance* instance)
{
    return LibToriRS_ScriptAPI_Game_ModelViewer_GetGameHandle(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_CoreTask_Dat1LoadModelNativeInt(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game,
    int model_id)
{
    LibToriRS_ScriptAPI_CoreTask_Dat1LoadModel(instance, game, model_id);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_RunTasks(
    struct LibToriRS_Instance* instance)
{
    return LibToriRS_ScriptAPI_RunTasks(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModelNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(instance, model_id);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModelScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id)
{
    assert(model_id && model_id->kind == LIBTORIRS_SCRIPT_VALUE_INT);
    int model_int = model_id->u.intval.value;
    LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(instance, model_int);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelCleanupNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    LibToriRS_ScriptAPI_Dat1_ModelCleanup(instance, model_id);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelCleanupScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id)
{
    assert(model_id && model_id->kind == LIBTORIRS_SCRIPT_VALUE_INT);
    int model_int = model_id->u.intval.value;
    LibToriRS_ScriptAPI_Dat1_ModelCleanup(instance, model_int);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesCleanup(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_TexturesCleanup(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitTextures(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SubmitTextures(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_ModelsClearAll(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_GameCache_ModelsClearAll(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_VersionListFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    LibToriRS_ScriptAPI_Dat1_VersionListFetch(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_VersionListLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    return LibToriRS_ScriptAPI_Dat1_VersionListLoad(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsFetchNativeInt(
    struct LibToriRS_Instance* instance,
    int archive_id,
    struct LibToriRS_IOQueue* io_queue)
{
    LibToriRS_ScriptAPI_Dat1_AnimationsFetch(instance, archive_id, io_queue);
}

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    return LibToriRS_ScriptAPI_Dat1_AnimationsLoad(instance, io_queue);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SequencesInitFromConfigJagfile(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SequencesInitFromConfigJagfile(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitSequences(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SubmitSequences(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitFloortypes(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SubmitFloortypes(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitSceneryConfigs(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SubmitSceneryConfigs(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitAnimations(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SubmitAnimations(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SequencesCleanup(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SequencesCleanup(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_FloortypesCleanup(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_FloortypesCleanup(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SceneryConfigsCleanup(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_SceneryConfigsCleanup(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsCleanup(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_Dat1_AnimationsCleanup(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_SequencesClearAll(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_GameCache_SequencesClearAll(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_FloortypesClearAll(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_GameCache_FloortypesClearAll(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_SceneryConfigsClearAll(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_GameCache_SceneryConfigsClearAll(instance);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_AnimationsClearAll(
    struct LibToriRS_Instance* instance)
{
    LibToriRS_ScriptAPI_GameCache_AnimationsClearAll(instance);
}