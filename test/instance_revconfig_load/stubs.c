#include "games/runescape.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_scene.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toriauxlib/vm/toriauxlibvm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

void
ToriDraw_Init(void)
{
}

struct ToriDraw_Scene*
ToriDraw_SceneNew(uint32_t flags)
{
    (void)flags;
    return calloc(1, sizeof(struct ToriDraw_Scene));
}

void
ToriDraw_SceneFree(struct ToriDraw_Scene* scene)
{
    free(scene);
}

void
GameRunescape_SetUIInvPool(struct GameRunescape* game, struct UIInventoryPool* pool)
{
    (void)game;
    (void)pool;
}

void
GameRunescape_SetUITreeReady(struct GameRunescape* game, bool ready)
{
    (void)game;
    (void)ready;
}

void
GameRunescape_SyncUISpritesFromScene(struct GameRunescape* game)
{
    (void)game;
}

struct ToriAuxLibTD*
ToriAuxLibTD_New(
    struct ToriAuxLibCore* core,
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene)
{
    (void)core;
    (void)c;
    (void)scene;
    return NULL;
}

void
ToriAuxLibTD_Free(struct ToriAuxLibTD* td)
{
    (void)td;
}

bool
ToriAuxLibTD_Sprite(struct ToriAuxLibTD* td, int element_id)
{
    (void)td;
    (void)element_id;
    return false;
}

bool
ToriAuxLibTD_Font(struct ToriAuxLibTD* td, int font_id)
{
    (void)td;
    (void)font_id;
    return false;
}

struct ToriAuxLibVM*
ToriAuxLibVM_New(void)
{
    return NULL;
}

void
ToriAuxLibVM_Free(struct ToriAuxLibVM* vm)
{
    (void)vm;
}

struct VarPVarBitManager*
ToriAuxLibVM_VarPVarBit(struct ToriAuxLibVM* vm)
{
    (void)vm;
    return NULL;
}
