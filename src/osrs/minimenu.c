#include "minimenu.h"

#include "buildcachedat.h"
#include "collision_map.h"
#include "game_entity.h"
#include "graphics/dash.h"
#include "osrs/core/clientprot_core.h"
#include "tori_rs.h"
#include "world.h"
#include "world_option_set.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACTIVE_PLAYER_SLOT 2047
#define MOVE_GAMECLICK_OPCODE 182

/* Row height in pixels for menu option rows (matches Client.ts 15px per row). */
#define MENU_ROW_HEIGHT 15
/* Header height: title "Choose Option" row. */
#define MENU_HEADER_HEIGHT 18

/* Extend a string into the game's ui_frame_text_pool so TORIRS_GFX_DRAW_FONT
 * can reference it safely across the frame. Returns NULL on failure. */
static const uint8_t*
mm_pool_text(struct GGame* game, const char* text)
{
    if( !text )
        return NULL;
    int len = (int)strlen(text);
    size_t need = (size_t)len + 1;
    if( game->ui_frame_text_pool_used + need > game->ui_frame_text_pool_cap )
    {
        size_t nc = game->ui_frame_text_pool_cap ? game->ui_frame_text_pool_cap * 2 : 4096u;
        while( game->ui_frame_text_pool_used + need > nc )
            nc *= 2;
        char* nb = (char*)realloc(game->ui_frame_text_pool, nc);
        if( !nb )
            return NULL;
        game->ui_frame_text_pool     = nb;
        game->ui_frame_text_pool_cap = nc;
    }
    char* p = game->ui_frame_text_pool + game->ui_frame_text_pool_used;
    memcpy(p, text, (size_t)len + 1);
    game->ui_frame_text_pool_used += need;
    return (const uint8_t*)p;
}

/* Emit a filled or outlined rect command. */
static void
mm_emit_rect(
    struct ToriRSRenderCommandBuffer* buf,
    int x,
    int y,
    int w,
    int h,
    int color,
    uint8_t fill)
{
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind              = TORIRS_GFX_DRAW_RECT;
    c->_rect_draw.x      = x;
    c->_rect_draw.y      = y;
    c->_rect_draw.w      = w;
    c->_rect_draw.h      = h;
    c->_rect_draw.color_rgb = color;
    c->_rect_draw.alpha  = 0;
    c->_rect_draw.fill   = fill;
}

/* Emit a font draw command (text must outlive the frame — use mm_pool_text). */
static void
mm_emit_text(
    struct ToriRSRenderCommandBuffer* buf,
    int font_id,
    struct DashPixFont* font,
    const uint8_t* text,
    int x,
    int y,
    int color)
{
    if( !font || !text )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind               = TORIRS_GFX_DRAW_FONT;
    c->_font_draw.font_id  = font_id;
    c->_font_draw.font     = font;
    c->_font_draw.text     = text;
    c->_font_draw.x        = x;
    c->_font_draw.y        = y;
    c->_font_draw.color_rgb = color;
}

bool
send_move_path_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z)
{
    // int path_local_x[25];
    // int path_local_z[25];
    // int waypoints = collision_map_bfs_path(
    //     cm, src_local_x, src_local_z, dest_local_x, dest_local_z, path_local_x, path_local_z,
    //     25);
    // if( waypoints < 0 )
    //     return false;
    // if( waypoints > 0 )
    // {
    //     int buffer_size = (waypoints + 1) > 25 ? 25 : waypoints + 1;
    //     int steps_to_send = buffer_size - 1;
    //     int payload_size = 1 + 2 + 2 + steps_to_send * 2;
    //     int start_world_x = game->scene_base_tile_x + src_local_x;
    //     int start_world_z = game->scene_base_tile_z + src_local_z;
    //     if( game->outbound_size + 2 + payload_size <= (int)sizeof(game->outbound_buffer) )
    //     {
    //         uint32_t op = (MOVE_GAMECLICK_OPCODE + isaac_next(game->random_out)) & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
    //         game->outbound_buffer[game->outbound_size++] = (uint8_t)payload_size;
    //         game->outbound_buffer[game->outbound_size++] = 0;
    //         game->outbound_buffer[game->outbound_size++] = (start_world_x >> 8) & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = start_world_x & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = (start_world_z >> 8) & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = start_world_z & 0xff;
    //         for( int i = 0; i < steps_to_send && i < waypoints; i++ )
    //         {
    //             game->outbound_buffer[game->outbound_size++] =
    //                 (uint8_t)(int8_t)(path_local_x[i] - src_local_x);
    //             game->outbound_buffer[game->outbound_size++] =
    //                 (uint8_t)(int8_t)(path_local_z[i] - src_local_z);
    //         }
    //     }
    // }
    return true;
}

bool
send_move_opclick_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z)
{
    // int path_local_x[25];
    // int path_local_z[25];
    // int waypoints = collision_map_bfs_path(
    //     cm, src_local_x, src_local_z, dest_local_x, dest_local_z, path_local_x, path_local_z,
    //     25);
    // if( waypoints < 0 )
    //     return false;
    // if( waypoints > 0 )
    // {
    //     int buffer_size = (waypoints + 1) > 25 ? 25 : waypoints + 1;
    //     int steps_to_send = buffer_size - 1;
    //     int payload_size = 1 + 2 + 2 + steps_to_send * 2;
    //     int start_world_x = game->scene_base_tile_x + src_local_x;
    //     int start_world_z = game->scene_base_tile_z + src_local_z;
    //     if( game->outbound_size + 2 + payload_size <= (int)sizeof(game->outbound_buffer) )
    //     {
    //         uint32_t op = (PKTOUT_LC245_2_MOVE_OPCLICK + isaac_next(game->random_out)) & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
    //         game->outbound_buffer[game->outbound_size++] = (uint8_t)payload_size;
    //         game->outbound_buffer[game->outbound_size++] = 0;
    //         game->outbound_buffer[game->outbound_size++] = (start_world_x >> 8) & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = start_world_x & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = (start_world_z >> 8) & 0xff;
    //         game->outbound_buffer[game->outbound_size++] = start_world_z & 0xff;
    //         for( int i = 0; i < steps_to_send && i < waypoints; i++ )
    //         {
    //             game->outbound_buffer[game->outbound_size++] =
    //                 (uint8_t)(int8_t)(path_local_x[i] - src_local_x);
    //             game->outbound_buffer[game->outbound_size++] =
    //                 (uint8_t)(int8_t)(path_local_z[i] - src_local_z);
    //         }
    //     }
    // }
    return true;
}

static void
add_option(
    struct GGame* game,
    const char* text,
    int op_index)
{
    // if( game->menu_size >= MINIMENU_MAX_OPTIONS )
    //     return;
    // int n = game->menu_size;
    // snprintf(game->menu_options[n], MINIMENU_OPTION_LEN, "%s", text);
    // game->menu_option_action[n] = op_index;
    // game->menu_size++;
}

static int
string_width(
    struct DashPixFont* font,
    const char* s)
{
    if( !font || !s )
        return 0;
    return dashfont_text_width_taggable(font, (uint8_t*)s);
}

void
minimenu_show(
    struct GGame* game,
    int click_x,
    int click_y)
{
    game->minimenu_option_count = 0;
    game->minimenu_visible = 0;

    /* Populate from option_set (built by world_options_add_pickset_options at FrameEnd). */
    struct WorldOptionSet* os = &game->option_set;
    int n = os->option_count;
    if( n >= GAME_MINIMENU_MAX_OPTIONS - 1 )
        n = GAME_MINIMENU_MAX_OPTIONS - 1;

    /* Copy options in sorted order (highest-priority first = top of menu). */
    for( int i = 0; i < n; i++ )
    {
        struct WorldOption* opt = world_option_set_get_option(os, i);
        snprintf(
            game->minimenu_options[i],
            GAME_MINIMENU_OPTION_LEN,
            "%s",
            opt->text);
        game->minimenu_option_action[i]  = (int)opt->action;
        game->minimenu_option_param_a[i] = opt->param_a;
        game->minimenu_option_param_b[i] = opt->param_b;
        game->minimenu_option_param_c[i] = opt->param_c;
    }

    /* Fill Walk-here tile from mouse_tile_x/z (set each frame by the painter). */
    for( int i = 0; i < n; i++ )
    {
        if( game->minimenu_option_action[i] == (int)MINIMENU_ACTION_WALK )
        {
            game->minimenu_option_param_a[i] = game->mouse_tile_x;
            game->minimenu_option_param_b[i] = game->mouse_tile_z;
            break;
        }
    }

    /* Always append "Cancel" at the bottom. */
    snprintf(game->minimenu_options[n], GAME_MINIMENU_OPTION_LEN, "Cancel");
    game->minimenu_option_action[n]  = (int)MINIMENU_ACTION_CANCEL;
    game->minimenu_option_param_a[n] = 0;
    game->minimenu_option_param_b[n] = 0;
    game->minimenu_option_param_c[n] = 0;
    n++;

    game->minimenu_option_count = n;

    /* Estimate width from longest option text (8px/char approx). */
    int width = 120; /* minimum */
    for( int i = 0; i < n; i++ )
    {
        int w = (int)strlen(game->minimenu_options[i]) * 8 + 12;
        if( w > width )
            width = w;
    }
    int height = n * MENU_ROW_HEIGHT + MENU_HEADER_HEIGHT + 4;

    /* Viewport extents. */
    int vp_w = game->iface_view_port ? game->iface_view_port->width  : 765;
    int vp_h = game->iface_view_port ? game->iface_view_port->height : 503;

    /* Position: left-aligned to click, clamped to viewport. */
    int x = click_x - width / 2;
    if( x + width > vp_w ) x = vp_w - width;
    if( x < 0 ) x = 0;

    int y = click_y - 11;
    if( y + height > vp_h ) y = vp_h - height;
    if( y < 0 ) y = 0;

    game->minimenu_visible = 1;
    game->minimenu_x       = x;
    game->minimenu_y       = y;
    game->minimenu_width   = width;
    game->minimenu_height  = height;
}

void
minimenu_draw(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* buf,
    int font_id,
    struct DashPixFont* font)
{
    if( !game->minimenu_visible || game->minimenu_option_count <= 0 || !buf )
        return;

    int x = game->minimenu_x;
    int y = game->minimenu_y;
    int w = game->minimenu_width;
    int h = game->minimenu_height;
    int n = game->minimenu_option_count;

    /* Background colour (Client.ts OPTIONS_MENU ~= dark olive). */
    int color_bg     = 0x5F5000;
    int color_black  = 0x000000;
    int color_white  = 0xFFFFFF;
    int color_yellow = 0xFFFF00;

    /* Outer background fill. */
    mm_emit_rect(buf, x, y, w, h, color_bg, 1);
    /* Black header bar. */
    mm_emit_rect(buf, x + 1, y + 1, w - 2, MENU_HEADER_HEIGHT - 2, color_black, 1);
    /* Black body. */
    mm_emit_rect(buf, x + 1, y + MENU_HEADER_HEIGHT, w - 2, h - MENU_HEADER_HEIGHT - 1, color_black, 1);

    /* Header label. */
    const uint8_t* header_text = mm_pool_text(game, "Choose Option");
    mm_emit_text(buf, font_id, font, header_text, x + 3, y + 3, color_bg);

    /* Mouse position for hover highlight. */
    int mx = game->mouse_x;
    int my = game->mouse_y;

    /* Option rows from top to bottom (index 0 = first/top option). */
    for( int i = 0; i < n; i++ )
    {
        int row_top    = y + MENU_HEADER_HEIGHT + i * MENU_ROW_HEIGHT;
        int row_bot    = row_top + MENU_ROW_HEIGHT;
        int hovered    = (mx >= x && mx < x + w && my >= row_top && my < row_bot);
        int text_color = hovered ? color_yellow : color_white;

        const uint8_t* opt_text = mm_pool_text(game, game->minimenu_options[i]);
        mm_emit_text(buf, font_id, font, opt_text, x + 3, row_top + 2, text_color);
    }
}

int
minimenu_click_option(
    struct GGame* game,
    int click_x,
    int click_y)
{
    if( !game->minimenu_visible )
        return -1;

    int menu_x = game->minimenu_x;
    int menu_y = game->minimenu_y;
    int menu_w = game->minimenu_width;
    int n      = game->minimenu_option_count;

    /* Check each option row. */
    for( int i = 0; i < n; i++ )
    {
        int row_top = menu_y + MENU_HEADER_HEIGHT + i * MENU_ROW_HEIGHT;
        int row_bot = row_top + MENU_ROW_HEIGHT;
        if( click_x >= menu_x && click_x < menu_x + menu_w &&
            click_y >= row_top && click_y < row_bot )
        {
            return i;
        }
    }

    /* Click in the header area - do nothing, keep open. */
    if( click_x >= menu_x && click_x < menu_x + menu_w &&
        click_y >= menu_y && click_y < menu_y + MENU_HEADER_HEIGHT )
    {
        return -1;
    }

    /* Click outside menu - close it. */
    game->minimenu_visible = 0;
    return -2;
}

/* Move toward entity at (tile_x, tile_z) with the "interact" (red) cross. */
static void
mm_move_toward(struct GGame* game, int tile_x, int tile_z)
{
    if( tile_x <= 0 && tile_z <= 0 )
        return;
    game->tile_clicked_x     = tile_x;
    game->tile_clicked_z     = tile_z;
    game->tile_clicked_level = 0;
    game->cross_mode         = 2;
    game->cross_x            = game->mouse_clicked_right_x;
    game->cross_y            = game->mouse_clicked_right_y;
    game->cross_cycle        = 0;
}

void
minimenu_use_option(
    struct GGame* game,
    int option_index)
{
    if( option_index < 0 || option_index >= game->minimenu_option_count )
        return;

    game->minimenu_visible = 0;

    int action  = game->minimenu_option_action[option_index];
    int param_a = game->minimenu_option_param_a[option_index];
    int param_b = game->minimenu_option_param_b[option_index];
    int param_c = game->minimenu_option_param_c[option_index];

    if( action == (int)MINIMENU_ACTION_CANCEL )
        return;

    if( action == (int)MINIMENU_ACTION_WALK )
    {
        game->tile_clicked_x     = param_a;
        game->tile_clicked_z     = param_b;
        game->tile_clicked_level = (game->mouse_tile_level >= 0) ? game->mouse_tile_level : 0;
        game->cross_x            = game->mouse_clicked_right_x;
        game->cross_y            = game->mouse_clicked_right_y;
        game->cross_mode         = 1;
        game->cross_cycle        = 0;
        return;
    }

    /* ── NPC actions ─────────────────────────────────────────────────────────
     * world_options.c: param_a=npc_slot, param_b=x, param_c=z               */
    {
        int which = -1;
        int base  = action;
        if( base > (int)MINIMENU_ACTION_PRIORITY_OFFSET )
            base -= (int)MINIMENU_ACTION_PRIORITY_OFFSET;
        if( base == (int)MINIMENU_ACTION_OPNPC1 ) which = 1;
        else if( base == (int)MINIMENU_ACTION_OPNPC2 ) which = 2;
        else if( base == (int)MINIMENU_ACTION_OPNPC3 ) which = 3;
        else if( base == (int)MINIMENU_ACTION_OPNPC4 ) which = 4;
        else if( base == (int)MINIMENU_ACTION_OPNPC5 ) which = 5;
        if( which >= 0 )
        {
            struct CPArgs_OpNpc a = { which, param_a };
            clientprot_core_emit(game, CLIENTPROT_OP_OPNPC, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    /* ── LOC actions ─────────────────────────────────────────────────────────
     * param_a=entity_id, param_b=x, param_c=z                               */
    {
        int which = -1;
        if( action == (int)MINIMENU_ACTION_OPLOC1 ) which = 1;
        else if( action == (int)MINIMENU_ACTION_OPLOC2 ) which = 2;
        else if( action == (int)MINIMENU_ACTION_OPLOC3 ) which = 3;
        else if( action == (int)MINIMENU_ACTION_OPLOC4 ) which = 4;
        else if( action == (int)MINIMENU_ACTION_OPLOC5 ) which = 5;
        if( which >= 0 )
        {
            struct CPArgs_OpLoc a = { which, param_b, param_c, param_a, 0 };
            clientprot_core_emit(game, CLIENTPROT_OP_OPLOC, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    /* ── OBJ (ground item) actions ───────────────────────────────────────────
     * param_a=obj_id, param_b=x, param_c=z                                  */
    {
        int which = -1;
        if( action == (int)MINIMENU_ACTION_OPOBJ1 ) which = 1;
        else if( action == (int)MINIMENU_ACTION_OPOBJ2 ) which = 2;
        else if( action == (int)MINIMENU_ACTION_OPOBJ3 ) which = 3;
        else if( action == (int)MINIMENU_ACTION_OPOBJ4 ) which = 4;
        else if( action == (int)MINIMENU_ACTION_OPOBJ5 ) which = 5;
        if( which >= 0 )
        {
            struct CPArgs_OpObj a = { which, param_b, param_c, param_a, 0 };
            clientprot_core_emit(game, CLIENTPROT_OP_OPOBJ, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    /* ── Player actions ──────────────────────────────────────────────────────
     * param_a=player_slot, param_b=x, param_c=z                             */
    {
        int which = -1;
        int base  = action;
        if( base > (int)MINIMENU_ACTION_PRIORITY_OFFSET )
            base -= (int)MINIMENU_ACTION_PRIORITY_OFFSET;
        if( base == (int)MINIMENU_ACTION_OPPLAYER1 ) which = 1;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER2 ) which = 2;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER3 ) which = 3;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER4 ) which = 4;
        else if( base == (int)MINIMENU_ACTION_OPPLAYER5 ) which = 5;
        if( which >= 0 )
        {
            struct CPArgs_OpPlayer a = { which, param_a };
            clientprot_core_emit(game, CLIENTPROT_OP_OPPLAYER, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    /* ── Inventory / held actions ───────────────────────────────────────────*/
    {
        int which = -1;
        if( action == (int)MINIMENU_ACTION_INV_BUTTON1 ) which = 1;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON2 ) which = 2;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON3 ) which = 3;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON4 ) which = 4;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON5 ) which = 5;
        if( which >= 0 )
        {
            /* param_a=comp_id, param_b=slot, param_c=obj_id */
            clientprot_inv_button(game, which, param_a, param_b, param_c);
            return;
        }
    }

    /* ── Interface button ───────────────────────────────────────────────────*/
    if( action == (int)MINIMENU_ACTION_IF_BUTTON ||
        action == (int)MINIMENU_ACTION_IF_BUTTON_TOGGLE ||
        action == (int)MINIMENU_ACTION_IF_BUTTON_SELECT )
    {
        clientprot_if_button(game, param_a);
        return;
    }

    /* ── Modal / social ─────────────────────────────────────────────────────*/
    if( action == (int)MINIMENU_ACTION_CLOSE_MODAL )
    {
        clientprot_close_modal(game);
        return;
    }
    if( action == (int)MINIMENU_ACTION_RESUME_PAUSEBUTTON )
    {
        clientprot_resume_pausebutton(game);
        return;
    }

    /* Fallback: if we have entity tile coords, walk toward them. */
    mm_move_toward(game, param_b, param_c);
}
