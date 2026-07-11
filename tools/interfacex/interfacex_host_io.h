#ifndef INTERFACEX_HOST_IO_H
#define INTERFACEX_HOST_IO_H

#include "toriauxlib/core/tasks/libtori_core_task_runner.h"

#include <stdbool.h>
#include <stdint.h>

struct pt;

struct CS2VM_HostRequest;
struct InterfaceX_VMHost;
struct LibToriPlatformX_IOReactor;
struct LibToriRS_IOQueue;
struct RSCacheDat2Disk;
struct RSCacheDat2DiskLib;
struct ToriAuxLibCache;
struct ToriAuxLibCore;
struct ToriDraw_Font;
struct ToriDraw_Scene;
struct ToriAuxLibCore_ClientScript;
struct UITreeXBuilder;

struct InterfaceX_HostIO
{
    struct RSCacheDat2DiskLib* cachelib;
    struct ToriAuxLibCache* aux_cache;
    struct ToriAuxLibCore* core;
    struct LibToriRS_IOQueue* io_queue;
    struct LibToriPlatformX_IOReactor* io_reactor;
    struct LibToriCoreTaskRunner* task_runner;
    struct ToriDraw_Scene* scene;
    int* next_scene_id;
};

bool
InterfaceX_HostIO_Init(
    struct InterfaceX_HostIO* io,
    struct ToriDraw_Scene* scene,
    int* next_scene_id,
    const char* cache_path);

void
InterfaceX_HostIO_Free(struct InterfaceX_HostIO* io);

void
InterfaceX_HostIO_DrainTasks(struct InterfaceX_HostIO* io);

/** One runner step + one reactor pass (emscripten / cooperative pump). */
void
InterfaceX_HostIO_Pump(struct InterfaceX_HostIO* io);

void
InterfaceX_HostIO_RunTask(
    struct InterfaceX_HostIO* io,
    void* task_state,
    int (*run_fn)(void*, struct LibToriRS_IOContext*),
    void (*destroy_fn)(void*));

struct ToriAuxLibCache*
InterfaceX_HostIO_Cache(struct InterfaceX_HostIO* io);

struct ToriAuxLibCore*
InterfaceX_HostIO_Core(struct InterfaceX_HostIO* io);

struct RSCacheDat2Disk*
InterfaceX_HostIO_Disk(struct InterfaceX_HostIO* io);

bool
InterfaceX_HostIO_ConfigEntryReady(
    struct InterfaceX_HostIO* io,
    int config_kind,
    int id);

bool
InterfaceX_HostIO_LoadConfigEntries(
    struct InterfaceX_HostIO* io,
    int config_kind,
    const int* ids,
    int id_count);

bool
InterfaceX_HostIO_LoadConfigEntry(
    struct InterfaceX_HostIO* io,
    int config_kind,
    int id);

struct ToriAuxLibCore_ClientScript*
InterfaceX_HostIO_ClientScriptGet(
    struct InterfaceX_HostIO* io,
    int script_id);

bool
InterfaceX_HostIO_LoadClientScript(
    struct InterfaceX_HostIO* io,
    int script_id);

void
InterfaceX_HostIO_LoadClientScripts(
    struct InterfaceX_HostIO* io,
    const int* script_ids,
    int script_count);

bool
InterfaceX_HostIO_GraphicSceneId(
    struct InterfaceX_HostIO* io,
    int graphic_id,
    int* scene_id_out);

bool
InterfaceX_HostIO_LoadGraphicScene(
    struct InterfaceX_HostIO* io,
    int graphic_id,
    int* scene_id_out);

struct ToriDraw_Font*
InterfaceX_HostIO_SceneFontGet(
    struct InterfaceX_HostIO* io,
    int font_id);

bool
InterfaceX_HostIO_LoadSceneFont(
    struct InterfaceX_HostIO* io,
    int font_id);

bool
InterfaceX_HostIO_ObjIconSceneId(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count,
    int* scene_id_out);

bool
InterfaceX_HostIO_ObjIconLoadFailed(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count);

void
InterfaceX_HostIO_MarkObjIconLoadFailed(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count);

bool
InterfaceX_HostIO_LoadObjIconScene(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count,
    int* scene_id_out);

int
InterfaceX_HostIO_ObjIconInventoryModelId(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count);

bool
InterfaceX_HostIO_LoadModel(
    struct InterfaceX_HostIO* io,
    int model_id);

bool
InterfaceX_HostIO_LoadNpctype(
    struct InterfaceX_HostIO* io,
    int npc_id);

bool
InterfaceX_HostIO_LoadPlayerAppearance(
    struct InterfaceX_HostIO* io,
    const int appearance[12]);

bool
InterfaceX_HostIO_PromoteObjtype(
    struct InterfaceX_HostIO* io,
    int obj_id);

bool
InterfaceX_HostIO_LoadObjectConfig(
    struct InterfaceX_HostIO* io,
    int obj_id);

int
InterfaceX_HostIO_LoadModelScene(
    struct InterfaceX_HostIO* io,
    int model_id,
    int zoom,
    int xan,
    int yan,
    int width,
    int height);

bool
InterfaceX_HostIO_LoadInterfaceGroup(
    struct InterfaceX_HostIO* io,
    int group_id);

enum InterfaceX_ModelLoadArgKind
{
    INTERFACEX_MLOAD_PLAIN = 1,
    INTERFACEX_MLOAD_NPC = 2,
    INTERFACEX_MLOAD_OBJ = 3,
    INTERFACEX_MLOAD_PLAYER = 4,
};

struct InterfaceX_ModelLoadArg
{
    int user_id;
    enum InterfaceX_ModelLoadArgKind kind;
    union
    {
        int model_id;
        int npc_id;
        int obj_id;
        int appearance[12];
    } u;
};

struct InterfaceX_ModelLoadResult
{
    int user_id;
    int scene_id;
};

struct InterfaceX_BatchModelLoad;

struct InterfaceX_TaskSpriteLoad;
struct InterfaceX_TaskFontLoad;
struct InterfaceX_TaskModelLoad;
struct InterfaceX_TaskInterfaceLoad;

struct InterfaceX_TaskSpriteLoad*
InterfaceX_TaskSpriteLoad_New(struct ToriAuxLibCache* cache, int sprite_id);

void
InterfaceX_TaskSpriteLoad_Free(struct InterfaceX_TaskSpriteLoad* task);

int
InterfaceX_TaskSpriteLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx);

bool
InterfaceX_HostIO_FinalizeGraphicScene(
    struct InterfaceX_HostIO* io,
    int graphic_id,
    int* scene_id_out);

struct InterfaceX_TaskFontLoad*
InterfaceX_TaskFontLoad_New(struct ToriAuxLibCache* cache, int font_id);

void
InterfaceX_TaskFontLoad_Free(struct InterfaceX_TaskFontLoad* task);

int
InterfaceX_TaskFontLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx);

bool
InterfaceX_HostIO_FinalizeSceneFont(struct InterfaceX_HostIO* io, int font_id);

struct InterfaceX_TaskModelLoad*
InterfaceX_TaskModelLoad_New(struct ToriAuxLibCache* cache, int model_id);

void
InterfaceX_TaskModelLoad_Free(struct InterfaceX_TaskModelLoad* task);

int
InterfaceX_TaskModelLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx);

struct InterfaceX_TaskInterfaceLoad*
InterfaceX_TaskInterfaceLoad_New(struct ToriAuxLibCache* cache, int group_id);

void
InterfaceX_TaskInterfaceLoad_Free(struct InterfaceX_TaskInterfaceLoad* task);

int
InterfaceX_TaskInterfaceLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx);

void
InterfaceX_HostIO_InterfaceGroupSubmit(struct InterfaceX_HostIO* io, int group_id);

bool
InterfaceX_HostIO_FinalizeObjIconScene(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count,
    int* scene_id_out);

int
InterfaceX_BatchModelLoad_RunAwait(
    struct InterfaceX_BatchModelLoad* batch,
    struct LibToriRS_IOContext* ctx);

void
InterfaceX_HostIO_QueueTask(
    struct InterfaceX_HostIO* io,
    void* task_state,
    int (*run_fn)(void*, struct LibToriRS_IOContext*),
    void (*destroy_fn)(void*));

struct InterfaceX_BatchModelLoad*
InterfaceX_BatchModelLoad_New(
    struct InterfaceX_HostIO* io,
    struct InterfaceX_ModelLoadArg const* args,
    int arg_count);

void
InterfaceX_BatchModelLoad_Destroy(struct InterfaceX_BatchModelLoad* batch);

void
InterfaceX_BatchModelLoad_Queue(
    struct InterfaceX_BatchModelLoad* batch,
    struct InterfaceX_HostIO* io);

struct InterfaceX_ModelLoadResult const*
InterfaceX_BatchModelLoad_Results(
    struct InterfaceX_BatchModelLoad const* batch,
    int* count_out);

#endif
