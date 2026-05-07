#ifndef GAME_UITREE_LOAD_EXEC_H
#define GAME_UITREE_LOAD_EXEC_H

#include "osrs/revconfig/uitree_loader.h"

struct GGame;

/**
 * Outcome of game_uitree_load_exec_* after UITREE_LOADER_NEEDS_ASSET.
 * The host uses this to decide which bulk I/O or cache build step to run before
 * calling uitree_loader_step() again.
 */
enum GameUitreeLoadExecResult
{
    /** Request satisfied (or nothing to do); retry uitree_loader_step(). */
    GAME_UITREE_EXEC_OK = 0,
    /** Load cfg_media_jagfile / 2D media and register sprites, then retry. */
    GAME_UITREE_EXEC_NEED_2D_MEDIA,
    /** Load interface archives into GameCache, then retry. */
    GAME_UITREE_EXEC_NEED_INTERFACES,
    /** Load the model archive for req->u.model.model_id, then retry. */
    GAME_UITREE_EXEC_NEED_MODEL,
    /** Register or load the requested font into UIScene / GameCache, then retry. */
    GAME_UITREE_EXEC_NEED_FONT,
    /** Unrecoverable for this request. */
    GAME_UITREE_EXEC_ERROR,
};

/**
 * Stub / minimal wiring: resolves `req` into `req->game_binding` where applicable
 * and returns what the host still needs to load.
 */
enum GameUitreeLoadExecResult
game_uitree_load_exec_sprite(struct GGame* game, struct UITreeLoaderAssetRequest* req);

enum GameUitreeLoadExecResult
game_uitree_load_exec_interface(struct GGame* game, struct UITreeLoaderAssetRequest* req);

enum GameUitreeLoadExecResult
game_uitree_load_exec_model(struct GGame* game, struct UITreeLoaderAssetRequest* req);

enum GameUitreeLoadExecResult
game_uitree_load_exec_font(struct GGame* game, struct UITreeLoaderAssetRequest* req);

#endif /* GAME_UITREE_LOAD_EXEC_H */
