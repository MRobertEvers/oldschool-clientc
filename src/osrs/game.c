#include "game.h"

#include <stdlib.h>
#include <string.h>

void
game_add_message(
    struct GGame* game,
    int type,
    const char* text,
    const char* sender)
{
    for( int i = GAME_CHAT_MAX - 1; i > 0; i-- )
    {
        game->message_type[i] = game->message_type[i - 1];
        strncpy(
            game->message_sender[i],
            game->message_sender[i - 1],
            GAME_CHAT_SENDER_LEN - 1);
        game->message_sender[i][GAME_CHAT_SENDER_LEN - 1] = '\0';
        strncpy(
            game->message_text[i],
            game->message_text[i - 1],
            GAME_CHAT_TEXT_LEN - 1);
        game->message_text[i][GAME_CHAT_TEXT_LEN - 1] = '\0';
    }
    game->message_type[0] = type;
    if( sender )
    {
        strncpy(game->message_sender[0], sender, GAME_CHAT_SENDER_LEN - 1);
        game->message_sender[0][GAME_CHAT_SENDER_LEN - 1] = '\0';
    }
    else
    {
        game->message_sender[0][0] = '\0';
    }
    if( text )
    {
        strncpy(game->message_text[0], text, GAME_CHAT_TEXT_LEN - 1);
        game->message_text[0][GAME_CHAT_TEXT_LEN - 1] = '\0';
    }
    else
    {
        game->message_text[0][0] = '\0';
    }
}
