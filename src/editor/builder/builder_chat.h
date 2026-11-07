#ifndef SRC_EDITOR_BUILDER_BUILDER_CHAT_H
#define SRC_EDITOR_BUILDER_BUILDER_CHAT_H

/**
 * The chat box's line rule (design section 10, "Text fields wrap at the chat
 * box's own line rule and show a thin page-break line where the client would
 * page"): where a dialogue page's text breaks into lines, and which lines
 * share a page.
 *
 * THE RULE is the client's splitter, the one the server's `split_init`
 * repeats byte for byte (torirs_server_split.c `find_split`, deob class671):
 * advance through the text glyph by glyph; `|` and `<br>` break the line
 * hard; a tag `<...>` is widthless (`<lt>` / `<gt>` measure their glyph); the
 * line breaks at the last space once the width passes the box, a hyphen
 * breaking after itself when it does not itself overflow. Lines then fill
 * pages `page_lines` at a time.
 *
 * THE NUMBERS. The dialogue procs (`~chatnpc` and friends, OSRS-Content's
 * server/scripts) page their text through `split_init(text, width, lines,
 * p12_full)`. Their exact width and line count are the content's; this tree
 * builds without the OSRS-Content checkout, so the defaults below are the
 * editor's own reading of the chat box (the text column right of the 46 px
 * chathead in the 519 px box, four lines a page) and a caller with the
 * content's numbers passes its own. The glyph advances are the BAKED font
 * metrics of p12_full (archive 495, the chrome's Body face at 1x:
 * ToriRSChrome_GlyphAdvanceQ), not the cache: Builder_ChatMetricsBaked.
 *
 * `{player}` is laid out as a stand-in name (`player`, default
 * BUILDER_CHAT_PLAYER_STANDIN: twelve characters, the longest display name),
 * because the emitter writes it as `<displayname>`, which the compiler turns
 * into the player's name before split_init sees the text. The page breaks
 * shown are therefore where the client pages for the widest name; a shorter
 * name can only page later.
 *
 * Pure data: no chrome, no cache. Contract violations assert.
 */

#include "builder_model.h"

#include <stddef.h>

#define BUILDER_CHAT_WIDTH_DEFAULT 380
#define BUILDER_CHAT_PAGE_LINES_DEFAULT 4
/** The cache font the dialogue procs split with (p12_full). */
#define BUILDER_CHAT_FONT_ID 495
#define BUILDER_CHAT_PLAYER_STANDIN "Wwwwwwwwwwww"
/** Lines a page's text may break into here (a 511-byte page at the
 *  narrowest glyphs stays well inside). */
#define BUILDER_CHAT_LINES_MAX 48
/** The text after `{player}` became the stand-in. */
#define BUILDER_CHAT_TEXT_MAX (BUILDER_TEXT_MAX * 2)

struct Builder_ChatMetrics
{
    /** Each byte's advance in px (CP-1252, the font table's domain). */
    int advance[256];
    /** The box's text width and the lines a page holds. */
    int width;
    int page_lines;
    /** What `{player}` measures as; NULL = BUILDER_CHAT_PLAYER_STANDIN. */
    const char* player;
};

struct Builder_ChatLine
{
    /** The line's bytes in Builder_ChatLayout.text: [start, end). */
    int start;
    int end;
    /** Where the line starts in the SOURCE text (the node's own, with
     *  `{player}` unexpanded): what a page-break mark points at. */
    int source_start;
    /** 0-based page. */
    int page;
    /** Its width in px, tags excluded. */
    int width;
};

struct Builder_ChatLayout
{
    /** The text as laid out (`{player}` expanded). */
    char text[BUILDER_CHAT_TEXT_MAX];
    struct Builder_ChatLine lines[BUILDER_CHAT_LINES_MAX];
    int line_count;
    int page_count;
    /** The text had more lines than BUILDER_CHAT_LINES_MAX holds; the rest
     *  are dropped (and the panel says so). */
    int truncated;
};

/** `metrics` with the defaults above and the baked p12_full advances.
 *  Lives in builder_panels.c, which links the chrome's font tables. */
void
Builder_ChatMetricsBaked(struct Builder_ChatMetrics* metrics);

/** Lay `source` out under `metrics`. An empty text is one empty line on
 *  one page, as split_init returns it. */
void
Builder_ChatLayoutText(
    const char* source,
    const struct Builder_ChatMetrics* metrics,
    struct Builder_ChatLayout* out);

/**
 * Where the client starts pages 2, 3, ...: the source offsets of each
 * page's first line, at most `max`. Returns how many breaks there are
 * (page_count - 1).
 */
int
Builder_ChatPageBreaks(
    const struct Builder_ChatLayout* layout,
    int* out_offsets,
    int max);

/** Line `line`'s bytes copied into `out` (tags kept), terminated. */
void
Builder_ChatLineText(
    const struct Builder_ChatLayout* layout,
    int line,
    char* out,
    size_t out_size);

/** The text a node shows on its pages: NPC / PLAYER / MESBOX / OBJBOX the
 *  page, CHOICE its title, QUEST COMPLETE the scroll; NULL for the rest. */
const char*
Builder_ChatNodeText(const struct Builder_Node* node);

#endif
