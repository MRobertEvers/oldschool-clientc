#ifndef CHAT_STATE_H
#define CHAT_STATE_H

#include <stdbool.h>
#include <stddef.h>

#define CHAT_STATE_LINE_MAX 100
#define CHAT_STATE_FRIEND_MAX 200
#define CHAT_STATE_USERNAME_MAX 64
#define CHAT_STATE_TEXT_MAX 256

struct ChatLine
{
    char text[CHAT_STATE_TEXT_MAX];
    char username[CHAT_STATE_USERNAME_MAX];
    int type;
};

/** Client chat buffer, friend list, filter modes, and scroll offset. */
struct ChatState
{
    struct ChatLine lines[CHAT_STATE_LINE_MAX];
    int line_count;
    char friend_username[CHAT_STATE_FRIEND_MAX][CHAT_STATE_USERNAME_MAX];
    int friend_count;
    /** Vertical scroll offset for the main chatbox (Client.ts chatScrollPos). */
    int scroll_pos;
    /** Non-zero when private chat is drawn in a separate strip. */
    int split_private_chat;
    /** Public chat filter: 0=all, 1=friends, 2=off, 3=hide. */
    int public_mode;
    /** Private chat filter: 0=all, 1=friends, 2=off. */
    int private_mode;
    /** Trade chat filter: 0=all, 1=friends, 2=off. */
    int trade_mode;
    /** Staff moderator level (>=1 enables report-abuse minimenu rows). */
    int staff_mod_level;
    /** Non-zero while a world reboot warning is shown (shifts private-strip layout). */
    int reboot_timer;
};

void
chat_state_reset(struct ChatState* state);

void
chat_state_add_line(
    struct ChatState* state,
    char const* text,
    char const* username,
    int type);

bool
chat_state_is_friend(struct ChatState const* state, char const* username);

bool
chat_state_add_friend(struct ChatState* state, char const* username);

bool
chat_state_remove_friend(struct ChatState* state, char const* username);

void
chat_state_cycle_public_mode(struct ChatState* state);

void
chat_state_cycle_private_mode(struct ChatState* state);

void
chat_state_cycle_trade_mode(struct ChatState* state);

void
chat_state_set_filter_modes(
    struct ChatState* state,
    int public_mode,
    int private_mode,
    int trade_mode);

#endif
