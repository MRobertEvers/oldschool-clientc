#ifndef INTERFACEX_HOST_IO_H
#define INTERFACEX_HOST_IO_H

#include "toriauxlib/core/tasks/core_task_runner.h"

#include <stdbool.h>
#include <stdint.h>

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
    struct CoreTaskRunner* task_runner;
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
InterfaceX_HostIO_LoadObjIconScene(
    struct InterfaceX_HostIO* io,
    int obj_id,
    int count,
    int* scene_id_out);

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

#endif
