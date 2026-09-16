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

/*
 * The chatbox lays itself out to the HEIGHT of its region, not to 96.
 *
 * Every number the reference states -- a 77px message window, the newest
 * line's baseline at 70, the input line's at 90, a line culled once its
 * baseline reaches 110 -- was measured on the 2004 client's 479x96 chatbox
 * (Client.ts drawChatArea), and every one of them is that box's height less a
 * constant. Written as the constants they follow a taller box, which is what
 * a modern gameframe's 519x142 housing hands the builtin: five lines of
 * history in 96 rows, seven in 122. A frame that keeps the 2004 box passes
 * UI_CHATVIEW_NATIVE_HEIGHT and every number comes back to what it was.
 *
 * They live in this header rather than in rs_chat.h because both halves read
 * them: the game layer places the baselines and the emit walk clips, rules
 * and scrollbars to the same window. ui/ stays leaf -- these are arithmetic
 * on a number the caller already has.
 *
 * The 14-row stride is the FONT's and does not scale with the box, so a
 * height that is not 14k+38 leaves the oldest visible line a sliver against
 * the top border rather than a whole line. That is the frame's number to
 * choose; the message clip cuts the sliver either way, exactly as it does for
 * a scrolled box.
 */
#define UI_CHATVIEW_NATIVE_HEIGHT 96
/** The message window: the rows above the rule the input line sits under. */
#define UI_CHATVIEW_WINDOW_H(h) ((h) - 19)
/** The newest message's baseline; older ones step 14 rows up from it. */
#define UI_CHATVIEW_LAST_BASELINE(h) ((h) - 26)
/** The input line's baseline, under the rule. */
#define UI_CHATVIEW_INPUT_BASELINE(h) ((h) - 6)
/** A baseline at or past this has scrolled off the top of the window. */
#define UI_CHATVIEW_BASELINE_LIMIT(h) ((h) + 14)
/** The scroll extent's floor: one message window plus the reference's 1px. */
#define UI_CHATVIEW_SCROLL_FLOOR(h) (UI_CHATVIEW_WINDOW_H(h) + 1)

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
