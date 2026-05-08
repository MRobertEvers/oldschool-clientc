#include "osrs/revconfig/uitree_loader_game_executor.h"

#include "osrs/buildcachedat_loader.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/gamecache/gamecache_from_buildcachedat.h"
#include "osrs/revconfig/uiscene.h"

#include <assert.h>

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_sprite(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    assert(0 && "Not implemented");
    return UITREE_LOADER_GAME_EXECUTOR_ERROR;
}

static inline enum UITreeLoaderRSComponentType
gcc_to_rs_component_type(int type)
{
    switch( type )
    {
    case COMPONENT_TYPE_LAYER:
        return UITREE_LOADER_RS_COMPONENT_TYPE_LAYER;
    case COMPONENT_TYPE_INV:
        return UITREE_LOADER_RS_COMPONENT_TYPE_INV;
    case COMPONENT_TYPE_RECT:
        return UITREE_LOADER_RS_COMPONENT_TYPE_RECT;
    case COMPONENT_TYPE_TEXT:
        return UITREE_LOADER_RS_COMPONENT_TYPE_TEXT;
    case COMPONENT_TYPE_GRAPHIC:
        return UITREE_LOADER_RS_COMPONENT_TYPE_GRAPHIC;
    case COMPONENT_TYPE_MODEL:
        return UITREE_LOADER_RS_COMPONENT_TYPE_MODEL;
    case COMPONENT_TYPE_INV_TEXT:
        return UITREE_LOADER_RS_COMPONENT_TYPE_INV_TEXT;
    default:
        return UITREE_LOADER_RS_COMPONENT_TYPE_LAYER;
    }

    return UITREE_LOADER_RS_COMPONENT_TYPE_LAYER;
}

static inline void
copy_gcc_to_resolve(
    struct GameCacheComponent* gcc,
    struct UITreeLoaderAssetRequest* req)
{
    req->u.rs_component.resolve.id = gcc->id;
    req->u.rs_component.resolve.type = gcc_to_rs_component_type(gcc->type);
    req->u.rs_component.resolve.buttonType = gcc->buttonType;
    req->u.rs_component.resolve.clientCode = gcc->clientCode;
    req->u.rs_component.resolve.width = gcc->width;
    req->u.rs_component.resolve.height = gcc->height;
    req->u.rs_component.resolve.x = gcc->x;
    req->u.rs_component.resolve.y = gcc->y;
    req->u.rs_component.resolve.scripts = gcc->scripts;
    req->u.rs_component.resolve.scripts_count = gcc->scripts_count;
    req->u.rs_component.resolve.scripts_lengths = gcc->scripts_lengths;
    req->u.rs_component.resolve.alpha = gcc->alpha;
    req->u.rs_component.resolve.overlayer = gcc->overlayer;
    req->u.rs_component.resolve.scriptComparator = gcc->scriptComparator;
    req->u.rs_component.resolve.scriptOperand = gcc->scriptOperand;
    req->u.rs_component.resolve.script_comparator_count = gcc->script_comparator_count;
    req->u.rs_component.resolve.invSlotObjId = gcc->invSlotObjId;
    req->u.rs_component.resolve.invSlotObjCount = gcc->invSlotObjCount;
    req->u.rs_component.resolve.seqFrame = gcc->seqFrame;
    req->u.rs_component.resolve.seqCycle = gcc->seqCycle;
    req->u.rs_component.resolve.children = gcc->children;
    req->u.rs_component.resolve.children_count = gcc->children_count;
    req->u.rs_component.resolve.anim = gcc->anim;
    req->u.rs_component.resolve.activeAnim = gcc->activeAnim;
    req->u.rs_component.resolve.activeModelType = gcc->activeModelType;
    req->u.rs_component.resolve.activeModel = gcc->activeModel;
    req->u.rs_component.resolve.zoom = gcc->zoom;
    req->u.rs_component.resolve.xan = gcc->xan;
    req->u.rs_component.resolve.yan = gcc->yan;
    req->u.rs_component.resolve.scroll = gcc->scroll;
    req->u.rs_component.resolve.hide = gcc->hide;
    req->u.rs_component.resolve.targetVerb = gcc->targetVerb;
    req->u.rs_component.resolve.targetText = gcc->targetText;
    req->u.rs_component.resolve.targetMask = gcc->targetMask;
    req->u.rs_component.resolve.option = gcc->option;
    req->u.rs_component.resolve.marginX = gcc->marginX;
    req->u.rs_component.resolve.marginY = gcc->marginY;
    req->u.rs_component.resolve.colour = gcc->colour;
    req->u.rs_component.resolve.activeColour = gcc->activeColour;
    req->u.rs_component.resolve.overColour = gcc->overColour;
    req->u.rs_component.resolve.activeOverColour = gcc->activeOverColour;
    req->u.rs_component.resolve.modelType = gcc->modelType;
    req->u.rs_component.resolve.model = gcc->model;
    req->u.rs_component.resolve.text = gcc->text;
    req->u.rs_component.resolve.activeText = gcc->activeText;
    req->u.rs_component.resolve.draggable = gcc->draggable;
    req->u.rs_component.resolve.interactable = gcc->interactable;
    req->u.rs_component.resolve.usable = gcc->usable;
    req->u.rs_component.resolve.swappable = gcc->swappable;
    req->u.rs_component.resolve.fill = gcc->fill;
    req->u.rs_component.resolve.center = gcc->center;
    req->u.rs_component.resolve.shadowed = gcc->shadowed;
    req->u.rs_component.resolve.graphic = gcc->graphic;
    req->u.rs_component.resolve.activeGraphic = gcc->activeGraphic;
    req->u.rs_component.resolve.invSlotGraphic = gcc->invSlotGraphic;
    req->u.rs_component.resolve.iop = gcc->iop;
    req->u.rs_component.resolve.childX = gcc->childX;
    req->u.rs_component.resolve.childY = gcc->childY;
    req->u.rs_component.resolve.invSlotOffsetX = gcc->invSlotOffsetX;
    req->u.rs_component.resolve.invSlotOffsetY = gcc->invSlotOffsetY;
    req->u.rs_component.resolve.font = gcc->font;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_rs_component(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    struct GameCacheComponent* gcc = NULL;
    assert(req->kind == UITREE_ASSET_RS_COMPONENT);

    struct GameCache* gc = game->gamecache;
    int cid = req->u.rs_component.component_id;
    if( cid < 0 )
        return UITREE_LOADER_GAME_EXECUTOR_ERROR;

    if( !(gcc = gamecache_get_component(gc, cid)) )
        return UITREE_LOADER_GAME_EXECUTOR_NEED_RS_COMPONENT;

    copy_gcc_to_resolve(gcc, req);

    req->resolved = true;
    return UITREE_LOADER_GAME_EXECUTOR_OK;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_model(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    assert(0 && "Not implemented");
    return UITREE_LOADER_GAME_EXECUTOR_ERROR;
}

enum UITreeLoaderGameExecutorResult
uitree_loader_game_executor_font(
    struct GGame* game,
    struct UITreeLoaderAssetRequest* req)
{
    assert(0 && "Not implemented");
    return UITREE_LOADER_GAME_EXECUTOR_ERROR;
}
