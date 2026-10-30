/*
 * `split_init` line breaking, and specifically which markup a continuation row
 * inherits.
 *
 * Quest journal rows are single-line widgets, so the server breaks the prose
 * and each row is drawn by the client as an independent string with a fresh
 * style. That makes the carried-forward prefix the whole contract:
 *
 *   - colour must carry, because content breaks mid-phrase
 *     ("Wizard's Tower South West of|Lumbridge" wants both rows dark red);
 *   - strikethrough must NOT carry, because `^journal_done` is a bare `<str>`
 *     opened at the start of every completed step and never closed. Carry it
 *     and the first completed step strikes out every row below it — which is
 *     what the Restless Ghost journal looked like before this test existed:
 *     the current objective crossed out along with the finished ones.
 *
 * The reference is LostCity FontType.split, which keeps one `savedCol` and
 * lets `@str@` clear it rather than survive.
 *
 * Uses the real cache font metrics (archive 13, p12_full) at the journal's real
 * 415px row width, because a width-independent version of this test would not
 * exercise the soft-wrap branch at all. Run:
 *   make -C src test-torirsserver-split
 */

#include "torirs_server.h"

#include "ssvm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pack/13_fonts.pack: 495=p12_full, the journal row font. */
#define JOURNAL_FONT 495
/* ^questjournal_text_width */
#define JOURNAL_WIDTH 350
/* ^questjournal_max_lines */
#define JOURNAL_LINES 210

/*
 * The two seams `torirs_server_split.c` reaches out through. Supplying them here
 * rather than linking the world and the VM is what keeps this an instant test:
 * neither one takes part in any assertion below.
 */
const char*
ToriRSServer_WorldCacheDir(void)
{
    return TORIRSSERVER_CACHE_DIR_DEFAULT;
}

void
SSVM_Abort(struct SSVM_State* state, const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(state->err.message, sizeof(state->err.message), fmt, args);
    va_end(args);
    state->execution = SSVM_ABORTED;
}

static int Failures = 0;

static void
check(int condition, const char* what)
{
    printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if( !condition )
        Failures++;
}

static void
dump(struct SSVM_State* state)
{
    for( int i = 0; i < state->split_line_count; i++ )
        printf("    [%2d] %s\n", i, state->split_lines[i]);
}

/*
 * `~priest_journal` at %prieststart = ^priest_spoken_ghost, with the constants
 * from interface_questjournal/configs/questjournal.constant substituted in:
 * ^journal_done = "<str>", ^journal_todo = "<col=000080>",
 * ^journal_highlight = "<col=800000>".
 */
static const char* const RestlessGhost =
    "<str>Father Aereck asked me to help him deal with the Ghost in|"
    "<str>the graveyard next to the church.|"
    "<str>I found Father Urhney in the swamp south of Lumbridge. He|"
    "<str>gave me an Amulet of Ghostspeak to talk to the ghost.|"
    "<str>I spoke to the Ghost and he told me he could not rest in|"
    "<str>peace because an evil wizard had stolen his skull.|"
    "<col=000080>I should go and search the <col=800000>Wizard's Tower South West of|"
    "Lumbridge<col=000080> for the <col=800000>Ghost's Skull<col=000080>.";

static void
test_journal(struct SSVM_State* state)
{
    printf("\n-- The Restless Ghost, after speaking to the ghost --\n");
    if( !ToriRSServer_SplitInit(state, RestlessGhost, JOURNAL_WIDTH, JOURNAL_LINES, JOURNAL_FONT) )
    {
        printf("FAIL split_init: %s\n", state->err.message);
        Failures++;
        return;
    }
    dump(state);

    check(state->split_line_count == 8, "the eight hand-broken steps stay eight rows");
    if( state->split_line_count != 8 )
        return;

    for( int i = 0; i < 6; i++ )
    {
        char what[96];

        snprintf(what, sizeof(what), "completed step %d opens its own <str>", i + 1);
        check(strncmp(state->split_lines[i], "<str>", 5) == 0, what);
    }

    /* The bug: rows 7 and 8 are the current objective. Neither the source line
     * nor the row above it may put a strike on them. */
    check(
        strstr(state->split_lines[6], "<str>") == NULL,
        "the current objective is not struck through");
    check(
        strstr(state->split_lines[7], "<str>") == NULL,
        "its continuation row is not struck through either");

    /* ...while colour still crosses the same break, or "Lumbridge" would drop
     * out of the highlighted place name it is half of. */
    check(
        strncmp(state->split_lines[6], "<col=000080>", 12) == 0,
        "the objective row keeps its own opening colour");
    check(
        strncmp(state->split_lines[7], "<col=800000>", 12) == 0,
        "the continuation row inherits the colour open at the break");
}

static void
test_soft_wrap(struct SSVM_State* state)
{
    /* No `|` anywhere: the only break available is a width break. */
    static const char* const text =
        "<col=800000>A single unbroken sentence written to be far wider than one "
        "four-hundred-and-fifteen pixel journal row can hold, so that the split "
        "has to find a space to cut on rather than a pipe.";

    printf("\n-- soft wrap, colour open across the break --\n");
    if( !ToriRSServer_SplitInit(state, text, JOURNAL_WIDTH, JOURNAL_LINES, JOURNAL_FONT) )
    {
        printf("FAIL split_init: %s\n", state->err.message);
        Failures++;
        return;
    }
    dump(state);

    check(state->split_line_count > 1, "an over-wide line actually wraps");
    if( state->split_line_count < 2 )
        return;
    check(
        strncmp(state->split_lines[1], "<col=800000>", 12) == 0,
        "a width break re-emits the open colour");
}

static void
test_no_leak_without_close(struct SSVM_State* state)
{
    /* `<str>` with no `</str>`, the shape all journal content uses, and a hard
     * break straight after it. */
    printf("\n-- an unclosed <str> ends at its own hard break --\n");
    if( !ToriRSServer_SplitInit(state, "<str>done|next", JOURNAL_WIDTH, JOURNAL_LINES, JOURNAL_FONT) )
    {
        printf("FAIL split_init: %s\n", state->err.message);
        Failures++;
        return;
    }
    dump(state);

    check(state->split_line_count == 2, "one pipe makes two rows");
    if( state->split_line_count != 2 )
        return;
    check(strcmp(state->split_lines[0], "<str>done") == 0, "the struck row is unchanged");
    check(strcmp(state->split_lines[1], "next") == 0, "the row after it carries nothing");
}

static void
test_own_colour_wins(struct SSVM_State* state)
{
    /* Fishing Contest at stage 2 (RSProx index 4404 [677]): the third row ends
     * in <col=800000>, the fourth opens with <col=000080>. Jagex sends the
     * fourth with no carried tag in front of its own. */
    static const char* const text =
        "<col=000080>My <col=800000>fishing spot<col=000080> in the contest is <col=800000>next to the willow tree|"
        "<col=000080>I need to catch the <col=800000>biggest fish<col=000080> to win!|"
        "after";

    printf("\n-- a row that opens its own colour --\n");
    if( !ToriRSServer_SplitInit(state, text, JOURNAL_WIDTH, JOURNAL_LINES, JOURNAL_FONT) )
    {
        printf("FAIL split_init: %s\n", state->err.message);
        Failures++;
        return;
    }
    dump(state);

    check(state->split_line_count == 3, "two pipes make three rows");
    if( state->split_line_count != 3 )
        return;
    check(
        strncmp(state->split_lines[1], "<col=000080>I need", 18) == 0,
        "the row's own colour is not preceded by the carried one");
    check(
        strncmp(state->split_lines[2], "<col=000080>after", 17) == 0,
        "a row with no colour of its own still inherits the open one");
}

static void
test_wrap_restates_colour(struct SSVM_State* state)
{
    /* Scorpion Catcher at stage 2 (RSProx index 4421 [1730]): the row wraps
     * in <col=000080> right before a <col=800000> name. Jagex restates the
     * carried colour in front of the row's own, as in every one of the 51
     * such rows recorded; a hard break (above) does not. */
    static const char* const text =
        "<col=000080>I need to talk to the <col=800000>Seer<col=000080> again now I've caught the first <col=800000>Kharid Scorpion.";

    printf("\n-- a wrap before a row's own colour --\n");
    if( !ToriRSServer_SplitInit(state, text, JOURNAL_WIDTH, JOURNAL_LINES, JOURNAL_FONT) )
    {
        printf("FAIL split_init: %s\n", state->err.message);
        Failures++;
        return;
    }
    dump(state);

    check(state->split_line_count == 2, "the paragraph wraps once");
    if( state->split_line_count != 2 )
        return;
    check(
        strcmp(state->split_lines[0], "<col=000080>I need to talk to the <col=800000>Seer<col=000080> again now I've caught the first") == 0,
        "the first row is Jagex's");
    check(
        strcmp(state->split_lines[1], "<col=000080><col=800000>Kharid Scorpion.") == 0,
        "the wrapped row restates the carried colour before its own");
}

static void
test_blank_row_carries_nothing(struct SSVM_State* state)
{
    /* Death Plateau at stage 30 (RSProx index 4431 [726]): the current step,
     * left in <col=000080>, then a blank row and a struck paragraph. Jagex
     * sends the blank row and the struck row bare. */
    static const char* const text =
        "<col=000080>I have offered to help <col=800000>Denulth<col=000080> the door.||<str>The equipment room guard";

    printf("\n-- a blank row between paragraphs --\n");
    if( !ToriRSServer_SplitInit(state, text, JOURNAL_WIDTH, JOURNAL_LINES, JOURNAL_FONT) )
    {
        printf("FAIL split_init: %s\n", state->err.message);
        Failures++;
        return;
    }
    dump(state);

    check(state->split_line_count == 3, "three rows: the step, the blank row, the struck row");
    if( state->split_line_count != 3 )
        return;
    check(strcmp(state->split_lines[1], "") == 0, "the blank row carries no colour");
    check(strcmp(state->split_lines[2], "<str>The equipment room guard") == 0, "the struck row starts bare");

    /* [1437]: a struck item after one still to get, one hard break apart. */
    static const char* const items = "<col=800000>Ten loaves of bread.|<str>Ten cooked trout.";

    printf("\n-- a struck row after a coloured one --\n");
    if( !ToriRSServer_SplitInit(state, items, JOURNAL_WIDTH, JOURNAL_LINES, JOURNAL_FONT) )
    {
        printf("FAIL split_init: %s\n", state->err.message);
        Failures++;
        return;
    }
    dump(state);
    check(state->split_line_count == 2 && strcmp(state->split_lines[1], "<str>Ten cooked trout.") == 0,
        "a struck row after a hard break carries no colour");
}

int
main(void)
{
    struct SSVM_State* state = calloc(1, sizeof(*state));

    if( !state )
    {
        printf("FAIL out of memory\n");
        return 1;
    }
    SSVM_StrPoolInit(&state->pool);

    test_journal(state);
    test_soft_wrap(state);
    test_no_leak_without_close(state);
    test_own_colour_wins(state);
    test_wrap_restates_colour(state);
    test_blank_row_carries_nothing(state);

    SSVM_StrPoolFree(&state->pool);
    free(state);

    printf("\n%s (%d failure%s)\n", Failures ? "FAILED" : "PASSED", Failures,
           Failures == 1 ? "" : "s");
    return Failures ? 1 : 0;
}
