#ifndef TASK_PACK_ASSETS_LOAD_H
#define TASK_PACK_ASSETS_LOAD_H

#include "asyncio.h"

struct CacheProvider;

/**
 * Prefetch sprites / fonts / models referenced by a loaded ComponentPack
 * (interfacex AwaitPendingAssets). Returns NULL if the pack is missing or
 * already fully cached with nothing to load — callers use PT_TASK_AWAITSELF_IF.
 */
struct ToriRS_Task*
CreateTask_PackAssetsLoad(
    struct CacheProvider* provider,
    int iface_id);

#endif
