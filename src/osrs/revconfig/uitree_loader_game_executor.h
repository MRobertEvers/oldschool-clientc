#ifndef UITREE_LOADER_GAME_EXECUTOR_H
#define UITREE_LOADER_GAME_EXECUTOR_H

#include "osrs/revconfig/uitree_loader.h"

struct GGame;

/**
 * Outcome of uitree_loader_game_executor_* after UITREE_LOADER_NEEDS_ASSET.
 * The host uses this to decide which bulk I/O or cache build step to run before
 * calling uitree_loader_step() again.
 */
enum UITreeLoaderGameExecutorResult
{
    /** Request satisfied (or nothing to do); retry uitree_loader_step(). */
    UITREE_LOADER_GAME_EXECUTOR_OK = 0,
    /** Sprite ref missing from GameCache; load 2D media / register sprites, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_2D_MEDIA,
    /** Load interface archives into GameCache, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_INTERFACES,
    /** Load the model archive for req->u.model.model_id, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_MODEL,
    /** Font name missing from GameCache font reftable; load fonts and sync refs, then retry. */
    UITREE_LOADER_GAME_EXECUTOR_NEED_FONT,
    /** Unrecoverable for this request. */
    UITREE_LOADER_GAME_EXECUTOR_ERROR,
};

/**
 * Resolves `req` into `req->game_binding` where applicable using GameCache only
 * and returns what the host still needs to load.
 */
enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_sprite(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_interface(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_model(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_font(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req);

#endif /* UITREE_LOADER_GAME_EXECUTOR_H */
