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
    /**
     * The typed-input line (`chatbox:input`). **-1 at rev 230, and that is not
     * "unset" — it is the answer.**
     *
     * The scrollback is ours because nothing else writes it. The input line is
     * not: at this revision the whole typed line is a clientscript. The
     * chatbox root carries an onKey hook (script 73) that edits
     * `%varcstring335` character by character and ends by calling `~script223`,
     * which composes `<mode icon><chat_playername>: <col=…><typed></col>` plus
     * the `*` caret, sets the colour, shadow, alignment and ops, and does
     * `if_settext(…, interface_162:57)` itself — including the "You must set a
     * name before you can chat" branch when varbit 8119 is clear. Submitting is
     * script 73's too (`docheat` for `::`, `~script5517` for a public line).
     *
     * Writing this component from C therefore does not *add* a line, it
     * *overwrites* one, every frame, right before emit — and what it wrote was
     * built from `RS_Chat.input`, which at rev 230 is always empty because the
     * C key path is gated on `slots.chat_index`, a revconfig-baked tag a cache
     * gameframe does not have. The result was a chatbox that showed
     * `name: *` forever while the characters really were arriving, being
     * echoed by the script, and being sent on Enter — a live input line with a
     * dead one painted over it.
     *
     * Set it only for a revision whose chatbox is widgets AND whose scripts do
     * not own the input line. No such revision is known.
     */
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
