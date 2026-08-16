#include "chat_state.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

void
chat_state_reset(struct ChatState* state)
{
    assert(state);

    memset(state, 0, sizeof(*state));
}

void
chat_state_add_line(
    struct ChatState* state,
    char const* text,
    char const* username,
    int type)
{
    assert(state);

    if( state->line_count >= CHAT_STATE_LINE_MAX )
    {
        memmove(
            &state->lines[0],
            &state->lines[1],
            (size_t)(CHAT_STATE_LINE_MAX - 1) * sizeof(state->lines[0]));
        state->line_count = CHAT_STATE_LINE_MAX - 1;
    }

    struct ChatLine* row = &state->lines[state->line_count++];
    row->text[0] = '\0';
    row->username[0] = '\0';
    row->type = type;
    if( text )
        snprintf(row->text, sizeof(row->text), "%s", text);
    if( username )
        snprintf(row->username, sizeof(row->username), "%s", username);
}

bool
chat_state_is_friend(struct ChatState const* state, char const* username)
{
    assert(state);
    assert(username);
    if( username[0] == '\0' )
        return false;

    for( int i = 0; i < state->friend_count; i++ )
    {
        if( strcmp(state->friend_username[i], username) == 0 )
            return true;
    }
    return false;
}

bool
chat_state_add_friend(struct ChatState* state, char const* username)
{
    assert(state);
    assert(username);
    if( username[0] == '\0' )
        return false;
    if( chat_state_is_friend(state, username) )
        return false;
    if( state->friend_count >= CHAT_STATE_FRIEND_MAX )
        return false;

    snprintf(
        state->friend_username[state->friend_count],
        sizeof(state->friend_username[0]),
        "%s",
        username);
    state->friend_count++;
    return true;
}

bool
chat_state_remove_friend(struct ChatState* state, char const* username)
{
    assert(state);
    assert(username);
    if( username[0] == '\0' )
        return false;

    for( int i = 0; i < state->friend_count; i++ )
    {
        if( strcmp(state->friend_username[i], username) != 0 )
            continue;

        if( i + 1 < state->friend_count )
        {
            memmove(
                &state->friend_username[i],
                &state->friend_username[i + 1],
                (size_t)(state->friend_count - i - 1) * sizeof(state->friend_username[0]));
        }
        state->friend_count--;
        return true;
    }
    return false;
}

void
chat_state_cycle_public_mode(struct ChatState* state)
{
    assert(state);

    state->public_mode = (state->public_mode + 1) % 4;
}

void
chat_state_cycle_private_mode(struct ChatState* state)
{
    assert(state);

    state->private_mode = (state->private_mode + 1) % 3;
}

void
chat_state_cycle_trade_mode(struct ChatState* state)
{
    assert(state);

    state->trade_mode = (state->trade_mode + 1) % 3;
}

void
chat_state_set_filter_modes(
    struct ChatState* state,
    int public_mode,
    int private_mode,
    int trade_mode)
{
    assert(state);

    state->public_mode = public_mode;
    state->private_mode = private_mode;
    state->trade_mode = trade_mode;
}
