#ifndef PLATFORM_EMSCRIPTEN_JSHOST_H
#define PLATFORM_EMSCRIPTEN_JSHOST_H

#include "../libtorirs.h"
#include "../scripting/libtorirs_scripting.h"

#include <emscripten.h>

struct LibToriRS_Instance;
struct CacheDatArchive;
struct CacheArchive;

EMSCRIPTEN_KEEPALIVE
char*
LibToriPlatformEmscripten_JSHost_ScriptGetName(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptGetNameLength(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptGetIsInline(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptArgs*
LibToriPlatformEmscripten_JSHost_ScriptGetArgs(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptFree(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_ScriptQueue*
LibToriPlatformEmscripten_JSHost_GetScriptQueue(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Instance*
LibToriPlatformEmscripten_JSHost_GetInstancePtr(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_Script*
LibToriPlatformEmscripten_JSHost_ScriptQueuePop(struct LibToriRS_ScriptQueue* queue);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueue*
LibToriPlatformEmscripten_JSHost_LuaHost_GetIOQueue(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueueItem*
LibToriPlatformEmscripten_JSHost_IOQueuePop(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetArchiveId(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetTableId(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetFlags(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptQueueIsEmpty(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IOQueueGetCount(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
struct LibToriRS_IOQueueItem*
LibToriPlatformEmscripten_JSHost_IOQueueGetItemByIndex(
    struct LibToriRS_Instance* instance,
    int index);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_IOQueueItemResolve(
    struct LibToriRS_IOQueueItem* item,
    void* data_ptr);

EMSCRIPTEN_KEEPALIVE
void*
LibToriPlatformEmscripten_JSHost_Malloc(int size);

EMSCRIPTEN_KEEPALIVE
struct CacheDatArchive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveNewFromBuffer(
    int table_id,
    int archive_id,
    void* data_ptr,
    int data_size);

EMSCRIPTEN_KEEPALIVE
struct CacheArchive*
LibToriPlatformEmscripten_JSHost_CacheArchiveDeserialize(
    void* buffer,
    int size);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheArchiveFree(struct CacheArchive* archive);

EMSCRIPTEN_KEEPALIVE
struct CacheDatArchive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveDeserialize(
    void* buffer,
    int size);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheDatArchiveFree(struct CacheDatArchive* archive);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_Init(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModel(
    struct LibToriRS_Instance* instance,
    int model_id);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetch(
    struct LibToriRS_Instance* instance,
    int model_id,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModel(
    struct LibToriRS_Instance* instance,
    int model_id);
#endif