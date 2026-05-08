#ifndef UITREE_LOADER_H
#define UITREE_LOADER_H

#include <stdint.h>

struct UITree;
struct UIScene;
struct UIInventoryPool;
struct RevConfigBuffer;
struct BuildCacheDat;
struct GameCache;
struct DashGraphics;
struct GGame;

/** Max distinct GameCache component ids reported in one UITREE_ASSET_INTERFACE request. */
#define UITREE_MAX_INTERFACE_REQUESTS 128

/** Max asset requests emitted in one UITREE_LOADER_NEEDS_ASSET pause. */
#define UITREE_LOADER_MAX_PENDING_ASSETS 16

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
    /** Sprite ref missing from GameCache; load 2D media / register sprites, then retry. */
    UITREE_ASSET_SPRITE,
    /** A GameCacheComponent is absent; load interface archive and convert components. */
    UITREE_ASSET_INTERFACE,
    /** A specific model is missing from the model cache. */
    UITREE_ASSET_MODEL,
    /** Font name missing from GameCache font reftable. */
    UITREE_ASSET_FONT,
};

/**
 * Describes the asset the loader could not find.  Inspect `kind`, then read the
 * matching union member to learn what to load.
 */
struct UITreeLoaderAssetRequest
{
    enum UITreeLoaderAssetKind kind;
    /**
     * Filled by uitree_loader_game_executor_* after the loader pauses on UITREE_LOADER_NEEDS_ASSET.
     * The loader zeroes the whole struct when it emits a request; these stay 0 until exec runs.
     */
    struct
    {
        int uiscene_element_id;
        int inv_pool_index;
    } game_binding;
    union
    {
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
 * The struct is opaque to callers; status and pending asset requests are read via
 * uitree_loader_step() / uitree_loader_pending_assets().
 */
struct UITreeLoader;

struct UITreeLoader*
uitree_loader_new(
    struct UITree* ui,
    struct RevConfigBuffer* revconfig_buffer);

enum UITreeLoaderStatus
uitree_loader_step(struct UITreeLoader* loader);

enum UITreeLoaderStatus
uitree_loader_step_send_revconfig(
    struct UITreeLoader* loader,
    struct RevConfigBuffer* revconfig);

int
uitree_loader_pending_asset_count(const struct UITreeLoader* loader);

const struct UITreeLoaderAssetRequest*
uitree_loader_pending_assets(const struct UITreeLoader* loader);

/** Writable pending slots while status is UITREE_LOADER_NEEDS_ASSET (uitree_loader_game_executor_* writes
 * bindings).
 */
struct UITreeLoaderAssetRequest*
uitree_loader_pending_assets_mut(struct UITreeLoader* loader);

void
uitree_loader_free(struct UITreeLoader* loader);

#endif /* UITREE_LOADER_H */
