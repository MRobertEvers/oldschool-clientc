#ifndef SRC_UI_CHAT_LINE_H
#define SRC_UI_CHAT_LINE_H

/*
 * Who the typed chat line belongs to right now.
 *
 * Two shapes of chatbox, two answers, one line.
 *
 * Where the client paints its own chatbox (a revconfig tree's chat region),
 * the line is FOCUSED or not. While it is, typed keys are text for the line
 * and the debug hotkeys stay quiet; while it is not, every key belongs to the
 * hotkeys. Clicking the region or pressing Enter focuses it; clicking
 * elsewhere, Escape and sending the line drop it.
 *
 * Where the chatbox is the cache's own interface and the line is kept closed
 * until the player opens it (the web's press-Enter-to-chat, where the digit
 * row is the F-key row), the line is OPEN or not. While it is closed a prompt
 * covers what was typed. The cover remembers both what it covered and the
 * prompt it wrote, so opening the line puts the covered text back only if the
 * line still shows that prompt, and a fresh write by the line's own script --
 * anything that is not the prompt -- is covered again rather than mistaken
 * for it. Opening or closing the line forgets both.
 *
 * The prompt keeps the line's lead-in up to and including the `<col=...>` of
 * the typed part (`<icon><name>:<nbsp><col=...>`) and replaces what follows
 * with "Click here or press Enter to chat...</col>". A line with no colon, or
 * no colour tag after it, has nothing typed to cover and is left alone.
 *
 * This does not know about a widget tree, a chat store, a key or a pointer.
 * The caller finds the line and reads the text it shows, writes back the text
 * this hands it, decides which clicks and keys focus, open or close the line,
 * and decides what a change redraws.
 */

/** The cover's text buffers, each the size the line's text is cut to. */
#define UI_CHAT_LINE_TEXT_CAP 1024

struct UIChatLine
{
    /** The client-painted line has the keyboard. 0 or 1. */
    int input_active;
    /** The cache line is open for typing. 0 or 1. */
    int open;
    /** What the prompt covered, kept while it covers it; empty when nothing
     *  is covered. */
    char saved[UI_CHAT_LINE_TEXT_CAP];
    /** The prompt as last written over the line; empty when nothing is
     *  covered. */
    char prompt[UI_CHAT_LINE_TEXT_CAP];
};

/** Unfocused, closed, nothing covered. Also the state of a zeroed struct. */
void
UIChatLine_Reset(struct UIChatLine* line);

/** The client-painted line has the keyboard: 1 or 0. */
int
UIChatLine_InputActive(struct UIChatLine const* line);

/** Give the client-painted line the keyboard (nonzero) or take it away. */
void
UIChatLine_SetInputActive(
    struct UIChatLine* line,
    int active);

/** The cache line is open for typing: 1 or 0. */
int
UIChatLine_Open(struct UIChatLine const* line);

/**
 * What opening the line should put back: the covered text, when something is
 * covered and the line still `shown`s the prompt written over it; else NULL.
 * The pointer is into `line` and is good until the next call that changes it.
 */
char const*
UIChatLine_CoveredText(
    struct UIChatLine const* line,
    char const* shown);

/**
 * Open (nonzero) or close the line, forgetting what was covered and the
 * prompt. Take UIChatLine_CoveredText first: this empties it.
 */
void
UIChatLine_SetOpen(
    struct UIChatLine* line,
    int open);

/**
 * Cover what the line `shown`s with the prompt.
 *
 * Returns the prompt to write over the line, having remembered `shown` and the
 * prompt; or NULL, changing nothing, when the line already shows the prompt or
 * has no typed part to cover. Both are cut to UI_CHAT_LINE_TEXT_CAP. Asks
 * nothing about whether the line is open: the caller covers only a closed one.
 */
char const*
UIChatLine_Cover(
    struct UIChatLine* line,
    char const* shown);

#endif /* SRC_UI_CHAT_LINE_H */
