#include "game.h"

#include "osrs/gamenet_send.h"
#include "osrs/ginput.h"
#include "osrs/interface_state.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/lua_sidecar_misc.h"
#include "osrs/painters_cullmap_baked_path.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/wordpack.h"
#include "osrs/world.h"

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

/** @return 1 if @a pfx (length n) was at the start of @a s and was removed. */
static int
game_chat_strip_if(char* s, char const* pfx)
{
    size_t n = strlen(pfx);
    if( strncmp(s, pfx, n) != 0 )
        return 0;
    memmove(s, s + n, strlen(s + n) + 1);
    return 1;
}

/** Client.ts public chat: colour + optional wave/scroll ( MESSAGE_PUBLIC before WordPack ). */
static void
game_chat_parse_public_style(char* line, int* colour, int* effect)
{
    *colour = 0;
    *effect = 0;
    if( game_chat_strip_if(line, "yellow:") )
        *colour = 0;
    else if( game_chat_strip_if(line, "red:") )
        *colour = 1;
    else if( game_chat_strip_if(line, "green:") )
        *colour = 2;
    else if( game_chat_strip_if(line, "cyan:") )
        *colour = 3;
    else if( game_chat_strip_if(line, "purple:") )
        *colour = 4;
    else if( game_chat_strip_if(line, "white:") )
        *colour = 5;
    else if( game_chat_strip_if(line, "flash1:") )
        *colour = 6;
    else if( game_chat_strip_if(line, "flash2:") )
        *colour = 7;
    else if( game_chat_strip_if(line, "flash3:") )
        *colour = 8;
    else if( game_chat_strip_if(line, "glow1:") )
        *colour = 9;
    else if( game_chat_strip_if(line, "glow2:") )
        *colour = 10;
    else if( game_chat_strip_if(line, "glow3:") )
        *colour = 11;

    if( game_chat_strip_if(line, "wave:") )
        *effect = 1;
    if( game_chat_strip_if(line, "scroll:") )
        *effect = 2;
}

/** JString.toSentenceCase — lower entire string, then capitalise first letter after . ! or start. */
static void
game_chat_sentence_case(char* s)
{
    for( char* p = s; *p; p++ )
    {
        if( *p >= 'A' && *p <= 'Z' )
            *p = (char)(*p - 'A' + 'a');
    }
    int punctuation = 1;
    for( char* p = s; *p; p++ )
    {
        char c = *p;
        if( punctuation && c >= 'a' && c <= 'z' )
        {
            *p = (char)(c - 'a' + 'A');
            punctuation = 0;
        }
        if( c == '.' || c == '!' )
            punctuation = 1;
    }
}

static void
game_chat_trim_edges(char* s)
{
    char* p = s;
    while( *p == ' ' || *p == '\t' )
        p++;
    if( p != s )
        memmove(s, p, strlen(p) + 1);
    size_t L = strlen(s);
    while( L > 0 && (s[L - 1] == ' ' || s[L - 1] == '\t') )
    {
        s[--L] = '\0';
    }
}

static void
game_chat_submit_public(struct GGame* game)
{
    struct Chat* ch = game->chat;
    if( !ch || !ch->chat_input[0] )
        return;

    if( strncmp(ch->chat_input, "::", 2) == 0 )
    {
        gamenet_send_client_cheat(game, ch->chat_input);
        return;
    }

    char line[80];
    strncpy(line, ch->chat_input, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    int colour;
    int effect;
    game_chat_parse_public_style(line, &colour, &effect);

    int8_t stack[512];
    struct RSBuffer wb;
    rsbuf_init(&wb, stack, (int)sizeof(stack));
    wordpack_pack(&wb, line);
    gamenet_send_message_public(
        game, colour, effect, (uint8_t*)wb.data, (int)wb.position);

    if( !game->world )
        return;
    struct PlayerEntity* lp = world_player(game->world, ACTIVE_PLAYER_SLOT);
    if( !lp || !lp->alive || !lp->name.name[0] )
        return;

    char display[256];
    strncpy(display, line, sizeof(display) - 1);
    display[sizeof(display) - 1] = '\0';
    game_chat_sentence_case(display);
    game_chat_trim_edges(display);
    if( !display[0] )
        return;

    char sender[32];
    if( game->staff_mod_level == 2 )
        snprintf(sender, sizeof(sender), "@cr2@%s", lp->name.name);
    else if( game->staff_mod_level == 1 )
        snprintf(sender, sizeof(sender), "@cr1@%s", lp->name.name);
    else
    {
        strncpy(sender, lp->name.name, sizeof(sender) - 1);
        sender[sizeof(sender) - 1] = '\0';
    }

    game_add_message(game, 2, display, sender);
}

bool
game_chat_public_input_eligible(struct GGame const* game)
{
    if( !game || !game->chat || !game->iface )
        return false;
    struct Chat const* ch = game->chat;
    return game->net_state == GAME_NET_STATE_GAME && game->iface->chat_interface_id < 0 &&
           !game->minimenu.visible && !ch->social_input_open && !ch->dialog_input_open;
}

void
game_chat_process_input(struct GGame* game, struct GInput* input)
{
    if( !game || !game->chat || !input )
        return;

    struct Chat* ch = game->chat;
    int eligible = game_chat_public_input_eligible(game);

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
