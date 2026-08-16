#ifndef SRC_UI_UITREE_CHATVIEW_H
#define SRC_UI_UITREE_CHATVIEW_H

/*
 * Flattened chat-panel draw model. The game layer (rs_chat.c) renders the
 * message ring + filters + input state into this struct each frame; the emit
 * walk turns it into TEXT descs (host request GET_CHAT_STATE). Pure data so
 * ui/ stays leaf — same pattern as UIMinimenu/UIHoverText, but the layout
 * math (reference drawChat) lives game-side where the filters and social
 * store are.
 *
 * All coordinates are REGION-LOCAL; emit adds the chat node's origin.
 */

#define UI_CHATVIEW_LINE_MAX 12
#define UI_CHATVIEW_SPAN_MAX 3
#define UI_CHATVIEW_TEXT_LEN 220

struct UIChatViewSpan
{
    int x;     /* region-local left edge */
    int color; /* RGB */
    char text[UI_CHATVIEW_TEXT_LEN];
};

struct UIChatViewLine
{
    int baseline_y; /* region-local text baseline (reference drawString y) */
    int span_count;
    struct UIChatViewSpan spans[UI_CHATVIEW_SPAN_MAX];
};

struct UIChatView
{
    /** 0 while a chat dialog interface is mounted (its pack draws instead). */
    int visible;
    int font_id;

    struct UIChatViewLine lines[UI_CHATVIEW_LINE_MAX];
    int line_count;

    /** Scrollbar model (reference chatScrollHeight/chatScrollPos). */
    int scroll_height;
    int scroll_pos;

    /** Bottom input line ("user: text*") — absent when centered mode is on. */
    int has_input_line;
    struct UIChatViewLine input_line;

    /** Centered two-line mode (social input / amount input / tutorial). */
    int centered;
    struct UIChatViewLine center_lines[2];
    int center_count;
};

#endif
