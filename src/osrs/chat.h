#ifndef OSRS_CHAT_H
#define OSRS_CHAT_H

#include <stdint.h>

struct GGame;
struct ToriRSRenderCommandBuffer;

#define CHAT_MESSAGE_CAP 100

/** Bits set when a key is present in [component:chat_messages] / chat_input INI (supports 0 values). */
enum ChatLayoutGeomMaskBits
{
    CHAT_LAYOUT_BIT_CHATBACK_SCREEN_X = 1u << 0,
    CHAT_LAYOUT_BIT_CHATBACK_SCREEN_Y = 1u << 1,
    CHAT_LAYOUT_BIT_CLIP_W = 1u << 2,
    CHAT_LAYOUT_BIT_CLIP_H = 1u << 3,
    CHAT_LAYOUT_BIT_TEXT_X_LOCAL = 1u << 4,
    CHAT_LAYOUT_BIT_SCROLLBAR_X_LOCAL = 1u << 5,
    CHAT_LAYOUT_BIT_SEPARATOR_Y_LOCAL = 1u << 6,
    CHAT_LAYOUT_BIT_SEPARATOR_W = 1u << 7,
    CHAT_LAYOUT_BIT_LINE_H = 1u << 8,
    CHAT_LAYOUT_BIT_INPUT_LINE_Y_LOCAL = 1u << 9,
};

/** Chatback geometry for builtin chat drawing (Client.ts drawChat pack). */
struct ChatUILayout
{
    int chatback_screen_x;
    int chatback_screen_y;
    int clip_w;
    int clip_h;
    int text_x_local;
    int scrollbar_x_local;
    int separator_y_local;
    int separator_w;
    int line_h;
    int input_line_y_local;
};

void
chat_layout_builtin(struct ChatUILayout* out);

/** Apply masked fields from INI parse onto `dst` (typically after chat_layout_builtin). */
void
chat_layout_apply_mask(
    struct ChatUILayout* dst,
    struct ChatUILayout const* patch,
    unsigned mask);

/** Resolved layout: game INI when loaded, else builtin defaults. */
void
chat_layout_from_game(struct GGame const* game, struct ChatUILayout* out);

/** Scroll math line height; falls back to builtin 14. */
int
chat_layout_line_h(struct GGame const* game);

struct ChatMessage
{
    int type;
    char user[16];
    char text[256];
};

struct Chat
{
    struct ChatMessage messages[CHAT_MESSAGE_CAP]; /* ring, [0] newest */
    int message_count;

    int chat_scroll_pos;
    int chat_scroll_height;

    /* Privacy/filter modes (mirrors Client.ts CHAT_FILTER_SETTINGS) */
    int chat_public_mode;
    int chat_private_mode;
    int chat_trade_mode;

    /** Enter toggles typing; click on input row focuses (see chat_try_begin_typing_click). */
    int chat_typing_active;

    /* clientcode side-effects written by clientscript_vm_drain_clientcodes */
    int chat_effects;
    int split_private;

    char chat_input[80];

    /* Social (add friend/ignore) input overlay */
    int social_input_open;
    char social_input[80];
    char social_input_header[64];

    /* Count-dialog / P_COUNTDIALOG input overlay */
    int dialog_input_open;
    char dialog_input[80];

    /* PM deduplication (privateMessageIds, Client.ts 460) */
    int pm_ids[100];
    int pm_count;

    /* Frame-stable formatted strings for TORIRS_GFX_DRAW_FONT (pointer lifetime per-frame). */
    char font_draw_line_scratch[CHAT_MESSAGE_CAP][288];
    char font_draw_input_prefix[72];
    char font_draw_input_tail[96];
};

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

struct Chat*
chat_new(void);
void
chat_free(struct Chat* chat);

/* ── Message ring ──────────────────────────────────────────────────────── */

void
chat_add(
    struct Chat* chat,
    struct GGame const* game,
    int type,
    const char* sender,
    const char* text);

/* ── Frame ─────────────────────────────────────────────────────────────── */

/** Draw all chat components (messages, input, privacy buttons). */
void
chat_draw(
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf);

/** Draw only chat messages + scrollbar + Client.ts separator line under messages.
 *  Pass area_x,y,w,h all **-1** to use Client.ts chatback pack (areaChatback.draw(17,357)). */
void
chat_draw_messages(
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int preset_font_id,
    int area_x,
    int area_y,
    int area_w,
    int area_h);

/** Draw chat input line. Pass pen_x and pen_y **-1** for Client.ts positions (local y=90 on
 * chatback). */
void
chat_draw_input(
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int preset_font_id,
    int pen_x,
    int pen_y);

/** Draw only privacy/chat mode buttons (part of chat_draw). */
void
chat_draw_privacy(
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int preset_font_id);

/** Handle a mouse click inside the chat area (privacy buttons, scrollbar, tabs). */
void
chat_handle_click(
    struct Chat* chat,
    struct GGame* game,
    int mx,
    int my,
    int button);

/** Client.ts chatModeLoop(): privacy strip + report-abuse band; fixed 503×765 coords.
 * @return 1 if the click was consumed (modes updated / modal closed). */
int
chat_handle_privacy_strip_click(
    struct Chat* chat,
    struct GGame* game,
    int mx,
    int my,
    int button);

/** Click on the chat input row (same geometry as chat_draw_input). Returns 1 if consumed. */
int
chat_try_begin_typing_click(struct Chat* chat, struct GGame* game, int mx, int my);

/** Returns non-zero when (mx,my) falls within the built-in chat chrome (messages, input row,
 *  privacy strip) and no server chat interface is currently open.  Used by the input layer to
 *  bypass the minimenu primary path so that frame_handle_interface_and_world_clicks can reach
 *  chat_handle_privacy_strip_click / chat_try_begin_typing_click. */
int
chat_builtin_click_is_chrome(struct GGame const* game, int mx, int my);

#endif /* OSRS_CHAT_H */
