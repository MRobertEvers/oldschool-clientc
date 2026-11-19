#ifndef TASK_PACK_ASSETS_LOAD_H
#define TASK_PACK_ASSETS_LOAD_H

#include "asyncio.h"

struct CacheProvider;
struct ToriMem_Group;
struct ToriRS_ComponentPack;

/**
 * Prefetch sprites / fonts / models referenced by a loaded ComponentPack
 * (interfacex AwaitPendingAssets). Returns NULL if the pack is missing or
 * already fully cached with nothing to load — callers use PT_TASK_AWAITSELF_IF.
 */
struct ToriRS_Task*
CreateTask_PackAssetsLoad(
    struct CacheProvider* provider,
    int iface_id);

/**
 * What a MOUNT waits for of a loaded pack: its sprites, fonts and the npc
 * configs its npc-head widgets name -- everything the bake, the load scripts
 * and the transmits read. Not its models or the default avatar's kit models:
 * those only change what is drawn, and the mount leaves them to a parallel
 * task (PackAssets_CollectVisual says which). NULL as CreateTask_PackAssetsLoad.
 */
struct ToriRS_Task*
CreateTask_PackAssetsLoadRequired(
    struct CacheProvider* provider,
    int iface_id);

/** A pack's VISUAL assets: the ones CreateTask_PackAssetsLoadRequired leaves
 *  out. */
struct PackVisualAssets
{
    /* Its model ids (the static and the active model of a model widget),
     * each once. Owned; grown in the collector's memory group. */
    int* model_ids;
    int model_count;
    int model_cap;
    /* A widget composites the local player (clientCode 327/328): the default
     * appearance's kit models (CreateTask_PlayerAppearanceLoad). */
    int needs_player;
};

/** Fill `out` (zeroed by the caller) from `pack`, allocating in `mem`. */
void
PackAssets_CollectVisual(
    struct ToriRS_ComponentPack const* pack,
    struct ToriMem_Group* mem,
    struct PackVisualAssets* out);

/** Free what PackAssets_CollectVisual allocated in `mem`. */
void
PackAssets_FreeVisual(
    struct ToriMem_Group* mem,
    struct PackVisualAssets* assets);

#endif
