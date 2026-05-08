#ifndef UITREE_LOADER_GAME_EXECUTOR_H
#define UITREE_LOADER_GAME_EXECUTOR_H

#include "osrs/revconfig/uitree_loader.h"

struct GGame;

/**
 * Outcome of uitree_loader_game_executor_* after UITREE_LOADER_NEEDS_ASSET.
 * The host calls these on `uitree_loader_pending_assets_mut()` slots to resolve
 * GameCache-backed fields into `game_binding`; `uitree_loader_step()` does not
 * invoke them. Use the result to decide which bulk I/O to run before stepping again.
 */
enum UITreeLoaderGameExecutorResult
{
    /** Request satisfied (or nothing to do); retry uitree_loader_step(). */
    UITREE_LOADER_GAME_EXECUTOR_OK = 0,
    /** Sprite ref missing from GameCache; load 2D media / register sprites, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_2D_MEDIA,
    /** Load interface archives into GameCache, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_RS_COMPONENT,
    /** Load the model archive for req->u.model.model_id, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_RS_MODEL,
    /** Font name missing from GameCache font reftable; load fonts and sync refs, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_FONT,
    /** Unrecoverable for this request. */
    UITREE_LOADER_GAME_EXECUTOR_ERROR,
};

/**
 * Fills `req->game_binding`, allocates UIScene rows / lazy loads from BuildCacheDat as needed,
 * then sets `req->binding_ready`. Call from the host between UITREE_LOADER_NEEDS_ASSET and the
 * next uitree_loader_step().
 */
enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_sprite(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_rs_component(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_rs_model(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_font(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

#endif /* UITREE_LOADER_GAME_EXECUTOR_H */
