#include "games/runescape.h"
#include "osrs/varp_varbit_manager.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_scene.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "ui/ui_input.h"
#include "ui/ui_minimenu.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int g_sin_table_data[2048];
static int noise_cos_table_data[2048];
const int* g_sin_table = g_sin_table_data;
const int* RSCacheDat2A_NoiseCosTable = noise_cos_table_data;

void
ToriDraw_LightModelDefault(
    struct ToriDraw_ModelHandle hnd,
    int model_contrast,
    int model_ambient)
{
    (void)hnd;
    (void)model_contrast;
    (void)model_ambient;
}

struct ToriDraw_Model*
ToriDraw_ModelNewFromCacheModel(struct RSCacheDat2A_Model* model)
{
    (void)model;
    return calloc(1, sizeof(struct ToriDraw_Model));
}

void
ToriDraw_ModelFree(struct ToriDraw_Model* model)
{
    free(model);
}

void
ToriDraw_AnimationFree(struct ToriDraw_Animation* animation)
{
    free(animation);
}

void
ToriDraw_ModelSetBoundsCylinder(struct ToriDraw_Model* model)
{
    (void)model;
}

void
ToriDraw_RenderModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer)
{
    (void)hnd;
    (void)scene;
    (void)position;
    (void)view_port;
    (void)camera;
    (void)pixel_buffer;
}

void
ToriDraw_Init(void)
{
}

void
GameRunescape_SetUIInvPool(struct GameRunescape* game, struct UIInventoryPool* pool)
{
    (void)game;
    (void)pool;
}

void
GameRunescape_SetTD(
    struct GameRunescape* game,
    struct ToriAuxLibTD* td)
{
    if( game )
        game->td = td;
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

int32_t
GameRunescape_UIHitTest(
    struct GameRunescape* game,
    int px,
    int py)
{
    if( !game || !game->ui_tree )
        return -1;
    return uitree_hit_test_interactive(game->ui_tree, &game->ui_host, NULL, px, py);
}

bool
GameRunescape_MinimenuPrepareShow(
    struct GameRunescape* game,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width)
{
    if( !game )
        return false;

    int font_id = -1;
    if( game->ui_tree && game->ui_minimenu_node >= 0 )
        font_id = game->ui_tree->components[game->ui_minimenu_node].u.minimenu.font_id;

    struct ToriDraw_Font* font =
        game->scene && font_id >= 0 ? ToriDraw_SceneFontGet(game->scene, font_id) : NULL;
    return ui_minimenu_prepare_show(
        &game->minimenu, font, out_layout, out_content_width);
}

void
GameRunescape_SendChatSetMode(struct GameRunescape* game)
{
    (void)game;
}

void
GameRunescape_GetScrollPos(
    struct GameRunescape const* game,
    int component_id,
    int* sx,
    int* sy)
{
    if( sx )
        *sx = 0;
    if( sy )
        *sy = 0;
    if( !game || component_id < 0 || component_id >= 8192 )
        return;
    if( sx )
        *sx = game->ui_scroll_x[component_id];
    if( sy )
        *sy = game->ui_scroll_y[component_id];
}

void
GameRunescape_ClampScroll(
    struct GameRunescape* game,
    struct StaticUIComponent const* layer)
{
    (void)game;
    (void)layer;
}

struct ToriAuxLibVM
{
    struct VarPVarBitManager varp_varbit;
};

struct ToriAuxLibVM*
ToriAuxLibVM_New(void)
{
    struct ToriAuxLibVM* vm = calloc(1, sizeof(*vm));
    if( !vm )
        return NULL;
    varp_varbit_init(&vm->varp_varbit);
    return vm;
}

void
ToriAuxLibVM_Free(struct ToriAuxLibVM* vm)
{
    if( !vm )
        return;
    varp_varbit_free(&vm->varp_varbit);
    free(vm);
}

struct VarPVarBitManager*
ToriAuxLibVM_VarPVarBit(struct ToriAuxLibVM* vm)
{
    return vm ? &vm->varp_varbit : NULL;
}
