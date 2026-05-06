#include "game.h"

#include "osrs/core/clientprot_core.h"
#include "osrs/ginput.h"
#include "osrs/interface_state.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/lua_sidecar_misc.h"
#include "osrs/painters_cullmap_baked_path.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/wordpack.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
game_player_menu_ops_reset(struct GGame* game)
{
    if( !game )
        return;
    for( int i = 0; i < 5; i++ )
    {
        game->player_menu_op[i][0] = '\0';
        game->player_menu_op_deprioritize[i] = false;
    }
}

void
game_add_message(
    struct GGame* game,
    int type,
    const char* text,
    const char* sender)
{
    if( !game || !game->chat || !text || !text[0] )
        return;
    chat_add(game->chat, game, type, sender, text);
}

void
game_chat_input_screen_name(struct GGame* game, char* out, size_t cap)
{
    if( !game || !out || cap == 0 )
        return;
    out[0] = '\0';
    if( game->world )
    {
        struct PlayerEntity* lp = world_player(game->world, ACTIVE_PLAYER_SLOT);
        if( lp && lp->alive && lp->name.name[0] )
        {
            strncpy(out, lp->name.name, cap - 1);
            out[cap - 1] = '\0';
            return;
        }
    }
    strncpy(out, game->login_username, cap - 1);
    out[cap - 1] = '\0';
}

static char
game_chat_key_to_char(int key, int shift)
{
    if( key >= TORIRSK_A && key <= TORIRSK_Z )
    {
        char c = (char)('a' + (key - TORIRSK_A));
        if( shift )
            c = (char)(c - 'a' + 'A');
        return c;
    }
    if( key >= TORIRSK_0 && key <= TORIRSK_9 )
    {
        static char const unshift[] = "0123456789";
        static char const shifted[] = ")!@#$%^&*(";
        int i = key - TORIRSK_0;
        if( i >= 0 && i < 10 )
            return shift ? shifted[i] : unshift[i];
    }
    if( key == TORIRSK_SPACE )
        return ' ';
    if( key == TORIRSK_COMMA )
        return shift ? '<' : ',';
    if( key == TORIRSK_TAB )
        return '\t';
    return '\0';
}

static void
game_chat_submit_public(struct GGame* game)
{
    struct Chat* ch = game->chat;
    if( !ch || !ch->chat_input[0] )
        return;

    int8_t stack[512];
    struct RSBuffer wb;
    rsbuf_init(&wb, stack, (int)sizeof(stack));
    wordpack_pack(&wb, ch->chat_input);
    clientprot_message_public(
        game, 0, 0, (uint8_t*)wb.data, (int)wb.position);
}

void
game_chat_process_input(struct GGame* game, struct GInput* input)
{
    if( !game || !game->chat || !input )
        return;

    struct Chat* ch = game->chat;
    int eligible = game->net_state == GAME_NET_STATE_GAME && game->iface &&
                   game->iface->chat_interface_id < 0 && !game->minimenu.visible &&
                   !ch->social_input_open && !ch->dialog_input_open;

    int shift = input->key_states[TORIRSK_SHIFT].down;

    for( int i = 0; i < input->event_count; i++ )
    {
        if( input->events[i].type != TORIRSEV_KEY_DOWN )
            continue;
        int k = input->events[i].key_down.key;

        if( k == TORIRSK_RETURN )
        {
            if( !eligible )
                continue;
            if( !ch->chat_typing_active )
            {
                ch->chat_typing_active = 1;
            }
            else
            {
                if( ch->chat_input[0] )
                    game_chat_submit_public(game);
                ch->chat_typing_active = 0;
                ch->chat_input[0] = '\0';
            }
            continue;
        }

        if( !ch->chat_typing_active || !eligible )
            continue;

        char c = game_chat_key_to_char(k, shift);
        if( c && c != '\t' )
        {
            size_t L = strlen(ch->chat_input);
            if( L < sizeof(ch->chat_input) - 1 )
            {
                ch->chat_input[L] = c;
                ch->chat_input[L + 1] = '\0';
            }
        }
    }

    if( ch->chat_typing_active && eligible &&
        input->key_states[TORIRSK_BACKSPACE].pressed )
    {
        size_t L = strlen(ch->chat_input);
        if( L > 0 )
            ch->chat_input[L - 1] = '\0';
    }
}

bool
game_chat_is_typing(struct GGame const* game)
{
    return game && game->chat && game->chat->chat_typing_active;
}
