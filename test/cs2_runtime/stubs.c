#include "toriauxlib/vm/toriauxlibvm.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/varp_varbit_manager.h"
#include "toridraw/toridraw_scene.h"

#include <stddef.h>

struct ToriDraw_Font*
ToriDraw_SceneFontGet(
    struct ToriDraw_Scene* scene,
    int font_id)
{
    (void)scene;
    (void)font_id;
    return NULL;
}

int
ToriAuxLibVM_GetVarp(
    struct ToriAuxLibVM* vm,
    int id)
{
    (void)vm;
    (void)id;
    return 0;
}

int
ToriAuxLibVM_GetVarbit(
    struct ToriAuxLibVM* vm,
    int id)
{
    (void)vm;
    (void)id;
    return 0;
}

void
ToriAuxLibVM_SetVarpOptimistic(
    struct ToriAuxLibVM* vm,
    int id,
    int value)
{
    (void)vm;
    (void)id;
    (void)value;
}

struct VarPVarBitManager*
ToriAuxLibVM_VarPVarBit(struct ToriAuxLibVM* vm)
{
    (void)vm;
    return NULL;
}

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCore_ClientScriptGet(
    struct ToriAuxLibCore* core,
    int script_id)
{
    (void)core;
    (void)script_id;
    return NULL;
}

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptResolve(
    struct ToriAuxLibCache* cache,
    int script_id)
{
    (void)cache;
    (void)script_id;
    return NULL;
}

struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewLoad(
    struct RSCacheDat2Disk* cache,
    int table,
    int archive)
{
    (void)cache;
    (void)table;
    (void)archive;
    return NULL;
}

void
RSCacheDat2Disk_ArchiveInitMetadata(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive)
{
    (void)cache;
    (void)archive;
}

void
RSCacheDat2Disk_ArchiveFree(struct RSCacheDat2Disk_Archive* archive)
{
    (void)archive;
}

int
varp_varbit_get_varp(
    struct VarPVarBitManager const* mgr,
    int id)
{
    (void)mgr;
    (void)id;
    return 0;
}

void
varp_varbit_set_varp_optimistic(
    struct VarPVarBitManager* mgr,
    int id,
    int value)
{
    (void)mgr;
    (void)id;
    (void)value;
}
