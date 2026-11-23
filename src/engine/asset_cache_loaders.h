#ifndef SRC_ENGINE_ASSET_CACHE_LOADERS_H
#define SRC_ENGINE_ASSET_CACHE_LOADERS_H

/*
 * The assets table's loaders for every kind whose load is the cache's own
 * task: a model (and the skybox, which is one), a texture, a sprite, an
 * interface group's ComponentPack, a sound clip, an obj's model chain, every
 * config table, and a map square's terrain and scenery. One place says which
 * CreateTask_* loads a key and how the cache answers "is it here?", for every
 * host -- the App, a test, a tool.
 *
 * A kind whose load needs more than the cache (a sequence needs the scene, a
 * song the music player, a spot-animation or a world map region the App) is
 * registered by its owner after this, over the same table.
 */

#include "engine/asset_requests.h"

#include <stdbool.h>

struct Assets;
struct CacheProvider;
struct ToriRS_Task;

/** Does `kind` load through this file? */
bool
AssetCache_HandlesKind(enum AssetKind kind);

/** Is `key` here? For a kind AssetCache_HandlesKind says yes to. */
bool
AssetCache_Resident(
    struct CacheProvider* provider,
    struct AssetKey key);

/**
 * The cache's load for `key`, or NULL when it holds the record already (the
 * CreateTask_*Load convention). `args` is storage the load may read by
 * pointer while it runs ({id, variant}: an obj model reads its obj and count
 * from it); AssetLoadTask carries it.
 */
struct ToriRS_Task*
AssetCache_LoadTask(
    struct CacheProvider* provider,
    struct AssetKey key,
    int const* args);

/** Register this file's loader for every kind it handles, over `assets`,
 *  loading from `provider` onto its asset queue, and make `assets` the
 *  provider's table (CacheProvider_SetAssets). */
void
AssetCache_RegisterLoaders(
    struct Assets* assets,
    struct CacheProvider* provider);

#endif /* SRC_ENGINE_ASSET_CACHE_LOADERS_H */
