#include "minimenu_game.h"

#include <assert.h>

#include "collision_map.h"
#include "game.h"
#include "minimenu_action.h"
#include "osrs/core/clientprot_core.h"
#include "osrs/revconfig/uiscene.h"
#include "revconfig/uitree.h"
#include "world_option_set.h"

#include <stdbool.h>
#include <string.h>

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
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind                 = TORIRS_GFX_DRAW_FONT;
    c->_font_draw.font_id   = font_id;
    c->_font_draw.font      = font;
    c->_font_draw.text      = text;
    c->_font_draw.x         = x;
    c->_font_draw.y         = y;
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

    int mx     = mouse_x;
    int my     = mouse_y;
    int menu_x = mm->x;
    int menu_w = mm->width;

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
            int row_top = mm->y + MINIMENU_HEADER_HEIGHT + opt_i * MINIMENU_ROW_HEIGHT;
            int row_bot = row_top + MINIMENU_ROW_HEIGHT;
            int hovered =
                (mx >= menu_x && mx < menu_x + menu_w && my >= row_top && my < row_bot);
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

void
minimenu_game_show(
    struct GGame* game,
    int click_x,
    int click_y)
{
    if( !game )
        return;

    uitree_sync_hover_option_set(game);

    int vp_w = game->iface_view_port ? game->iface_view_port->width  : 765;
    int vp_h = game->iface_view_port ? game->iface_view_port->height : 503;

    struct UITree* uit = game->ui_root_buffer;
    int use_uitree =
        uit && uit->uitree_optionset.option_count > 0 &&
        !(uit->hover_node_index >= 0 &&
          uit->components[uit->hover_node_index].type == UIELEM_BUILTIN_WORLD);

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

        struct MinimenuOptionLine lines[MINIMENU_MAX_OPTIONS];
        for( int i = 0; i < nlines; i++ )
            lines[i] = tmp[nlines - 1 - i];

        minimenu_show(&game->minimenu, lines, nlines, vp_w, vp_h, click_x, click_y);
        return;
    }

    struct MinimenuOptionLine tmp[MINIMENU_MAX_OPTIONS];
    struct WorldOptionSet* os = &game->option_set;
    int nlines = minimenu_fill_world_ts_menu_lines(
        os,
        game->mouse_tile_x,
        game->mouse_tile_z,
        tmp,
        MINIMENU_MAX_OPTIONS);

    /* Client.ts draws option i at (menuNumEntries - 1 - i): index 0 is bottom; we draw 0 at top. */
    struct MinimenuOptionLine lines[MINIMENU_MAX_OPTIONS];
    for( int i = 0; i < nlines; i++ )
        lines[i] = tmp[nlines - 1 - i];

    minimenu_show(&game->minimenu, lines, nlines, vp_w, vp_h, click_x, click_y);
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
        game->mouse_x,
        game->mouse_y);
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
    game->cross_x            = game->mouse_clicked_right_x;
    game->cross_y            = game->mouse_clicked_right_y;
    game->cross_cycle        = 0;
}

void
minimenu_game_use_option(
    struct GGame* game,
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

    if( action == (int)MINIMENU_ACTION_CANCEL )
        return;

    if( action == (int)MINIMENU_ACTION_WALK )
    {
        game->tile_clicked_x     = param_a;
        game->tile_clicked_z     = param_b;
        game->tile_clicked_level =
            (game->mouse_tile_level >= 0) ? game->mouse_tile_level : 0;
        game->cross_x     = game->mouse_clicked_right_x;
        game->cross_y     = game->mouse_clicked_right_y;
        game->cross_mode  = 1;
        game->cross_cycle = 0;
        return;
    }

    {
        int which = -1;
        int base  = action;
        if( base > (int)MINIMENU_ACTION_PRIORITY_OFFSET )
            base -= (int)MINIMENU_ACTION_PRIORITY_OFFSET;
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
            struct CPArgs_OpNpc a = { which, param_a };
            clientprot_core_emit(game, CLIENTPROT_OP_OPNPC, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    {
        int which = -1;
        if( action == (int)MINIMENU_ACTION_OPLOC1 )
            which = 1;
        else if( action == (int)MINIMENU_ACTION_OPLOC2 )
            which = 2;
        else if( action == (int)MINIMENU_ACTION_OPLOC3 )
            which = 3;
        else if( action == (int)MINIMENU_ACTION_OPLOC4 )
            which = 4;
        else if( action == (int)MINIMENU_ACTION_OPLOC5 )
            which = 5;
        if( which >= 0 )
        {
            struct CPArgs_OpLoc a = { which, param_b, param_c, param_a, 0 };
            clientprot_core_emit(game, CLIENTPROT_OP_OPLOC, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    {
        int which = -1;
        if( action == (int)MINIMENU_ACTION_OPOBJ1 )
            which = 1;
        else if( action == (int)MINIMENU_ACTION_OPOBJ2 )
            which = 2;
        else if( action == (int)MINIMENU_ACTION_OPOBJ3 )
            which = 3;
        else if( action == (int)MINIMENU_ACTION_OPOBJ4 )
            which = 4;
        else if( action == (int)MINIMENU_ACTION_OPOBJ5 )
            which = 5;
        if( which >= 0 )
        {
            struct CPArgs_OpObj a = { which, param_b, param_c, param_a, 0 };
            clientprot_core_emit(game, CLIENTPROT_OP_OPOBJ, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    {
        int which = -1;
        int base = action;
        if( base > (int)MINIMENU_ACTION_PRIORITY_OFFSET )
            base -= (int)MINIMENU_ACTION_PRIORITY_OFFSET;
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
            struct CPArgs_OpPlayer a = { which, param_a };
            clientprot_core_emit(game, CLIENTPROT_OP_OPPLAYER, &a);
            mm_move_toward(game, param_b, param_c);
            return;
        }
    }

    {
        int which = -1;
        if( action == (int)MINIMENU_ACTION_INV_BUTTON1 )
            which = 1;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON2 )
            which = 2;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON3 )
            which = 3;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON4 )
            which = 4;
        else if( action == (int)MINIMENU_ACTION_INV_BUTTON5 )
            which = 5;
        if( which >= 0 )
        {
            clientprot_inv_button(game, which, param_a, param_b, param_c);
            return;
        }
    }

    {
        int which = -1;
        int base = action;
        if( base > (int)MINIMENU_ACTION_PRIORITY_OFFSET )
            base -= (int)MINIMENU_ACTION_PRIORITY_OFFSET;
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
            struct CPArgs_OpHeld a = { which, param_c, param_b, param_a };
            clientprot_core_emit(game, CLIENTPROT_OP_OPHELD, &a);
            return;
        }
        if( which == 6 )
        {
            /* Examine held item — optional server packet depending on revision; keep stub. */
            return;
        }
    }

    if( action == (int)MINIMENU_ACTION_IF_BUTTON ||
        action == (int)MINIMENU_ACTION_IF_BUTTON_TOGGLE ||
        action == (int)MINIMENU_ACTION_IF_BUTTON_SELECT )
    {
        clientprot_if_button(game, param_a);
        return;
    }

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
    (void)game;
    (void)cm;
    (void)src_local_x;
    (void)src_local_z;
    (void)dest_local_x;
    (void)dest_local_z;
    return true;
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
    (void)game;
    (void)cm;
    (void)src_local_x;
    (void)src_local_z;
    (void)dest_local_x;
    (void)dest_local_z;
    return true;
}
