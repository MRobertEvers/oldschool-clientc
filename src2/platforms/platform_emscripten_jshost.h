#ifndef PLATFORM_EMSCRIPTEN_JSHOST_H
#define PLATFORM_EMSCRIPTEN_JSHOST_H

#include "../libtorirs.h"
#include "../scripting/libtorirs_scripting.h"

#include <emscripten.h>

struct LibToriRS_Instance;
struct RSCacheDat1Disk_Archive;
struct RSCacheDat2Disk_Archive;
struct GameHandle;

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
int
LibToriPlatformEmscripten_JSHost_ScriptGetArgCount(struct LibToriRS_Script* script);

EMSCRIPTEN_KEEPALIVE
void*
LibToriPlatformEmscripten_JSHost_ScriptGetArg(
    struct LibToriRS_Script* script,
    int index);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_ScriptValueAsInt(struct LibToriRS_ScriptValue* scriptValue);

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
int
LibToriPlatformEmscripten_JSHost_IORequestGetKind(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

EMSCRIPTEN_KEEPALIVE
int
LibToriPlatformEmscripten_JSHost_IORequestGetStatus(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueueItem* item);

EMSCRIPTEN_KEEPALIVE
const char*
LibToriPlatformEmscripten_JSHost_IORequestGetPath(
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
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ConfigFileLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesLoad(
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
void
LibToriPlatformEmscripten_JSHost_IOQueueItemResolveWithSize(
    struct LibToriRS_IOQueueItem* item,
    void* data_ptr,
    int data_size);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_IOQueueItemError(
    struct LibToriRS_IOQueueItem* item,
    int error_code);

EMSCRIPTEN_KEEPALIVE
void*
LibToriPlatformEmscripten_JSHost_Malloc(int size);

EMSCRIPTEN_KEEPALIVE
struct RSCacheDat1Disk_Archive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveNewFromBuffer(
    int table_id,
    int archive_id,
    void* data_ptr,
    int data_size);

EMSCRIPTEN_KEEPALIVE
struct RSCacheDat2Disk_Archive*
LibToriPlatformEmscripten_JSHost_CacheArchiveDeserialize(
    void* buffer,
    int size);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheArchiveFree(struct RSCacheDat2Disk_Archive* archive);

EMSCRIPTEN_KEEPALIVE
struct RSCacheDat1Disk_Archive*
LibToriPlatformEmscripten_JSHost_CacheDatArchiveDeserialize(
    void* buffer,
    int size);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_CacheDatArchiveFree(struct RSCacheDat1Disk_Archive* archive);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_Init(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_Init(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_BuildWorld(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_BuildWorldCenterzoneNativeInt(
    struct LibToriRS_Instance* instance,
    int center_x,
    int center_z,
    int scene_size);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_Runescape_BuildWorldChunkListNativeInt(
    struct LibToriRS_Instance* instance,
    int* chunks_xz,
    int count);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModelNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_RenderModelScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id);

EMSCRIPTEN_KEEPALIVE
struct GameHandle*
LibToriPlatformEmscripten_JSHost_ScriptAPI_Game_ModelViewer_GetGameHandle(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_CoreTask_Dat1LoadModelNativeInt(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game,
    int model_id);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_RunTasks(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetchNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelFetchScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkTerrainFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkSceneryFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkTerrainLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_MapChunkSceneryLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModelNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitGameCacheModelScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelCleanupNativeInt(
    struct LibToriRS_Instance* instance,
    int model_id);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_ModelCleanupScriptInt(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_ScriptValue* model_id);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_TexturesCleanup(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitTextures(struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat2_TexturesLoad(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat2_SubmitTextures(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
const char*
LibToriPlatformEmscripten_JSHost_ScriptAPI_GetCacheMode(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_ModelsClearAll(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_VersionListFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_VersionListLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsFetchNativeInt(
    struct LibToriRS_Instance* instance,
    int archive_id,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
bool
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SequencesInitFromConfigJagfile(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitSequences(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitFloortypes(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitSceneryConfigs(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SubmitAnimations(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SequencesCleanup(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_FloortypesCleanup(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_SceneryConfigsCleanup(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_Dat1_AnimationsCleanup(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_SequencesClearAll(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_FloortypesClearAll(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_SceneryConfigsClearAll(
    struct LibToriRS_Instance* instance);

EMSCRIPTEN_KEEPALIVE
void
LibToriPlatformEmscripten_JSHost_ScriptAPI_GameCache_AnimationsClearAll(
    struct LibToriRS_Instance* instance);
#endif