#ifndef OSRS_CHAT_H
#define OSRS_CHAT_H

#include <stdint.h>

struct GGame;
struct ToriRSRenderCommandBuffer;

#define CHAT_MESSAGE_CAP 100

struct ChatMessage {
    int  type;
    char user[16];
    char text[256];
};

struct Chat {
    struct ChatMessage messages[CHAT_MESSAGE_CAP]; /* ring, [0] newest */
    int message_count;

    int chat_scroll_pos;
    int chat_scroll_height;

    /* Privacy/filter modes (mirrors Client.ts CHAT_FILTER_SETTINGS) */
    int chat_public_mode;
    int chat_private_mode;
    int chat_trade_mode;

    /* clientcode side-effects written by clientscript_vm_drain_clientcodes */
    int chat_effects;
    int split_private;

    char chat_input[80];

    /* Social (add friend/ignore) input overlay */
    int  social_input_open;
    char social_input[80];
    char social_input_header[64];

    /* Count-dialog / P_COUNTDIALOG input overlay */
    int  dialog_input_open;
    char dialog_input[80];

    /* PM deduplication (privateMessageIds, Client.ts 460) */
    int pm_ids[100];
    int pm_count;
};

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

struct Chat* chat_new(void);
void         chat_free(struct Chat* chat);

/* ── Message ring ──────────────────────────────────────────────────────── */

void chat_add(struct Chat* chat, int type, const char* sender, const char* text);

/* ── Frame ─────────────────────────────────────────────────────────────── */

/** Draw all chat components (messages, input, privacy buttons). */
void chat_draw(
    struct Chat*                  chat,
    struct GGame*                 game,
    struct ToriRSRenderCommandBuffer* cmdbuf);

/** Draw only chat messages (part of chat_draw). */
void chat_draw_messages(
    struct Chat*                  chat,
    struct GGame*                 game,
    struct ToriRSRenderCommandBuffer* cmdbuf);

/** Draw only chat input field (part of chat_draw). */
void chat_draw_input(
    struct Chat*                  chat,
    struct GGame*                 game,
    struct ToriRSRenderCommandBuffer* cmdbuf);

/** Draw only privacy/chat mode buttons (part of chat_draw). */
void chat_draw_privacy(
    struct Chat*                  chat,
    struct GGame*                 game,
    struct ToriRSRenderCommandBuffer* cmdbuf);

/** Handle a mouse click inside the chat area (privacy buttons, scrollbar, tabs). */
void chat_handle_click(
    struct Chat*  chat,
    struct GGame* game,
    int           mx,
    int           my,
    int           button);

#endif /* OSRS_CHAT_H */
