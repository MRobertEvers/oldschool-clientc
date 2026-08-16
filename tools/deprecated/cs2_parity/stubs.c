#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/varp_varbit_manager.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/vm/toriauxlibvm.h"

#include <stdbool.h>
#include <stddef.h>

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

struct ToriAuxLibCore_Component*
ToriAuxLibCore_ComponentGet(
    struct ToriAuxLibCore* core,
    int component_id)
{
    (void)core;
    (void)component_id;
    return NULL;
}

bool
ToriAuxLibCore_ComponentHas(
    struct ToriAuxLibCore* core,
    int component_id)
{
    (void)core;
    (void)component_id;
    return false;
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

int
varp_varbit_resolve_transform(
    const struct VarPVarBitManager* mgr,
    const int* transforms,
    int transform_count,
    int transform_varbit,
    int transform_varp)
{
    (void)mgr;
    (void)transforms;
    (void)transform_count;
    (void)transform_varbit;
    (void)transform_varp;
    return -1;
}
