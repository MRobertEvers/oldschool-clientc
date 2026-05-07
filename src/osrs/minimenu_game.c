#include "minimenu_game.h"

#include <assert.h>

#include "collision_map.h"
#include "game.h"
#include "osrs/chat.h"
#include "minimenu_action.h"
#include "osrs/buildcachedat.h"
#include "osrs/gamenet_send.h"
#include "osrs/game_entity.h"
#include "osrs/ginput.h"
#include "osrs/interface_state.h"
#include "osrs/world.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "revconfig/uitree.h"
#include "world_option_set.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** Client.ts TGT_BUTTON handler: targetOp = prefix + ' ' + targetBase + ' ' + suffix from verb. */
static void
iface_inv_build_target_op_full(
    char* out,
    size_t cap,
    char const* verb,
    char const* base)
{
    char pre[64]  = "";
    char suff[64] = "";
    if( verb && verb[0] )
    {
        char const* sp = strchr(verb, ' ');
        if( sp )
        {
            size_t n = (size_t)(sp - verb);
            if( n > sizeof(pre) - 1 )
                n = sizeof(pre) - 1;
            memcpy(pre, verb, n);
            pre[n] = '\0';
            strncpy(suff, sp + 1, sizeof(suff) - 1);
            suff[sizeof(suff) - 1] = '\0';
        }
        else
            strncpy(pre, verb, sizeof(pre) - 1);
    }
    snprintf(out, cap, "%s %s %s", pre, base ? base : "", suff);
}

/* Client.ts buildMinimenu (2816-2844): bubble >1000 actions toward lower indices. */
void
minimenu_sort_ts_action_order(
    struct MinimenuOptionLine* lines,
    int n)
{
    for( ;; )
    {
        int done = 1;
        for( int i = 0; i < n - 1; i++ )
        {
            if( lines[i].action < 1000 && lines[i + 1].action > 1000 )
            {
                struct MinimenuOptionLine t = lines[i];
                lines[i]         = lines[i + 1];
                lines[i + 1]     = t;
                done             = 0;
            }
        }
        if( done )
            break;
    }
}

/* Build [Cancel, ...world options] in TS array order (index 0 = bottom when drawn like Client.ts). */
static int
minimenu_fill_world_ts_menu_lines(
    struct WorldOptionSet* os,
    int mouse_tile_x,
    int mouse_tile_z,
    struct MinimenuOptionLine* tmp,
    int max_tmp)
{
    if( !os || !tmp || max_tmp < 1 )
        return 0;

    int wn = os->option_count;
    if( wn > max_tmp - 1 )
        wn = max_tmp - 1;

    tmp[0].text    = "Cancel";
    tmp[0].action  = (int)MINIMENU_ACTION_CANCEL;
    tmp[0].param_a = 0;
    tmp[0].param_b = 0;
    tmp[0].param_c = 0;

    for( int i = 0; i < wn; i++ )
    {
        struct WorldOption* opt = world_option_set_get_option(os, i);
        tmp[i + 1].text    = opt->text;
        tmp[i + 1].action  = (int)opt->action;
        tmp[i + 1].param_a = opt->param_a;
        tmp[i + 1].param_b = opt->param_b;
        tmp[i + 1].param_c = opt->param_c;
    }

    int nlines = wn + 1;
    minimenu_sort_ts_action_order(tmp, nlines);

    for( int i = 0; i < nlines; i++ )
    {
        if( tmp[i].action == (int)MINIMENU_ACTION_WALK )
        {
            tmp[i].param_a = mouse_tile_x;
            tmp[i].param_b = mouse_tile_z;
            break;
        }
    }

    return nlines;
}

static const uint8_t*
minimenu_game_pool_text(
    void* user,
    const char* utf8)
{
    struct UITree* ui = (struct UITree*)user;
    if( !utf8 || !ui )
        return NULL;
    size_t len = strlen(utf8);
    char* p    = uitree_textpool_push_dup(&ui->text_pool, utf8, len);
    return p ? (const uint8_t*)p : NULL;
}

static void
mm_tor_emit_rect(
    struct ToriRSRenderCommandBuffer* buf,
    struct MinimenuRenderCommand const* cmd)
{
    if( !buf || !cmd || cmd->kind != MINIMENU_RENDER_COMMAND_RECT )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind                 = TORIRS_GFX_DRAW_RECT;
    c->_rect_draw.x         = cmd->u.rect.x;
    c->_rect_draw.y         = cmd->u.rect.y;
    c->_rect_draw.w         = cmd->u.rect.w;
    c->_rect_draw.h         = cmd->u.rect.h;
    c->_rect_draw.color_rgb = cmd->u.rect.color_rgb;
    c->_rect_draw.alpha     = 0;
    c->_rect_draw.fill      = cmd->u.rect.fill;
}

static void
mm_tor_emit_text(
    struct ToriRSRenderCommandBuffer* buf,
    int font_id,
    struct DashPixFont* font,
    const uint8_t* text,
    int x,
    int y,
    int color_rgb,
    int shadowed)
{
    if( !buf || !font || !text )
        return;
    /* Client.ts drawString / drawStringTag pen_y; TORIRS_GFX_DRAW_FONT expects pen_y - height2d
     * (see rev_245_2_ui.ini, uielem_hover_tooltip_step, rs_component_gfx). */
    int draw_y = y;
    if( font->height2d > 0 )
        draw_y -= font->height2d;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind                 = TORIRS_GFX_DRAW_FONT;
    c->_font_draw.font_id   = font_id;
    c->_font_draw.font      = font;
    c->_font_draw.text      = text;
    c->_font_draw.x         = x;
    c->_font_draw.y         = draw_y;
    c->_font_draw.color_rgb = color_rgb;
    c->_font_draw.shadowed  = shadowed ? 1u : 0u;
}

void
minimenu_translate_commands(
    struct MinimenuRenderCommandBuffer* cb,
    struct MinimenuState const* mm,
    void* pool_user,
    MinimenuPoolTextFn pool_text,
    struct ToriRSRenderCommandBuffer* out,
    int font_id,
    struct DashPixFont* font,
    int mouse_x,
    int mouse_y)
{
    if( !cb || !mm || !out || cb->count <= 0 || !font || !pool_text )
        return;
    (void)font_id;

    int mx = mouse_x;
    int my = mouse_y;

    for( int i = 0; i < cb->count; i++ )
    {
        struct MinimenuRenderCommand* cmd = &cb->commands[i];
        switch( cmd->kind )
        {
        case MINIMENU_RENDER_COMMAND_RECT:
            mm_tor_emit_rect(out, cmd);
            break;
        case MINIMENU_RENDER_COMMAND_TEXT_HEADER:
            mm_tor_emit_text(
                out,
                cmd->u.text_header.font_id,
                font,
                cmd->u.text_header.text,
                cmd->u.text_header.x,
                cmd->u.text_header.y,
                cmd->u.text_header.color_rgb,
                0);
            break;
        case MINIMENU_RENDER_COMMAND_TEXT_OPTION:
        {
            int opt_i = cmd->u.text_option.option_index;
            if( opt_i < 0 || opt_i >= mm->option_count )
                break;
            int ox = mm->origin_x;
            (void)mm->origin_y;
            int lx    = mx - ox;
            int mx0   = mm->x - ox;
            int opt_y = mm->y + opt_i * MINIMENU_ROW_HEIGHT + 31;
            int hovered =
                lx > mx0 && lx < mx0 + mm->width && my > opt_y - 13 && my < opt_y + 3;
            int text_color = hovered ? 0xFFFF00 : 0xFFFFFF;
            const uint8_t* opt_text =
                pool_text(pool_user, (const char*)mm->options[opt_i]);
            if( !opt_text )
                break;
            mm_tor_emit_text(
                out,
                cmd->u.text_option.font_id,
                font,
                opt_text,
                cmd->u.text_option.x,
                cmd->u.text_option.y,
                text_color,
                1);
        }
        break;
        }
    }
}

void
minimenu_game_world_ts_default_row(
    struct WorldOptionSet const* os,
    int mouse_tile_x,
    int mouse_tile_z,
    struct WorldOption* out)
{
    if( !os || !out )
        return;

    struct MinimenuOptionLine tmp[MINIMENU_MAX_OPTIONS];
    int nlines = minimenu_fill_world_ts_menu_lines(
        (struct WorldOptionSet*)os,
        mouse_tile_x,
        mouse_tile_z,
        tmp,
        MINIMENU_MAX_OPTIONS);

    memset(out, 0, sizeof(*out));
    if( nlines <= 0 )
        return;

    struct MinimenuOptionLine* top = &tmp[nlines - 1];
    const char* t = top->text ? top->text : "";
    strncpy(out->text, t, sizeof(out->text) - 1);
    out->text[sizeof(out->text) - 1] = '\0';
    out->action  = (enum MinimenuAction)top->action;
    out->param_a = top->param_a;
    out->param_b = top->param_b;
    out->param_c = top->param_c;
}

int
minimenu_game_collect_lines(
    struct GGame* game,
    struct MinimenuOptionLine* lines_out,
    int max_lines)
{
    if( !game || !lines_out || max_lines <= 0 )
        return 0;

    struct UITree* uit = game->ui_root_buffer;
    int use_uitree     = uit && uit->uitree_optionset.option_count > 0;

    if( use_uitree )
    {
        struct MinimenuOptionLine tmp[MINIMENU_MAX_OPTIONS];
        int uin = uit->uitree_optionset.option_count;
        if( uin >= MINIMENU_MAX_OPTIONS - 1 )
            uin = MINIMENU_MAX_OPTIONS - 1;

        tmp[0].text    = "Cancel";
        tmp[0].action  = (int)MINIMENU_ACTION_CANCEL;
        tmp[0].param_a = 0;
        tmp[0].param_b = 0;
        tmp[0].param_c = 0;

        for( int i = 0; i < uin; i++ )
        {
            struct UITreeOption* o = uitree_option_set_get_option(&uit->uitree_optionset, i);
            tmp[i + 1].text    = o->text;
            tmp[i + 1].action  = (int)o->action;
            tmp[i + 1].param_a = o->param_a;
            tmp[i + 1].param_b = o->param_b;
            tmp[i + 1].param_c = o->param_c;
        }

        int nlines = uin + 1;
        minimenu_sort_ts_action_order(tmp, nlines);
        if( nlines > max_lines )
            return 0;
        for( int i = 0; i < nlines; i++ )
            lines_out[i] = tmp[nlines - 1 - i];
        return nlines;
    }

    struct MinimenuOptionLine tmp[MINIMENU_MAX_OPTIONS];
    struct WorldOptionSet* os = &game->option_set;
    int nlines                  = minimenu_fill_world_ts_menu_lines(
        os,
        game->mouse_tile_x,
        game->mouse_tile_z,
        tmp,
        MINIMENU_MAX_OPTIONS);
    if( nlines > max_lines )
        return 0;
    for( int i = 0; i < nlines; i++ )
        lines_out[i] = tmp[nlines - 1 - i];
    return nlines;
}

int
minimenu_game_use_primary_at(
    struct GGame* game,
    struct GInput* input,
    int cx,
    int cy)
{
    if( !game || !input )
        return 0;
    uitree_sync_hover_option_set(game, cx, cy);
    struct MinimenuOptionLine lines[MINIMENU_MAX_OPTIONS];
    int nlines = minimenu_game_collect_lines(game, lines, MINIMENU_MAX_OPTIONS);
    if( nlines <= 0 )
        return 0;
    /* Index 0 = top row = Client.ts doAction(menuNumEntries - 1). */
    game->minimenu.option_action[0]  = lines[0].action;
    game->minimenu.option_param_a[0] = lines[0].param_a;
    game->minimenu.option_param_b[0] = lines[0].param_b;
    game->minimenu.option_param_c[0] = lines[0].param_c;
    game->minimenu.option_count      = 1;
    minimenu_game_use_option(game, input, 0);
    return 1;
}

void
minimenu_game_show(
    struct GGame* game,
    int click_x,
    int click_y)
{
    if( !game )
        return;

    uitree_sync_hover_option_set(game, click_x, click_y);

    int vp_w = game->iface_view_port ? game->iface_view_port->width  : 765;
    int vp_h = game->iface_view_port ? game->iface_view_port->height : 503;

    struct MinimenuOptionLine lines[MINIMENU_MAX_OPTIONS];
    int nlines = minimenu_game_collect_lines(game, lines, MINIMENU_MAX_OPTIONS);
    if( nlines <= 0 )
        return;

    minimenu_show(
        &game->minimenu,
        lines,
        nlines,
        vp_w,
        vp_h,
        &game->minimenu_regions,
        click_x,
        click_y);
}

void
minimenu_game_enqueue(
    struct GGame* game,
    int font_id)
{
    if( !game )
        return;
    minimenu_enqueue(
        game->minimenu_commands,
        &game->minimenu,
        game->ui_root_buffer,
        minimenu_game_pool_text,
        font_id);
}

void
minimenu_game_translate_commands(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* out,
    int font_id,
    struct DashPixFont* font)
{
    if( !game || !out )
        return;

    struct DashPixFont* draw_font = font;
    if( !draw_font && game->ui_scene && font_id >= 0 )
        draw_font = uiscene_font_get(game->ui_scene, font_id);
    if( !draw_font )
        return;

    minimenu_translate_commands(
        game->minimenu_commands,
        &game->minimenu,
        game->ui_root_buffer,
        minimenu_game_pool_text,
        out,
        font_id,
        draw_font,
        game->mouse.cursor_x,
        game->mouse.cursor_y);
}

int
minimenu_game_click_option(
    struct GGame* game,
    int click_x,
    int click_y)
{
    if( !game )
        return -1;
    return minimenu_click_option(&game->minimenu, click_x, click_y);
}

#define MM_BFS_PATH_MAX 25

/** Shared tile-to-tile BFS → MOVE_OPCLICK + suppress next MOVE_GAMECLICK (Client.ts tryMove type 2). */
static bool
mm_emit_move_opclick_simple(
    struct GGame* game,
    struct CollisionMap* cm,
    int run,
    int sx,
    int sz,
    int dx,
    int dz)
{
    if( !game || !cm )
        return false;
    if( game->net_state != GAME_NET_STATE_GAME || !game->random_out )
        return false;

    struct PlayerEntity* pl =
        game->world ? world_player(game->world, ACTIVE_PLAYER_SLOT) : NULL;
    if( !pl || !pl->alive )
        return false;

    if( dx < 0 || dz < 0 || dx >= cm->size_x || dz >= cm->size_z )
        return false;

    int path_x[MM_BFS_PATH_MAX];
    int path_z[MM_BFS_PATH_MAX];
    int n = collision_map_bfs_path(cm, sx, sz, dx, dz, path_x, path_z, MM_BFS_PATH_MAX);
    if( n <= 0 )
        return false;

    {
        int save = n < GAME_SENT_PATH_MAX ? n : GAME_SENT_PATH_MAX;
        game->sent_path.length = save;
        for( int i = 0; i < save; i++ )
        {
            game->sent_path.tile_x[i] = (uint8_t)path_x[i];
            game->sent_path.tile_z[i] = (uint8_t)path_z[i];
        }
    }

    gamenet_send_move_opclick(game, run, path_x, path_z, n);
    game->suppress_move_gameclick_once = 1;
    return true;
}

/** Tile-to-tile BFS → MOVE_GAMECLICK (no suppress flag). */
static bool
mm_emit_move_gameclick_simple(
    struct GGame* game,
    struct CollisionMap* cm,
    int run,
    int sx,
    int sz,
    int dx,
    int dz)
{
    if( !game || !cm )
        return false;
    if( game->net_state != GAME_NET_STATE_GAME || !game->random_out )
        return false;

    struct PlayerEntity* pl =
        game->world ? world_player(game->world, ACTIVE_PLAYER_SLOT) : NULL;
    if( !pl || !pl->alive )
        return false;

    if( dx < 0 || dz < 0 || dx >= cm->size_x || dz >= cm->size_z )
        return false;

    int path_x[MM_BFS_PATH_MAX];
    int path_z[MM_BFS_PATH_MAX];
    int n = collision_map_bfs_path(cm, sx, sz, dx, dz, path_x, path_z, MM_BFS_PATH_MAX);
    if( n <= 0 )
        return false;

    {
        int save = n < GAME_SENT_PATH_MAX ? n : GAME_SENT_PATH_MAX;
        game->sent_path.length = save;
        for( int i = 0; i < save; i++ )
        {
            game->sent_path.tile_x[i] = (uint8_t)path_x[i];
            game->sent_path.tile_z[i] = (uint8_t)path_z[i];
        }
    }

    gamenet_send_move_gameclick(game, run, path_x, path_z, n);
    return true;
}

static int
mm_input_run(struct GInput* input)
{
    if( input && game_input_keydown_or_pressed(input, TORIRSK_SHIFT) )
        return 1;
    return 0;
}

/** Client.ts tryMove type=2 before OP*: emit MOVE_OPCLICK; set suppress for next `game_try_move`. */
static void
mm_emit_opclick_to_tile(
    struct GGame* game,
    struct GInput* input,
    int dst_x,
    int dst_z)
{
    if( !game->world || !game->world->collision_map )
        return;

    struct PlayerEntity* pl = world_player(game->world, ACTIVE_PLAYER_SLOT);
    if( !pl || !pl->alive )
        return;

    /* Client.ts: srcX = localPlayer.routeX[0] */
    int sx = pl->pathing.route_x[0];
    int sz = pl->pathing.route_z[0];
    (void)mm_emit_move_opclick_simple(
        game, game->world->collision_map, mm_input_run(input), sx, sz, dst_x, dst_z);
}

/** Client.ts tryMove(..., entity.routeX[0], routeZ[0], false, 1, 1, 0, 0, 0, 2): MOVE_OPCLICK
 * with testLoc 1x1 arrival (adjacent or on footprint at route tile). */
static void
mm_emit_opclick_to_entity_footprint(
    struct GGame* game,
    struct GInput* input,
    int dst_route_x,
    int dst_route_z,
    int footprint_w,
    int footprint_h)
{
    if( !game->world || !game->world->collision_map )
        return;
    if( game->net_state != GAME_NET_STATE_GAME || !game->random_out )
        return;

    struct PlayerEntity* pl = world_player(game->world, ACTIVE_PLAYER_SLOT);
    if( !pl || !pl->alive )
        return;

    struct CollisionMap* cm = game->world->collision_map;
    if( dst_route_x < 0 || dst_route_z < 0 || dst_route_x >= cm->size_x ||
        dst_route_z >= cm->size_z )
        return;

    int sx = pl->pathing.route_x[0];
    int sz = pl->pathing.route_z[0];
    int path_x[MM_BFS_PATH_MAX];
    int path_z[MM_BFS_PATH_MAX];
    int fw = footprint_w > 0 ? footprint_w : 1;
    int fh = footprint_h > 0 ? footprint_h : 1;
    int n = collision_map_bfs_path_entity(
        cm, sx, sz, dst_route_x, dst_route_z, fw, fh, path_x, path_z, MM_BFS_PATH_MAX);
    if( n <= 0 )
        return;

    {
        int save = n < GAME_SENT_PATH_MAX ? n : GAME_SENT_PATH_MAX;
        game->sent_path.length = save;
        for( int i = 0; i < save; i++ )
        {
            game->sent_path.tile_x[i] = (uint8_t)path_x[i];
            game->sent_path.tile_z[i] = (uint8_t)path_z[i];
        }
    }

    gamenet_send_move_opclick(game, mm_input_run(input), path_x, path_z, n);
    game->suppress_move_gameclick_once = 1;
}

/** Client.ts interactWithLoc tryMove: loc-aware MOVE_OPCLICK using collision_map_bfs_path_loc.
 * For SCENERY / FLOOR_DECORATION shapes the BFS accepts arrival at any unblocked adjacent tile
 * of the loc footprint (testLoc, forceapproach=0). Other shapes use exact-tile fallback. */
static void
mm_emit_opclick_to_loc(
    struct GGame* game,
    struct GInput* input,
    const struct MapBuildLocEntity* loc,
    int dst_x,
    int dst_z)
{
    if( !game->world || !game->world->collision_map )
        return;
    if( game->net_state != GAME_NET_STATE_GAME || !game->random_out )
        return;

    struct PlayerEntity* pl = world_player(game->world, ACTIVE_PLAYER_SLOT);
    if( !pl || !pl->alive )
        return;

    int sx = pl->pathing.route_x[0];
    int sz = pl->pathing.route_z[0];
    int path_x[MM_BFS_PATH_MAX];
    int path_z[MM_BFS_PATH_MAX];
    int loc_w = loc ? (int)loc->place_size_x : 1;
    int loc_h = loc ? (int)loc->place_size_z : 1;
    int shape = loc ? (int)loc->shape_select : 0;
    int n = collision_map_bfs_path_loc(
        game->world->collision_map,
        sx,
        sz,
        dst_x,
        dst_z,
        shape,
        loc_w > 0 ? loc_w : 1,
        loc_h > 0 ? loc_h : 1,
        path_x,
        path_z,
        MM_BFS_PATH_MAX);
    if( n <= 0 )
        return;

    {
        int save = n < GAME_SENT_PATH_MAX ? n : GAME_SENT_PATH_MAX;
        game->sent_path.length = save;
        for( int i = 0; i < save; i++ )
        {
            game->sent_path.tile_x[i] = (uint8_t)path_x[i];
            game->sent_path.tile_z[i] = (uint8_t)path_z[i];
        }
    }

    gamenet_send_move_opclick(game, mm_input_run(input), path_x, path_z, n);
    game->suppress_move_gameclick_once = 1;
}

/* Move toward entity at (tile_x, tile_z) with the "interact" (red) cross. */
static void
mm_move_toward(
    struct GGame* game,
    int tile_x,
    int tile_z)
{
    if( !game )
        return;
    if( tile_x <= 0 && tile_z <= 0 )
        return;
    game->tile_clicked_x     = tile_x;
    game->tile_clicked_z     = tile_z;
    game->tile_clicked_level = 0;
    game->cross_mode         = 2;
    /*
     * Anchor the crosshair to the right-click press position (where the context menu opened).
     * This is correct: the user right-clicked an entity, the menu appeared, and "Walk here" /
     * interact was chosen — the cross should mark where the right-click was, not a stale coord.
     */
    game->cross_x     = game->mouse.buttons[TORIRSM_RIGHT].press_x;
    game->cross_y     = game->mouse.buttons[TORIRSM_RIGHT].press_y;
    game->cross_cycle = 0;
}

/** Mirror Client.ts `doAction`: strip priority (+2000) before opcode dispatch. */
static int
mm_menu_action_norm(int action)
{
    if( action >= (int)MINIMENU_ACTION_PRIORITY_OFFSET )
        return action - (int)MINIMENU_ACTION_PRIORITY_OFFSET;
    return action;
}

/** Client.ts held triple on OPLOCU / OPNPCU / OPOBJU / OPPLAYERU (objComId, slot, selComId). */
static void
mm_held_iface_triple(struct InterfaceState* iface, int* obj_comp, int* slot, int* sel_comp)
{
    int root = 0;
    if( iface )
    {
        root = iface->sidebar_interface_id ? iface->sidebar_interface_id
                                           : iface->viewport_interface_id;
    }
    if( obj_comp )
        *obj_comp = root;
    if( slot )
        *slot = iface ? iface->inv_sel_slot : 0;
    if( sel_comp )
        *sel_comp = iface ? iface->inv_sel_comp_id : 0;
}

/** Client passes full scene typecode in param_a when using item on loc/obj from world pick. */
static int
mm_loc_obj_wire_id(struct GGame* game, int param_a)
{
    if( game && game->iface && game->iface->inv_use_mode && param_a >= (1 << 14) )
        return (param_a >> 14) & 0x7fff;
    return param_a;
}

void
minimenu_game_use_option(
    struct GGame* game,
    struct GInput* input,
    int option_index)
{
    if( !game )
        return;
    if( option_index < 0 || option_index >= game->minimenu.option_count )
        return;

    game->minimenu.visible = 0;

    int action  = game->minimenu.option_action[option_index];
    int param_a = game->minimenu.option_param_a[option_index];
    int param_b = game->minimenu.option_param_b[option_index];
    int param_c = game->minimenu.option_param_c[option_index];
    int base    = mm_menu_action_norm(action);

    if( base == (int)MINIMENU_ACTION_CANCEL )
        return;

    if( base == (int)MINIMENU_ACTION_OPHELDT_START )
    {
        if( !game->iface )
            return;
        game->iface->inv_use_mode    = 1;
        game->iface->inv_target_mode = 0;
        /* Client.ts addComponentOptions: menuParamA=obj.id, B=slot, C=child.id (USE line). */
        game->iface->inv_sel_obj_id  = param_a;
        game->iface->inv_sel_slot    = param_b;
        game->iface->inv_sel_comp_id = param_c;
        struct GameCacheObj* o =
            game->gamecache ? gamecache_get_obj(game->gamecache, param_a) : NULL;
        snprintf(
            game->iface->inv_sel_obj_name,
            sizeof(game->iface->inv_sel_obj_name),
            "%s",
            (o && o->name) ? o->name : "item");
        return;
    }

    if( base == (int)MINIMENU_ACTION_TGT_BUTTON ||
        base == (int)MINIMENU_ACTION_OPHELDT_SELECT )
    {
        if( !game->iface || !game->gamecache )
            return;
        int comp = param_c;
        struct GameCacheComponent* cc =
            gamecache_get_component(game->gamecache, comp);
        game->iface->inv_target_mode        = 1;
        game->iface->inv_use_mode           = 0;
        game->iface->inv_target_src_comp_id = comp;
        game->iface->inv_target_mask        = cc ? cc->targetMask : -1;
        iface_inv_build_target_op_full(
            game->iface->inv_target_op,
            sizeof(game->iface->inv_target_op),
            cc ? cc->targetVerb : NULL,
            cc ? cc->targetText : NULL);
        return;
    }

    if( base == (int)MINIMENU_ACTION_USEHELD_ONHELD ||
        base == (int)MINIMENU_ACTION_OPHELDU )
    {
        if( !game->iface || GAME_NET_STATE_GAME != game->net_state )
            return;
        /* menuParam: obj, slot, comp (Client.ts; matches uitree_opt_append for held actions). */
        gamenet_send_opheldu(game,
            param_a, param_b, param_c,
            game->iface->inv_sel_obj_id,
            game->iface->inv_sel_slot,
            game->iface->inv_sel_comp_id);
        game->iface->inv_use_mode = 0;
        return;
    }

    if( base == (int)MINIMENU_ACTION_TGT_HELD )
    {
        if( !game->iface || GAME_NET_STATE_GAME != game->net_state )
            return;
        gamenet_send_opheldt(game,
            param_a, param_b, param_c, game->iface->inv_target_src_comp_id);
        game->iface->inv_target_mode = 0;
        return;
    }

    if( base == (int)MINIMENU_ACTION_WALK )
    {
        int tx  = game->mouse_tile_x;
        int tz  = game->mouse_tile_z;
        int lvl = (game->mouse_tile_level >= 0) ? game->mouse_tile_level : 0;
        if( tx < 0 || tz < 0 )
            return;
        game->tile_clicked_x     = tx;
        game->tile_clicked_z     = tz;
        game->tile_clicked_level = lvl;
        /*
         * "Walk here" from the context menu: anchor the cross to where the right-click opened
         * the menu, not the stale mouse_clicked_right_x/y that never resets.
         */
        game->cross_x     = game->mouse.buttons[TORIRSM_RIGHT].press_x;
        game->cross_y     = game->mouse.buttons[TORIRSM_RIGHT].press_y;
        game->cross_mode  = 1;
        game->cross_cycle = 0;
        return;
    }

    {
        int which = -1;
        if( base == (int)MINIMENU_ACTION_OPNPC1 )
            which = 1;
        else if( base == (int)MINIMENU_ACTION_OPNPC2 )
            which = 2;
        else if( base == (int)MINIMENU_ACTION_OPNPC3 )
            which = 3;
        else if( base == (int)MINIMENU_ACTION_OPNPC4 )
            which = 4;
        else if( base == (int)MINIMENU_ACTION_OPNPC5 )
            which = 5;
        if( which >= 0 )
        {
            int tx = param_b;
            int tz = param_c;
            struct NPCEntity* npc = world_npc(game->world, param_a);
            struct CollisionMap* cm =
                game->world ? game->world->collision_map : NULL;
            if( npc && npc->alive && cm )
            {
                tx = (int)npc->pathing.route_x[0];
                tz = (int)npc->pathing.route_z[0];
                if( tx < 0 || tz < 0 || tx >= cm->size_x || tz >= cm->size_z )
                {
                    tx = npc->draw_position.x >> 7;
                    tz = npc->draw_position.z >> 7;
                }
            }
            mm_emit_opclick_to_entity_footprint(game, input, tx, tz, 1, 1);
            gamenet_send_opnpc(game, which, param_a);
            mm_move_toward(game, tx, tz);
            return;
        }
    }

    if( base == (int)MINIMENU_ACTION_OPNPC6 && game->chat )
    {
        struct NPCEntity* npc = world_npc(game->world, param_a);
        char buf[320];
        if( npc && npc->name )
        {
            if( npc->description && npc->description[0] )
                snprintf(buf, sizeof(buf), "%s", npc->description);
            else
                snprintf(buf, sizeof(buf), "It's a %s.", npc->name);
            chat_add(game->chat, game, 0, "", buf);
        }
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPNPCT )
    {
        struct NPCEntity* npc = world_npc(game->world, param_a);
        struct CollisionMap* cm =
            game->world ? game->world->collision_map : NULL;
        if( npc && npc->alive && game->iface && cm &&
            GAME_NET_STATE_GAME == game->net_state )
        {
            int tx = (int)npc->pathing.route_x[0];
            int tz = (int)npc->pathing.route_z[0];
            if( tx < 0 || tz < 0 || tx >= cm->size_x || tz >= cm->size_z )
            {
                tx = npc->draw_position.x >> 7;
                tz = npc->draw_position.z >> 7;
            }
            mm_emit_opclick_to_entity_footprint(game, input, tx, tz, 1, 1);
            gamenet_send_opnpct_target(
                game, param_a, game->iface->inv_target_src_comp_id);
            mm_move_toward(game, tx, tz);
            game->iface->inv_target_mode = 0;
        }
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPNPCU )
    {
        if( !game->iface || GAME_NET_STATE_GAME != game->net_state )
            return;
        int oc = 0, sl = 0, sc = 0;
        mm_held_iface_triple(game->iface, &oc, &sl, &sc);
        struct NPCEntity* npc = world_npc(game->world, param_a);
        struct CollisionMap* cm =
            game->world ? game->world->collision_map : NULL;
        if( npc && npc->alive && cm )
        {
            int tx = (int)npc->pathing.route_x[0];
            int tz = (int)npc->pathing.route_z[0];
            if( tx < 0 || tz < 0 || tx >= cm->size_x || tz >= cm->size_z )
            {
                tx = npc->draw_position.x >> 7;
                tz = npc->draw_position.z >> 7;
            }
            mm_emit_opclick_to_entity_footprint(game, input, tx, tz, 1, 1);
            mm_move_toward(game, tx, tz);
        }
        gamenet_send_opnpcu_use(game, param_a, oc, sl, sc);
        game->iface->inv_use_mode = 0;
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPLOC6 && game->chat && game->world )
    {
        struct MapBuildLocEntity* loc = world_loc_entity(game->world, param_a);
        char buf[320];
        if( loc && loc->name.name[0] )
        {
            if( loc->description.description[0] )
                snprintf(buf, sizeof(buf), "%s", loc->description.description);
            else
                snprintf(buf, sizeof(buf), "It's a %s.", loc->name.name);
            chat_add(game->chat, game, 0, "", buf);
        }
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPLOCT && game->iface &&
        GAME_NET_STATE_GAME == game->net_state && game->world )
    {
        if( param_a < 0 || param_a >= entity_vec_count(&game->world->map_build_loc_entities) )
            return;
        struct MapBuildLocEntity* loc = world_loc_entity(game->world, param_a);
        int lid = loc ? loc->loc_type_id : mm_loc_obj_wire_id(game, param_a);
        mm_emit_opclick_to_loc(game, input, loc, param_b, param_c);
        gamenet_send_oploct_target(
            game,
            param_b,
            param_c,
            lid,
            0,
            game->iface->inv_target_src_comp_id);
        mm_move_toward(game, param_b, param_c);
        game->iface->inv_target_mode = 0;
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPLOCU )
    {
        if( !game->iface || GAME_NET_STATE_GAME != game->net_state || !game->world )
            return;
        if( param_a < 0 || param_a >= entity_vec_count(&game->world->map_build_loc_entities) )
            return;
        int oc = 0, sl = 0, sc = 0;
        mm_held_iface_triple(game->iface, &oc, &sl, &sc);
        struct MapBuildLocEntity* loc = world_loc_entity(game->world, param_a);
        int lid = loc ? loc->loc_type_id : mm_loc_obj_wire_id(game, param_a);
        mm_emit_opclick_to_loc(game, input, loc, param_b, param_c);
        gamenet_send_oplocu_use(game, param_b, param_c, lid, oc, sl, sc);
        mm_move_toward(game, param_b, param_c);
        game->iface->inv_use_mode = 0;
        return;
    }

    {
        int which = -1;
        if( base == (int)MINIMENU_ACTION_OPLOC1 )
            which = 1;
        else if( base == (int)MINIMENU_ACTION_OPLOC2 )
            which = 2;
        else if( base == (int)MINIMENU_ACTION_OPLOC3 )
            which = 3;
        else if( base == (int)MINIMENU_ACTION_OPLOC4 )
            which = 4;
        else if( base == (int)MINIMENU_ACTION_OPLOC5 )
            which = 5;
        if( which >= 0 && game->world )
        {
            if( param_a >= 0 &&
                param_a < entity_vec_count(&game->world->map_build_loc_entities) )
            {
                struct MapBuildLocEntity* loc = world_loc_entity(game->world, param_a);
                if( loc )
                {
                    mm_emit_opclick_to_loc(game, input, loc, param_b, param_c);
                    gamenet_send_oploc(game, which, param_b, param_c, loc->loc_type_id, 0);
                    mm_move_toward(game, param_b, param_c);
                }
            }
            return;
        }
    }

    if( base == (int)MINIMENU_ACTION_OPOBJ6 && game->chat && game->gamecache )
    {
        struct GameCacheObj* o = gamecache_get_obj(game->gamecache, param_a);
        char buf[320];
        if( o && o->name )
            snprintf(buf, sizeof(buf), "It's a %s.", o->name);
        else
            buf[0] = '\0';
        if( buf[0] )
            chat_add(game->chat, game, 0, "", buf);
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPOBJT && game->iface &&
        GAME_NET_STATE_GAME == game->net_state )
    {
        int oid = mm_loc_obj_wire_id(game, param_a);
        mm_emit_opclick_to_tile(game, input, param_b, param_c);
        gamenet_send_opobjt_spell(
            game, param_b, param_c, oid, game->iface->inv_target_src_comp_id);
        mm_move_toward(game, param_b, param_c);
        game->iface->inv_target_mode = 0;
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPOBJU )
    {
        if( !game->iface || GAME_NET_STATE_GAME != game->net_state )
            return;
        int oc = 0, sl = 0, sc = 0;
        mm_held_iface_triple(game->iface, &oc, &sl, &sc);
        int oid = mm_loc_obj_wire_id(game, param_a);
        mm_emit_opclick_to_tile(game, input, param_b, param_c);
        gamenet_send_opobju_use(game, param_b, param_c, oid, oc, sl, sc);
        mm_move_toward(game, param_b, param_c);
        game->iface->inv_use_mode = 0;
        return;
    }

    {
        int which = -1;
        if( base == (int)MINIMENU_ACTION_OPOBJ1 )
            which = 1;
        else if( base == (int)MINIMENU_ACTION_OPOBJ2 )
            which = 2;
        else if( base == (int)MINIMENU_ACTION_OPOBJ3 )
            which = 3;
        else if( base == (int)MINIMENU_ACTION_OPOBJ4 )
            which = 4;
        else if( base == (int)MINIMENU_ACTION_OPOBJ5 )
            which = 5;
        if( which >= 0 )
        {
            mm_emit_opclick_to_tile(game, input, param_b, param_c);
            gamenet_send_opobj(game, which, param_b, param_c, param_a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    if( base == (int)MINIMENU_ACTION_OPPLAYERT && game->iface &&
        GAME_NET_STATE_GAME == game->net_state )
    {
        struct PlayerEntity* tgt = world_player(game->world, param_a);
        struct CollisionMap* cm =
            game->world ? game->world->collision_map : NULL;
        if( tgt && tgt->alive && cm )
        {
            int tx = (int)tgt->pathing.route_x[0];
            int tz = (int)tgt->pathing.route_z[0];
            if( tx < 0 || tz < 0 || tx >= cm->size_x || tz >= cm->size_z )
            {
                tx = tgt->draw_position.x >> 7;
                tz = tgt->draw_position.z >> 7;
            }
            mm_emit_opclick_to_entity_footprint(game, input, tx, tz, 1, 1);
            mm_move_toward(game, tx, tz);
        }
        gamenet_send_opplayert_target(
            game, param_a, game->iface->inv_target_src_comp_id);
        game->iface->inv_target_mode = 0;
        return;
    }

    if( base == (int)MINIMENU_ACTION_OPPLAYERU )
    {
        if( !game->iface || GAME_NET_STATE_GAME != game->net_state )
            return;
        int oc = 0, sl = 0, sc = 0;
        mm_held_iface_triple(game->iface, &oc, &sl, &sc);
        struct PlayerEntity* tgt = world_player(game->world, param_a);
        struct CollisionMap* cm =
            game->world ? game->world->collision_map : NULL;
        if( tgt && tgt->alive && cm )
        {
            int tx = (int)tgt->pathing.route_x[0];
            int tz = (int)tgt->pathing.route_z[0];
            if( tx < 0 || tz < 0 || tx >= cm->size_x || tz >= cm->size_z )
            {
                tx = tgt->draw_position.x >> 7;
                tz = tgt->draw_position.z >> 7;
            }
            mm_emit_opclick_to_entity_footprint(game, input, tx, tz, 1, 1);
            mm_move_toward(game, tx, tz);
        }
        gamenet_send_opplayeru_use(game, param_a, oc, sl, sc);
        game->iface->inv_use_mode = 0;
        return;
    }

    {
        int which = -1;
        if( base == (int)MINIMENU_ACTION_OPPLAYER1 )
            which = 1;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER2 )
            which = 2;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER3 )
            which = 3;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER4 )
            which = 4;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER5 )
            which = 5;
        if( which >= 0 )
        {
            int tx = param_b;
            int tz = param_c;
            struct PlayerEntity* tgt = world_player(game->world, param_a);
            struct CollisionMap* cm =
                game->world ? game->world->collision_map : NULL;
            if( tgt && tgt->alive && cm )
            {
                tx = (int)tgt->pathing.route_x[0];
                tz = (int)tgt->pathing.route_z[0];
                if( tx < 0 || tz < 0 || tx >= cm->size_x || tz >= cm->size_z )
                {
                    tx = tgt->draw_position.x >> 7;
                    tz = tgt->draw_position.z >> 7;
                }
            }
            mm_emit_opclick_to_entity_footprint(game, input, tx, tz, 1, 1);
            gamenet_send_opplayer(game, which, param_a);
            mm_move_toward(game, tx, tz);
            return;
        }
    }

    {
        int which = -1;
        if( base == (int)MINIMENU_ACTION_INV_BUTTON1 )
            which = 1;
        else if( base == (int)MINIMENU_ACTION_INV_BUTTON2 )
            which = 2;
        else if( base == (int)MINIMENU_ACTION_INV_BUTTON3 )
            which = 3;
        else if( base == (int)MINIMENU_ACTION_INV_BUTTON4 )
            which = 4;
        else if( base == (int)MINIMENU_ACTION_INV_BUTTON5 )
            which = 5;
        if( which >= 0 )
        {
            gamenet_send_inv_button(game, which, param_c, param_b, param_a);
            return;
        }
    }

    {
        int which = -1;
        if( base == (int)MINIMENU_ACTION_OPHELD1 )
            which = 1;
        else if( base == (int)MINIMENU_ACTION_OPHELD2 )
            which = 2;
        else if( base == (int)MINIMENU_ACTION_OPHELD3 )
            which = 3;
        else if( base == (int)MINIMENU_ACTION_OPHELD4 )
            which = 4;
        else if( base == (int)MINIMENU_ACTION_OPHELD5 )
            which = 5;
        else if( base == (int)MINIMENU_ACTION_OPHELD6 )
            which = 6;
        if( which >= 1 && which <= 5 )
        {
            gamenet_send_opheld(game, which, param_a, param_b, param_c);
            return;
        }
        if( which == 6 )
        {
            /* Examine held item — optional server packet depending on revision; keep stub. */
            return;
        }
    }

    if( base == (int)MINIMENU_ACTION_IF_BUTTON ||
        base == (int)MINIMENU_ACTION_IF_BUTTON_TOGGLE ||
        base == (int)MINIMENU_ACTION_IF_BUTTON_SELECT )
    {
        gamenet_send_if_button(game, param_a);
        return;
    }

    if( base == (int)MINIMENU_ACTION_CLOSE_MODAL )
    {
        gamenet_send_close_modal(game);
        return;
    }
    if( base == (int)MINIMENU_ACTION_RESUME_PAUSEBUTTON )
    {
        gamenet_send_resume_pausebutton(game, param_a);
        return;
    }

    mm_move_toward(game, param_b, param_c);
}

bool
minimenu_game_send_move_path_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z)
{
    /* run=0; callers needing shift-run should extend API or route via minimenu + input. */
    return mm_emit_move_gameclick_simple(
        game, cm, 0, src_local_x, src_local_z, dest_local_x, dest_local_z);
}

bool
minimenu_game_send_move_opclick_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z)
{
    return mm_emit_move_opclick_simple(
        game, cm, 0, src_local_x, src_local_z, dest_local_x, dest_local_z);
}
