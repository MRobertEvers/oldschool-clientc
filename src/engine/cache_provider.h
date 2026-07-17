#ifndef CACHE_PROVIDER_H
#define CACHE_PROVIDER_H

#include "torirs_types.h"

#include <hmap.h>
#include <stdbool.h>

struct CS2VM2_Script;
struct CacheProviderVTable;

struct CacheProvider
{
    // This MUST be the very first member of the struct.
    struct CacheProviderVTable* vtable;

    struct HMap* model_cache;
    struct HMap* sprite_cache;
    struct HMap* componentpack_cache;
    struct HMap* clientscript_cache;
};

struct CacheProviderVTable
{
    struct ToriRS_Task* (*Task_ModelLoad)(
        struct CacheProvider* provider,
        int model_id);
    struct ToriRS_Task* (*Task_ComponentPackLoad)(
        struct CacheProvider* provider,
        int iface_id);
    struct ToriRS_Task* (*Task_ClientScriptLoad)(
        struct CacheProvider* provider,
        int script_id);
};

void
CacheProvider_InitEngineCaches(struct CacheProvider* provider);

void
CacheProvider_FreeEngineCaches(struct CacheProvider* provider);

void
CacheProvider_ModelAdd(
    struct CacheProvider* provider,
    int model_id,
    struct ToriRS_Model* model);

struct ToriRS_Model*
CacheProvider_ModelGet(
    struct CacheProvider* provider,
    int model_id);

bool
CacheProvider_ModelHas(
    struct CacheProvider* provider,
    int model_id);

void
CacheProvider_ModelsCleanup(struct CacheProvider* provider);

void
CacheProvider_ComponentPackAdd(
    struct CacheProvider* provider,
    int iface_id,
    struct ToriRS_ComponentPack* pack);

struct ToriRS_ComponentPack*
CacheProvider_ComponentPackGet(
    struct CacheProvider* provider,
    int iface_id);

bool
CacheProvider_ComponentPackHas(
    struct CacheProvider* provider,
    int iface_id);

void
CacheProvider_ComponentPacksCleanup(struct CacheProvider* provider);

void
CacheProvider_ClientScriptAdd(
    struct CacheProvider* provider,
    int script_id,
    struct CS2VM2_Script* script);

struct CS2VM2_Script*
CacheProvider_ClientScriptGet(
    struct CacheProvider* provider,
    int script_id);

bool
CacheProvider_ClientScriptHas(
    struct CacheProvider* provider,
    int script_id);

void
CacheProvider_ClientScriptsCleanup(struct CacheProvider* provider);

static inline struct ToriRS_Task*
CreateTask_ModelLoad(
    struct CacheProvider* provider,
    int model_id)
{
    return provider->vtable->Task_ModelLoad(provider, model_id);
}

static inline struct ToriRS_Task*
CreateTask_ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id)
{
    return provider->vtable->Task_ComponentPackLoad(provider, iface_id);
}

static inline struct ToriRS_Task*
CreateTask_ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id)
{
    return provider->vtable->Task_ClientScriptLoad(provider, script_id);
}

#endif
