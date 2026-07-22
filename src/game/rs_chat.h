#ifndef SRC_GAME_RS_CHAT_H
#define SRC_GAME_RS_CHAT_H

#include "ui/uitree_chatview.h"

#include <stdint.h>

struct RS_Social;
struct UITreeHost;

/*
 * Chat model: the 100-entry message ring, filter-aware line layout, the typed
 * input line, and the social/amount input flows (reference chatText/chatType/
 * chatUsername + drawChat + handleInputKey). Message TYPES are the 254-era
 * protocol values carried by MESSAGE_GAME/MESSAGE_PRIVATE packets.
 */

#define RS_CHAT_MESSAGE_MAX 100
#define RS_CHAT_SENDER_LEN 64
#define RS_CHAT_TEXT_LEN 200
#define RS_CHAT_INPUT_LEN 80

enum RS_ChatMessageType
{
    RS_CHAT_TYPE_GAME = 0,
    RS_CHAT_TYPE_PUBLIC_MOD = 1,
    RS_CHAT_TYPE_PUBLIC = 2,
    RS_CHAT_TYPE_PRIVATE_FROM = 3,
    RS_CHAT_TYPE_TRADEREQ = 4,
    RS_CHAT_TYPE_PRIVATE_SYSTEM = 5,
    RS_CHAT_TYPE_PRIVATE_TO = 6,
    RS_CHAT_TYPE_PRIVATE_FROM_MOD = 7,
    RS_CHAT_TYPE_DUELREQ = 8,
};

/** Social-input kinds (reference socialInputType). */
enum RS_ChatSocialInput
{
    RS_CHAT_SOCIAL_NONE = 0,
    RS_CHAT_SOCIAL_ADD_FRIEND = 1,
    RS_CHAT_SOCIAL_DEL_FRIEND = 2,
    RS_CHAT_SOCIAL_ADD_IGNORE = 4,
    RS_CHAT_SOCIAL_DEL_IGNORE = 5,
};

struct RS_ChatMessage
{
    int type;
    char sender[RS_CHAT_SENDER_LEN];
    char text[RS_CHAT_TEXT_LEN];
};

struct RS_Chat
{
    /** messages[0] is newest (reference inserts at the front). */
    struct RS_ChatMessage messages[RS_CHAT_MESSAGE_MAX];
    int message_count;

    int scroll_pos;
    char input[RS_CHAT_INPUT_LEN];
    char username[RS_CHAT_SENDER_LEN];

    int social_input_open;
    int social_input_type; /* enum RS_ChatSocialInput */
    char social_input[RS_CHAT_SENDER_LEN];
    char social_header[96];

    int dialog_input_open;
    char dialog_input[16];
};

/** Filter modes + friend lookup the layout needs, snapshot per frame. */
struct RS_ChatFilters
{
    int public_mode;
    int private_mode;
    int trade_mode;
    struct RS_Social const* social;
};

void
RS_Chat_Init(struct RS_Chat* chat, char const* username);

void
RS_Chat_AddMessage(
    struct RS_Chat* chat,
    int type,
    char const* sender,
    char const* text);

/**
 * Render the model into the flattened draw view (reference drawChat layout:
 * p12 lines bottom-up at 14px, baseline y = scroll + 70 - line*14, input line
 * at y 90). `dialog_mounted` suppresses message drawing while a chat
 * interface occupies the region. ui_host is used only for MEASURE_TEXT.
 */
void
RS_Chat_BuildView(
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    struct UITreeHost const* ui_host,
    int font_id,
    int dialog_mounted,
    struct UIChatView* out);

/**
 * Sender under a region-local point, for the minimenu social rows (inverse of
 * the BuildView line layout). Returns 1 and fills out_sender/out_chat_type
 * when the point lands on a line with a sender.
 */
int
RS_Chat_LineAt(
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    int local_x,
    int local_y,
    char* out_sender,
    int sender_cap,
    int* out_chat_type);

/** Scroll by wheel steps (positive = older messages), clamped. */
void
RS_Chat_Scroll(
    struct RS_Chat* chat,
    struct RS_ChatFilters const* filters,
    int wheel_y);

/**
 * One keyboard event (reference handleInputKey chat branches): printable
 * chars append to whichever input line is open, backspace deletes, return
 * submits (public chat = local echo + optional send hook; social input
 * mutates the social store). Returns nonzero when the event changed state.
 */
int
RS_Chat_HandleKey(
    struct RS_Chat* chat,
    struct RS_Social* social,
    int key_typed,
    int key_pressed);

#endif
