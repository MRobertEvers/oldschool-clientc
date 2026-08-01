#ifndef SRC_GAME_RS_CHAT_WIDGETS_H
#define SRC_GAME_RS_CHAT_WIDGETS_H

struct RS_Chat;
struct RS_ChatFilters;
struct RS_ChatMessage;
struct UITree;

/*
 * The chatbox as the cache draws it.
 *
 * Two eras answer "where do chat lines come from" completely differently, and
 * this file is the second answer.
 *
 * At 254 the chatbox is a *surface*: the gameframe reserves a box and the client
 * paints message text into it with its own font calls. `RS_Chat_BuildView` and
 * `emit_chat` are that path, and they stay — the dat1 revconfigs still use them.
 *
 * At 230 the chatbox is *widgets*. Interface 162 ships 500 empty text components
 * (`chatbox:line0`..`line499`) inside a scrolling layer, and the client's whole
 * job is to fill in their text and move them. Everything else — the background,
 * the seven filter tabs, the Report button, the scrollbar, the fonts, the
 * right-click ops on each line — is already in the cache and already drawn by
 * the ordinary widget pipeline. Painting a surface over it would mean
 * reimplementing all of that *and* covering the version the cache draws.
 *
 * Which is why the symptom was a perfectly rendered, permanently empty chatbox:
 * the cache's chatbox was drawing correctly the whole time. There was nothing to
 * fix in the renderer and nothing missing from the packets. The 500 components
 * were simply never written to, because nothing in the client knew they were
 * what a chat line is.
 *
 * The ids come from the boot manifest's `[ui:chatbox]` section rather than from
 * here. They are revision facts, like the gameframe mount table beside them, and
 * the reference client's own equivalent is a generated `ComponentID` constant —
 * not something derived at runtime.
 */
struct RS_ChatWidgetLayout
{
    /** Chatbox interface (rev 230: 162). 0 = this revision has no widget
     *  chatbox, and every function here is a no-op. */
    int interface_id;
    /** Scrolling layer holding the line components (`chatbox:scrollarea`). */
    int messages_child;
    /** First line component (`chatbox:line0`). */
    int first_line;
    /** How many line components the interface ships. */
    int line_count;
    /** The typed-input line (`chatbox:input`). -1 to leave it alone. */
    int input_child;
    /** Pixels per line — the height the line components declare. */
    int line_height;
};

/** True when the manifest declared a widget chatbox for this revision. */
int
RS_ChatWidgets_Enabled(struct RS_ChatWidgetLayout const* layout);

/**
 * Write the visible chat into the interface's line components.
 *
 * Oldest at the top, newest at the bottom above the input line — the layer
 * scrolls, so "newest visible" is a scroll position rather than a choice of
 * which messages to lay out. Lines past the end of the message list are hidden
 * rather than blanked, so they cost nothing to draw and cannot eat a click.
 *
 * Returns the number of lines written.
 */
int
RS_ChatWidgets_Apply(
    struct UITree* tree,
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    struct RS_ChatWidgetLayout const* layout);

/**
 * Compose one message into a single markup string, the way a rev-230 line
 * component wants it.
 *
 * The 254 path positions a sender span and a text span independently
 * (`layout_message` in rs_chat.c) because it is drawing them itself. A widget
 * line is one string, so the colour split becomes `<col=…>` markup — which the
 * font renderer already understands, and which is how the cache's own text
 * carries colour.
 *
 * Exposed for the unit test; `RS_ChatWidgets_Apply` is the only other caller.
 */
void
RS_ChatWidgets_ComposeLine(
    struct RS_ChatMessage const* message,
    char* out,
    int cap);

#endif
