#include "minimenu.h"

#include "buildcachedat.h"
#include "collision_map.h"
#include "colors.h"
#include "game_entity.h"
#include "graphics/dash.h"
#include "isaac.h"
#include "osrs/packetout.h"
#include "osrs/packets/revpacket_lc245_2.h"
#include "rscache/tables/config_locs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACTIVE_PLAYER_SLOT 2047
#define MOVE_GAMECLICK_OPCODE 182

bool
send_move_path_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z)
{
    int path_local_x[25];
    int path_local_z[25];
    int waypoints = collision_map_bfs_path(
        cm, src_local_x, src_local_z, dest_local_x, dest_local_z, path_local_x, path_local_z, 25);
    if( waypoints < 0 )
        return false;
    if( waypoints > 0 )
    {
        int buffer_size = (waypoints + 1) > 25 ? 25 : waypoints + 1;
        int steps_to_send = buffer_size - 1;
        int payload_size = 1 + 2 + 2 + steps_to_send * 2;
        int start_world_x = game->scene_base_tile_x + src_local_x;
        int start_world_z = game->scene_base_tile_z + src_local_z;
        if( game->outbound_size + 2 + payload_size <= (int)sizeof(game->outbound_buffer) )
        {
            uint32_t op = (MOVE_GAMECLICK_OPCODE + isaac_next(game->random_out)) & 0xff;
            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
            game->outbound_buffer[game->outbound_size++] = (uint8_t)payload_size;
            game->outbound_buffer[game->outbound_size++] = 0;
            game->outbound_buffer[game->outbound_size++] = (start_world_x >> 8) & 0xff;
            game->outbound_buffer[game->outbound_size++] = start_world_x & 0xff;
            game->outbound_buffer[game->outbound_size++] = (start_world_z >> 8) & 0xff;
            game->outbound_buffer[game->outbound_size++] = start_world_z & 0xff;
            for( int i = 0; i < steps_to_send && i < waypoints; i++ )
            {
                game->outbound_buffer[game->outbound_size++] =
                    (uint8_t)(int8_t)(path_local_x[i] - src_local_x);
                game->outbound_buffer[game->outbound_size++] =
                    (uint8_t)(int8_t)(path_local_z[i] - src_local_z);
            }
        }
    }
    return true;
}

static void
add_option(
    struct GGame* game,
    const char* text,
    int op_index)
{
    if( game->menu_size >= MINIMENU_MAX_OPTIONS )
        return;
    int n = game->menu_size;
    snprintf(game->menu_options[n], MINIMENU_OPTION_LEN, "%s", text);
    game->menu_option_action[n] = op_index;
    game->menu_size++;
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
    game->menu_size = 0;
    game->menu_visible = 0;

    /* Always add Cancel at index 0. Client.ts puts Cancel first. */
    for( int i = game->menu_size; i > 0; i-- )
    {
        memcpy(game->menu_options[i], game->menu_options[i - 1], MINIMENU_OPTION_LEN);
        game->menu_option_action[i] = game->menu_option_action[i - 1];
    }
    snprintf(game->menu_options[0], MINIMENU_OPTION_LEN, "Cancel");
    game->menu_option_action[0] = -1; /* Cancel = no action */
    game->menu_size++;

    /* Reverse options 1..n-1 so order matches Client.ts (Cancel stays at top). */
    for( int i = 1, j = game->menu_size - 1; i < j; i++, j-- )
    {
        char tmp_opt[MINIMENU_OPTION_LEN];
        memcpy(tmp_opt, game->menu_options[i], MINIMENU_OPTION_LEN);
        memcpy(game->menu_options[i], game->menu_options[j], MINIMENU_OPTION_LEN);
        memcpy(game->menu_options[j], tmp_opt, MINIMENU_OPTION_LEN);
        int tmp_act = game->menu_option_action[i];
        game->menu_option_action[i] = game->menu_option_action[j];
        game->menu_option_action[j] = tmp_act;
    }

    /* Compute width from "Choose Option" and option strings */
    int width = 0;
    if( game->pixfont_b12 )
    {
        width = string_width(game->pixfont_b12, "Choose Option");
        for( int i = 0; i < game->menu_size; i++ )
        {
            /* Strip @cya@ etc. for width - use raw length as approx */
            int w = string_width(game->pixfont_b12, game->menu_options[i]);
            if( w > width )
                width = w;
        }
    }
    width += 12;
    int height = game->menu_size * 15 + 21;

    int vp_w = game->view_port ? game->view_port->width : 512;
    int vp_h = game->view_port ? game->view_port->height : 334;

    /* Position menu: centered at click, above cursor. Client.ts showContextMenu viewport area. */
    int x = click_x - (width / 2);
    if( x + width > vp_w )
        x = vp_w - width;
    if( x < 0 )
        x = 0;

    int y = click_y - 11;
    if( y + height > vp_h )
        y = vp_h - height;
    if( y < 0 )
        y = 0;

    game->menu_visible = 1;
    game->menu_area = 0;
    game->menu_x = x;
    game->menu_y = y;
    game->menu_width = width;
    game->menu_height = game->menu_size * 15 + 22;
}

void
minimenu_draw(
    struct GGame* game,
    int* pixel_buffer,
    int stride,
    int clip_l,
    int clip_t,
    int clip_r,
    int clip_b,
    int offset_x,
    int offset_y)
{
    if( !game->menu_visible || !game->pixfont_b12 )
        return;

    int x = game->menu_x + offset_x;
    int y = game->menu_y + offset_y;
    int w = game->menu_width;
    int h = game->menu_height;

    /* Client.ts drawMenu: fill background, black header, border. Use 0xFF prefix for opaque. */
    dash2d_fill_rect_clipped(
        pixel_buffer,
        stride,
        x,
        y,
        w,
        h,
        0xFF000000 | OPTIONS_MENU,
        clip_l,
        clip_t,
        clip_r,
        clip_b);
    dash2d_fill_rect_clipped(
        pixel_buffer,
        stride,
        x + 1,
        y + 1,
        w - 2,
        16,
        0xFF000000 | BLACK,
        clip_l,
        clip_t,
        clip_r,
        clip_b);
    dash2d_draw_rect(pixel_buffer, stride, x + 1, y + 18, w - 2, h - 19, 0xFF000000 | BLACK);

    dashfont_draw_text_clipped(
        game->pixfont_b12,
        (uint8_t*)"Choose Option",
        x + 3,
        y + 2,
        0xFF000000 | OPTIONS_MENU,
        pixel_buffer,
        stride,
        clip_l,
        clip_t,
        clip_r,
        clip_b);

    /* Options: Client.ts order is reversed (menuOption[0] at bottom). Draw top to bottom.
     * Mouse and menu bounds in viewport space for hover highlight. */
    int mouse_vp_x = game->mouse_x - offset_x;
    int mouse_vp_y = game->mouse_y - offset_y;
    int menu_vp_x = game->menu_x;
    int menu_vp_y = game->menu_y;

    /* Option rows: header ends at y+18, first row at y+19. Client.ts optionY = y + (n-1-i)*15 + 31.
     * Use y+19 so text sits at top of row (dash font y = top of glyph). */
    for( int i = 0; i < game->menu_size; i++ )
    {
        int option_y = y + (game->menu_size - 1 - i) * 15 + 19;
        int option_y_vp = menu_vp_y + (game->menu_size - 1 - i) * 15 + 20;

        /* Hit area: full 15px row from layout. Row spans y+19+(n-1-i)*15 to y+34+(n-1-i)*15. */
        int row_top = menu_vp_y + 19 + (game->menu_size - 1 - i) * 15;
        int row_bot = row_top + 15;
        int rgb = 0xFF000000 | WHITE;
        if( mouse_vp_x > menu_vp_x && mouse_vp_x < menu_vp_x + w && mouse_vp_y > row_top &&
            mouse_vp_y < row_bot )
            rgb = 0xFF000000 | YELLOW;

        dashfont_draw_text_clipped_taggable(
            game->pixfont_b12,
            (uint8_t*)game->menu_options[i],
            x + 3,
            option_y,
            rgb,
            pixel_buffer,
            stride,
            clip_l,
            clip_t,
            clip_r,
            clip_b,
            true);
    }
}

int
minimenu_click_option(
    struct GGame* game,
    int click_x,
    int click_y)
{
    if( !game->menu_visible )
        return -1;

    int menu_x = game->menu_x;
    int menu_y = game->menu_y;
    int menu_width = game->menu_width;

    /* Click in viewport coords; menu is in viewport coords. Match draw hit area. */
    for( int i = 0; i < game->menu_size; i++ )
    {
        int row_top = menu_y + 19 + (game->menu_size - 1 - i) * 15;
        int row_bot = row_top + 15;
        if( click_x > menu_x && click_x < menu_x + menu_width && click_y > row_top &&
            click_y < row_bot )
        {
            return i;
        }
    }

    /* Click outside menu - hide. Client.ts: if mouse outside menu bounds, menuVisible = false */
    if( click_x < menu_x - 10 || click_x > menu_x + menu_width + 10 || click_y < menu_y - 10 ||
        click_y > menu_y + game->menu_height + 10 )
    {
        game->menu_visible = 0;
        return -2;
    }

    return -1;
}

void
minimenu_use_option(
    struct GGame* game,
    int option_index)
{
    if( option_index < 0 || option_index >= game->menu_size )
        return;

    int action = game->menu_option_action[option_index];
    if( action < 0 )
        return; /* Cancel */

    /* Walk Here (action 100): handled in frame - find tile and path. */
    if( action == 100 )
        return;

    game->menu_visible = 0;
}
