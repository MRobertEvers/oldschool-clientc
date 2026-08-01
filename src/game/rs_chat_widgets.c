#include "rs_chat_widgets.h"

#include "rs_chat.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <string.h>

/* The same six colours the 254 path draws with (rs_chat.c), spelled as the
 * six-digit hex `<col=…>` wants. One list, because a public-chat blue that
 * differs between the two renderers is a difference nobody would ever see
 * side by side and so would never get fixed. */
#define CHAT_HEX_BLACK "000000"
#define CHAT_HEX_BLUE "0000ff"
#define CHAT_HEX_DARKRED "800000"
#define CHAT_HEX_TRADE "800080"
#define CHAT_HEX_DUEL "7e3200"

static int
component_uid(
    struct RS_ChatWidgetLayout const* layout,
    int child)
{
    return (layout->interface_id << 16) | (child & 0xffff);
}

int
RS_ChatWidgets_Enabled(struct RS_ChatWidgetLayout const* layout)
{
    return layout && layout->interface_id > 0 && layout->line_count > 0;
}

void
RS_ChatWidgets_ComposeLine(
    struct RS_ChatMessage const* message,
    char* out,
    int cap)
{
    if( !out || cap <= 0 )
        return;
    out[0] = '\0';
    if( !message )
        return;

    switch( message->type )
    {
    case RS_CHAT_TYPE_GAME:
        snprintf(out, (size_t)cap, "<col=" CHAT_HEX_BLACK ">%s</col>", message->text);
        break;
    case RS_CHAT_TYPE_PUBLIC_MOD:
    case RS_CHAT_TYPE_PUBLIC:
        snprintf(out, (size_t)cap,
                 "<col=" CHAT_HEX_BLACK ">%s:</col> <col=" CHAT_HEX_BLUE ">%s</col>",
                 message->sender, message->text);
        break;
    case RS_CHAT_TYPE_PRIVATE_FROM:
    case RS_CHAT_TYPE_PRIVATE_FROM_MOD:
        snprintf(out, (size_t)cap,
                 "<col=" CHAT_HEX_BLACK ">From %s:</col> <col=" CHAT_HEX_DARKRED ">%s</col>",
                 message->sender, message->text);
        break;
    case RS_CHAT_TYPE_PRIVATE_TO:
        snprintf(out, (size_t)cap,
                 "<col=" CHAT_HEX_BLACK ">To %s:</col> <col=" CHAT_HEX_DARKRED ">%s</col>",
                 message->sender, message->text);
        break;
    case RS_CHAT_TYPE_PRIVATE_SYSTEM:
        snprintf(out, (size_t)cap, "<col=" CHAT_HEX_DARKRED ">%s</col>", message->text);
        break;
    case RS_CHAT_TYPE_TRADEREQ:
        snprintf(out, (size_t)cap, "<col=" CHAT_HEX_TRADE ">%s %s</col>", message->sender,
                 message->text);
        break;
    case RS_CHAT_TYPE_DUELREQ:
        snprintf(out, (size_t)cap, "<col=" CHAT_HEX_DUEL ">%s %s</col>", message->sender,
                 message->text);
        break;
    default:
        break;
    }
}

int
RS_ChatWidgets_Apply(
    struct UITree* tree,
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    struct RS_ChatWidgetLayout const* layout)
{
    char line[RS_CHAT_SENDER_LEN + RS_CHAT_TEXT_LEN + 64];
    int line_height;
    int written = 0;
    int visible = 0;
    int viewport = 0;

    if( !tree || !chat || !filters || !RS_ChatWidgets_Enabled(layout) )
        return 0;

    line_height = layout->line_height > 0 ? layout->line_height : 14;

    /*
     * Read the scroll viewport BEFORE moving anything.
     *
     * `UITree_ApplyPosition` clears `layout_resolved` and marks the tree stale,
     * and `UITree_LayoutGetBounds` silently falls back to the *declared* fields
     * when a node is unresolved. So asking for the viewport after the lines have
     * moved gets 452 — the number the interface record states against a 503-tall
     * root — instead of the ~114 the gameframe's chat slot actually gives it.
     * Reading first is a one-line ordering constraint with no symptom of its own:
     * the scroll offset just comes out three hundred pixels wrong.
     */
    if( layout->messages_child >= 0 )
    {
        int32_t idx = UITree_FindByComponentId(tree, component_uid(layout, layout->messages_child));

        if( idx >= 0 )
        {
            int bx = 0;
            int by = 0;
            int bw = 0;

            UITree_LayoutGetBounds(&tree->components[idx].position, &bx, &by, &bw, &viewport);
        }
    }

    /*
     * Oldest first, and the ring is newest first.
     *
     * `chat->messages[0]` is the newest (the reference inserts at the front), so
     * laying them out in index order would put the newest at the top and read
     * upside down. Counting the visible ones first and then walking backwards is
     * the cheap way to get both the count — which the scroll height needs before
     * the first line is placed — and the order.
     */
    for( int i = 0; i < chat->message_count; i++ )
    {
        if( RS_Chat_MessagePasses(filters, &chat->messages[i]) )
            visible++;
    }
    if( visible > layout->line_count )
        visible = layout->line_count;

    /*
     * A short chatbox fills from the BOTTOM.
     *
     * Six messages in a viewport eight lines tall leave two lines of gap, and
     * the gap goes above the oldest message, not below the newest — chat grows
     * upward off the input line. Laying out from y=0 puts the blank space at the
     * bottom, which reads as the newest message having scrolled away.
     *
     * Once the column overflows the viewport this term is zero and the scroll
     * position below takes over.
     */
    {
        /* Content height is exactly the lines — no padding term. The interface
         * gives line0 a y of 1, and carrying that through as a `+1` here made
         * the column one pixel taller than the lines it holds, so a full
         * chatbox scrolled one pixel short of the bottom and a short one sat
         * one pixel low. It is a nudge nobody would see and an off-by-one in
         * every arithmetic below that reads `content`. */
        int content = visible * line_height;
        int top = viewport > content ? viewport - content : 0;

        for( int i = chat->message_count - 1; i >= 0 && written < visible; i-- )
        {
            struct RS_ChatMessage const* message = &chat->messages[i];
            int uid;

            if( !RS_Chat_MessagePasses(filters, message) )
                continue;
            uid = component_uid(layout, layout->first_line + written);
            RS_ChatWidgets_ComposeLine(message, line, (int)sizeof(line));
            UITree_ApplyText(tree, uid, line);
            UITree_ApplyPosition(tree, uid, 3, top + written * line_height);
            UITree_ApplyHide(tree, uid, false);
            written++;
        }
    }

    /*
     * Hide the rest rather than blanking them.
     *
     * An empty text component still lays out, still hit-tests and still offers
     * its op — component 162:59 and friends each carry `op8='*'`, which is the
     * per-line right-click the reference uses for Report abuse. Five hundred
     * invisible, clickable, empty lines stacked over the chatbox would swallow
     * every click that missed a real message.
     */
    for( int i = written; i < layout->line_count; i++ )
        UITree_ApplyHide(tree, component_uid(layout, layout->first_line + i), true);

    /*
     * The scroll layer, and where it sits.
     *
     * Content is as tall as the lines; the position is pinned to the bottom so a
     * new message is the one you are looking at. `chat->scroll_pos` is the
     * player's own offset from that bottom, in pixels, and is what the wheel and
     * the scrollbar move — so scrolling up survives a new message arriving,
     * which is the behaviour that makes a busy chatbox readable.
     *
     * The clamp is against the layer's RESOLVED height, not its declared one.
     * `chatbox:scrollarea` is `heightmode=1` — parent height minus 16 — so what
     * the interface record says (452, against a 503-tall root) has nothing to do
     * with the ~100px the gameframe's chat slot actually gives it. Scrolling to
     * `content` unconditionally is only correct when the content overflows;
     * with six messages in a viewport seven lines tall it scrolls the whole
     * column off the top, which renders as a chatbox that draws its background,
     * its tabs and its input line perfectly and shows no messages at all.
     * `UITree_ApplyScrollPos` does not clamp for us — it is the raw setter the
     * CS2 op uses.
     */
    if( layout->messages_child >= 0 )
    {
        int uid = component_uid(layout, layout->messages_child);
        int content = written * line_height;
        int position = content - viewport - chat->scroll_pos;

        if( position < 0 )
            position = 0;
        UITree_ApplyScrollSize(tree, uid, 0, content);
        UITree_ApplyScrollPos(tree, uid, 0, position);
    }

    /*
     * The input line, `name: typed*`.
     *
     * The trailing `*` is the reference's caret. It is unconditional there —
     * there is no blink — so a missing one reads as the chatbox not having
     * focus, which at 230 it always does.
     */
    if( layout->input_child >= 0 )
    {
        int uid = component_uid(layout, layout->input_child);

        snprintf(line, sizeof(line), "<col=" CHAT_HEX_BLACK ">%s:</col> <col=" CHAT_HEX_BLUE
                                     ">%s</col><col=" CHAT_HEX_BLACK ">*</col>",
                 chat->username[0] ? chat->username : "Player", chat->input);
        UITree_ApplyText(tree, uid, line);
    }

    /*
     * Resolve the layout this function just invalidated.
     *
     * The app's own `UITree_LayoutResolve` runs earlier in the frame and only
     * when a CS2 script ran, so nothing after it re-lays the tree. An unresolved
     * text node emits at its *declared* position and a size of zero — which is
     * how the first working version of this drew every chat line as a
     * zero-by-zero box at the top-left corner of the screen, correct text and
     * all. Ending on a resolved tree is this function's contract, not the
     * caller's problem.
     */
    UITree_EnsureLayout(tree);

    return written;
}
