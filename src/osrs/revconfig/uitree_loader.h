#ifndef UITREE_LOADER_H
#define UITREE_LOADER_H

#include <stdint.h>

struct UITree;
struct UIScene;
struct Scene2;
struct UIInventoryPool;
struct GGame;
struct RevConfigBuffer;

/** Max distinct GameCache component ids reported in one UITREE_ASSET_INTERFACE request. */
#define UITREE_MAX_INTERFACE_REQUESTS 128

/**
 * Status returned by uitree_loader_step().
 */
enum UITreeLoaderStatus
{
    /** Still processing fields; call step() again. */
    UITREE_LOADER_RUNNING,
    /** Paused waiting for an external asset; fill the request and call step() again. */
    UITREE_LOADER_NEEDS_ASSET,
    /** All fields processed; tree is fully built. */
    UITREE_LOADER_DONE,
    /** Unrecoverable error during loading. */
    UITREE_LOADER_ERROR,
};

/**
 * Tagged kind for the asset the loader needs the caller to supply.
 */
enum UITreeLoaderAssetKind
{
    UITREE_ASSET_NONE = 0,
    /** 2D media jagfile is absent; load it and call buildcachedat_set_2d_media_jagfile(). */
    UITREE_ASSET_SPRITE,
    /** A GameCacheComponent is absent; load interface archive and convert components. */
    UITREE_ASSET_INTERFACE,
    /** A specific model is missing from the model cache. */
    UITREE_ASSET_MODEL,
    /** A named font is not yet in UIScene. */
    UITREE_ASSET_FONT,
};

/**
 * Describes the asset the loader could not find.  Inspect `kind`, then read the
 * matching union member to learn what to load.
 */
struct UITreeLoaderAssetRequest
{
    enum UITreeLoaderAssetKind kind;
    union {
        /** UITREE_ASSET_SPRITE: logical sprite name from the INI (informational only). */
        struct
        {
            char name[64];
        } sprite;
        /** UITREE_ASSET_INTERFACE: GameCache roots missing from gamecache (batch siblings). */
        struct
        {
            int component_ids[UITREE_MAX_INTERFACE_REQUESTS];
            int count;
        } interface_file;
        /** UITREE_ASSET_MODEL: model id missing from the model cache. */
        struct
        {
            int model_id;
        } model;
        /** UITREE_ASSET_FONT: font name not yet registered in UIScene. */
        struct
        {
            char name[64];
        } font;
    } u;
};

/**
 * Incremental UITree loader.  Create with uitree_loader_new(), drive with
 * uitree_loader_step(), and free with uitree_loader_free() when DONE or ERROR.
 *
 * The struct is opaque to callers; only the status and pending_asset fields are
 * accessed through uitree_loader_step() / uitree_loader_pending_asset().
 */
struct UITreeLoader;

/**
 * Allocate a new loader for the given revconfig buffer.  All pointer arguments
 * are borrowed (not owned); the caller must keep them alive until uitree_loader_free().
 * Returns NULL on allocation failure.
 */
struct UITreeLoader*
uitree_loader_new(
    struct UITree*          ui,
    struct UIScene*         ui_scene,
    struct Scene2*          scene2,
    struct UIInventoryPool* inv_pool,
    struct GGame*           game,
    struct RevConfigBuffer* revconfig_buffer);

/**
 * Advance the loader.  Processes revconfig fields until the item is complete,
 * an asset is needed, or all fields have been consumed.
 *
 * - UITREE_LOADER_RUNNING     : internal fields were accumulated; call again.
 * - UITREE_LOADER_NEEDS_ASSET : paused; call uitree_loader_pending_asset(), supply
 *                               the asset, then call step() again.
 * - UITREE_LOADER_DONE        : tree fully built; free the loader.
 * - UITREE_LOADER_ERROR       : allocation failure or invalid state; free the loader.
 */
enum UITreeLoaderStatus
uitree_loader_step(struct UITreeLoader* loader);

/**
 * Returns the asset request that caused the last UITREE_LOADER_NEEDS_ASSET result.
 * Only valid when the most recent step() returned UITREE_LOADER_NEEDS_ASSET.
 */
struct UITreeLoaderAssetRequest
uitree_loader_pending_asset(const struct UITreeLoader* loader);

/**
 * Free the loader.  Does NOT free the UITree, UIScene, etc. (those are borrowed).
 */
void
uitree_loader_free(struct UITreeLoader* loader);

#endif /* UITREE_LOADER_H */
