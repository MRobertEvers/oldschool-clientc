#include "games/runescape.h"
#include "osrs/varp_varbit_manager.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_scene.h"
#include "ui/rs_inv_container.h"
#include "ui/ui_behavior.h"
#include "ui/ui_input.h"
#include "ui/ui_input_adapter.h"
#include "ui/ui_minimenu.h"
#include "games/runescape_cs2_host.h"
#include "games/runescape_cs2_queue.h"
#include "vm/cs2vmx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define RS_UI_HOVER_MAIN_X 4
#define RS_UI_HOVER_MAIN_Y 4
#define RS_UI_HOVER_MAIN_W 512
#define RS_UI_HOVER_MAIN_H 334
#define RS_UI_HOVER_SIDE_X 553
#define RS_UI_HOVER_SIDE_Y 205
#define RS_UI_HOVER_SIDE_W 190
#define RS_UI_HOVER_SIDE_H 261
#define RS_UI_HOVER_CHAT_X 17
#define RS_UI_HOVER_CHAT_Y 357
#define RS_UI_HOVER_CHAT_W 409
#define RS_UI_HOVER_CHAT_H 96

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
{}

void
GameRunescape_SetUIInvPool(
    struct GameRunescape* game,
    struct UIInventoryPool* pool)
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
GameRunescape_SetUITreeReady(
    struct GameRunescape* game,
    bool ready)
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
    if( game->ui_tree && game->ui_hover.minimenu_node >= 0 )
        font_id = game->ui_tree->components[game->ui_hover.minimenu_node].u.minimenu.font_id;

    struct ToriDraw_Font* font =
        game->scene && font_id >= 0 ? ToriDraw_SceneFontGet(game->scene, font_id) : NULL;
    return ui_minimenu_prepare_show(&game->minimenu, font, out_layout, out_content_width);
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
        *sx = game->ui_scroll.scroll_x[component_id];
    if( sy )
        *sy = game->ui_scroll.scroll_y[component_id];
}

void
GameRunescape_ClampScroll(
    struct GameRunescape* game,
    struct StaticUIComponent const* layer)
{
    (void)game;
    (void)layer;
}

void
GameRunescape_RefreshPicksetAtMouse(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y)
{
    (void)game;
    (void)mouse_x;
    (void)mouse_y;
}

void
GameRunescape_UpdateWorldViewport(struct GameRunescape* game)
{
    (void)game;
}

static bool
stub_world_clip_is_builtin_widget(struct GameRunescape const* game)
{
    if( !game )
        return false;

    int const vw = game->view_port ? game->view_port->width : 765;
    int const vh = game->view_port ? game->view_port->height : 503;
    int const clip_w = game->world_view_port.clip_right - game->world_view_port.clip_left;
    int const clip_h = game->world_view_port.clip_bottom - game->world_view_port.clip_top;

    if( clip_w <= 0 || clip_h <= 0 )
        return false;

    return game->world_view_port.clip_left > 0 || game->world_view_port.clip_top > 0 ||
           game->world_view_port.clip_right < vw || game->world_view_port.clip_bottom < vh;
}

bool
GameRunescape_PointInMainHoverRegion(
    struct GameRunescape const* game,
    int px,
    int py)
{
    if( game && stub_world_clip_is_builtin_widget(game) )
    {
        return px >= game->world_view_port.clip_left && px < game->world_view_port.clip_right &&
               py >= game->world_view_port.clip_top && py < game->world_view_port.clip_bottom;
    }

    return px > RS_UI_HOVER_MAIN_X && py > RS_UI_HOVER_MAIN_Y &&
           px < RS_UI_HOVER_MAIN_X + RS_UI_HOVER_MAIN_W &&
           py < RS_UI_HOVER_MAIN_Y + RS_UI_HOVER_MAIN_H;
}

bool
GameRunescape_PointInSidebarHoverRegion(
    int px,
    int py)
{
    return px > RS_UI_HOVER_SIDE_X && py > RS_UI_HOVER_SIDE_Y &&
           px < RS_UI_HOVER_SIDE_X + RS_UI_HOVER_SIDE_W &&
           py < RS_UI_HOVER_SIDE_Y + RS_UI_HOVER_SIDE_H;
}

bool
GameRunescape_PointInChatHoverRegion(
    int px,
    int py)
{
    return px > RS_UI_HOVER_CHAT_X && py > RS_UI_HOVER_CHAT_Y &&
           px < RS_UI_HOVER_CHAT_X + RS_UI_HOVER_CHAT_W &&
           py < RS_UI_HOVER_CHAT_Y + RS_UI_HOVER_CHAT_H;
}

int32_t
GameRunescape_UISelectedSidebarIndex(struct GameRunescape const* game)
{
    if( !game || !game->ui_tree )
        return -1;

    int const tab = game->selected_tab;
    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &game->ui_tree->components[i];
        if( c->type == UIELEM_BUILTIN_SIDEBAR && c->u.sidebar.tabno == tab )
            return (int32_t)i;
    }
    return -1;
}

int32_t
GameRunescape_UITreeIndexForComponentId(
    struct GameRunescape const* game,
    int component_id)
{
    if( !game || !game->ui_tree || component_id < 0 )
        return -1;

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        if( game->ui_tree->components[i].component_id == component_id )
            return (int32_t)i;
    }
    return -1;
}

static void
stub_cs2_enqueue(
    void* ud,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count)
{
    struct GameRunescape* game = ud;
    if( !game )
        return;
    (void)GameRunescape_CS2QueueEnqueue(
        &game->cs2_queue, script_id, component_id, int_args, int_arg_count, NULL, 0);
}

static void
stub_init_cs2_host(struct GameRunescape* game)
{
    if( !game )
        return;
    if( game->cs2_vm_bound )
        return;
    GameRunescape_CS2HostInit(&game->cs2_host, game);
    GameRunescape_CS2QueueInit(&game->cs2_queue);
    memset(&game->cs2vm, 0, sizeof(game->cs2vm));
    CS2VMX_BindHost(&game->cs2vm, &game->cs2_host, GameRunescape_CS2HostExec);
    game->cs2_vm_bound = true;
}

static void
stub_build_behavior_host(
    struct GameRunescape* game,
    struct UITreeBehaviorHost* out)
{
    if( !game || !out )
        return;
    stub_init_cs2_host(game);
    ui_input_adapter_init_behavior_host_ex(out, game->vm, stub_cs2_enqueue, game);
}

static void
stub_run_hooks(
    struct GameRunescape* game,
    enum UITreeBehaviorHookKind hook_kind)
{
    if( !game || !game->core || !game->cs2_vm_bound || !game->ui_tree )
        return;

    struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
    struct UITreeBehaviorHost host;
    stub_build_behavior_host(game, &host);

    for( uint32_t i = 0; i < game->ui_tree->component_count; i++ )
    {
        struct StaticUIComponent const* node = &game->ui_tree->components[i];
        if( node->component_id < 0 )
            continue;
        struct ToriAuxLibCore_Component* core_comp =
            ToriAuxLibCore_ComponentGet(game->core, node->component_id);
        if( !core_comp )
            continue;
        uitree_behavior_run_hook(&host, game->core, cache, core_comp, hook_kind);
    }

    uitree_mark_all_dirty(game->ui_tree);
}

void
GameRunescape_DispatchInvTransmit(
    struct GameRunescape* game,
    int container_id)
{
    if( !game || !game->core || !game->cs2_vm_bound || !game->ui_tree ||
        game->ui_tree->component_count == 0 || container_id < 0 )
        return;

    struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
    struct UITreeBehaviorHost host;
    stub_build_behavior_host(game, &host);
    uitree_behavior_dispatch_inv_transmit(&host, game->core, cache, game->ui_tree, container_id);

    if( game->ui_tree )
        uitree_mark_all_dirty(game->ui_tree);
}

void
GameRunescape_RunOnLoadHooks(struct GameRunescape* game)
{
    if( !game || game->ui_on_load_hooks_ran || !game->core || !game->cs2_vm_bound || !game->ui_tree ||
        game->ui_tree->component_count == 0 )
        return;

    stub_run_hooks(game, UITREE_BEHAVIOR_HOOK_ON_LOAD);
    game->ui_on_load_hooks_ran = true;
}
