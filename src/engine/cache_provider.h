#ifndef CACHE_PROVIDER_H
#define CACHE_PROVIDER_H

#include "torirs_types.h"

#include <hmap.h>

struct CacheProviderVTable;

struct CacheProvider
{
    // This MUST be the very first member of the struct.
    struct CacheProviderVTable* vtable;

    struct HMap* model_cache;
    struct HMap* sprite_cache;
    struct HMap* componentpack_cache;
};

struct CacheProviderVTable
{
    struct ToriRS_Task* (*Task_ModelLoad)(
        struct CacheProvider* provider,
        int model_id,
        struct ToriRS_Model** out_model);
};

#endif