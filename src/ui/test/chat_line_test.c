/*
 * The typed chat line's focus, open state and prompt cover (ui/chat_line.h).
 *
 * Focus set and dropped; the cover writing the prompt from the line's lead-in
 * and remembering what it covered; a line already showing the prompt, and
 * lines with no colon or no colour tag, left alone; opening handing back the
 * covered text only while the prompt is still shown, and forgetting both; a
 * fresh write by the line's script covered again; an over-long line cut to
 * the buffer.
 *
 * Run: make -C src test-chat-line
 */

#include "ui/chat_line.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* `<img=0>Zezima:<nbsp><col=0000ff>hello</col>*`, the nbsp as cp1252 0xA0. */
static char const k_typed[] = "<img=0>Zezima:\xa0<col=0000ff>hello</col>*";
static char const k_prompt[] =
    "<img=0>Zezima:\xa0<col=0000ff>Click here or press Enter to chat...</col>";

static void
test_reset_is_unfocused_closed_and_empty(void)
{
    struct UIChatLine line;
    memset(&line, 0x5a, sizeof(line));
    UIChatLine_Reset(&line);
    TEST_ASSERT(UIChatLine_InputActive(&line) == 0, "reset drops focus");
    TEST_ASSERT(UIChatLine_Open(&line) == 0, "reset closes the line");
    TEST_ASSERT(line.saved[0] == '\0' && line.prompt[0] == '\0', "reset covers nothing");
}

static void
test_focus(void)
{
    struct UIChatLine line;
    UIChatLine_Reset(&line);
    UIChatLine_SetInputActive(&line, 1);
    TEST_ASSERT(UIChatLine_InputActive(&line) == 1, "focus taken");
    UIChatLine_SetInputActive(&line, 0);
    TEST_ASSERT(UIChatLine_InputActive(&line) == 0, "focus dropped");
    UIChatLine_SetInputActive(&line, 1);
    UIChatLine_SetOpen(&line, 1);
    TEST_ASSERT(UIChatLine_InputActive(&line) == 1, "opening the cache line leaves the focus");
}

static void
test_cover_writes_the_prompt(void)
{
    struct UIChatLine line;
    char const* cover;

    UIChatLine_Reset(&line);
    cover = UIChatLine_Cover(&line, k_typed);
    TEST_ASSERT(cover != NULL, "a typed line is covered");
    TEST_ASSERT(cover && strcmp(cover, k_prompt) == 0, "the prompt keeps the lead-in and colour");
    TEST_ASSERT(strcmp(line.saved, k_typed) == 0, "the covered text is remembered");
    TEST_ASSERT(strcmp(line.prompt, k_prompt) == 0, "the prompt is remembered");

    /* The line now shows the prompt: nothing to do. */
    TEST_ASSERT(UIChatLine_Cover(&line, k_prompt) == NULL, "the prompt is not covered again");
    TEST_ASSERT(strcmp(line.saved, k_typed) == 0, "and what it covers is kept");
}

static void
test_cover_leaves_lines_with_nothing_typed(void)
{
    struct UIChatLine line;

    UIChatLine_Reset(&line);
    TEST_ASSERT(UIChatLine_Cover(&line, "Set a name") == NULL, "no colon, not covered");
    TEST_ASSERT(UIChatLine_Cover(&line, "Zezima: hello") == NULL, "no colour tag, not covered");
    TEST_ASSERT(
        UIChatLine_Cover(&line, "<col=ff0000>x</col> Zezima: hi") == NULL,
        "a colour tag only before the colon, not covered");
    TEST_ASSERT(
        UIChatLine_Cover(&line, "Zezima:\xa0<col=0000ff") == NULL,
        "an unclosed colour tag, not covered");
    TEST_ASSERT(line.saved[0] == '\0' && line.prompt[0] == '\0', "a refusal remembers nothing");
}

static void
test_open_restores_only_under_the_prompt(void)
{
    struct UIChatLine line;
    char const* covered;

    UIChatLine_Reset(&line);
    TEST_ASSERT(
        UIChatLine_CoveredText(&line, "") == NULL,
        "nothing covered, nothing to put back (even matching the empty prompt)");

    UIChatLine_Cover(&line, k_typed);
    covered = UIChatLine_CoveredText(&line, k_prompt);
    TEST_ASSERT(covered && strcmp(covered, k_typed) == 0, "under the prompt, the covered text");
    TEST_ASSERT(
        UIChatLine_CoveredText(&line, "<img=0>Zezima:\xa0<col=0000ff>new</col>*") == NULL,
        "after the script rewrote the line, nothing to put back");

    UIChatLine_SetOpen(&line, 1);
    TEST_ASSERT(UIChatLine_Open(&line) == 1, "the line is open");
    TEST_ASSERT(line.saved[0] == '\0' && line.prompt[0] == '\0', "opening forgets the cover");
    TEST_ASSERT(UIChatLine_CoveredText(&line, k_prompt) == NULL, "and has nothing to put back");

    UIChatLine_SetOpen(&line, 0);
    TEST_ASSERT(UIChatLine_Open(&line) == 0, "the line is closed");
}

static void
test_fresh_write_is_covered_again(void)
{
    struct UIChatLine line;
    static char const k_fresh[] = "<img=0>Zezima:\xa0<col=0000ff>hello there</col>*";
    char const* cover;

    UIChatLine_Reset(&line);
    UIChatLine_Cover(&line, k_typed);
    cover = UIChatLine_Cover(&line, k_fresh);
    TEST_ASSERT(cover && strcmp(cover, k_prompt) == 0, "the fresh write is covered");
    TEST_ASSERT(strcmp(line.saved, k_fresh) == 0, "the fresh write is what is remembered");
}

static void
test_long_line_is_cut(void)
{
    struct UIChatLine line;
    char shown[UI_CHAT_LINE_TEXT_CAP + 200];
    char const* cover;
    size_t const lead = strlen("Zezima:\xa0<col=0000ff>");

    memcpy(shown, "Zezima:\xa0<col=0000ff>", lead);
    memset(shown + lead, 'a', sizeof(shown) - lead - 1);
    shown[sizeof(shown) - 1] = '\0';

    UIChatLine_Reset(&line);
    cover = UIChatLine_Cover(&line, shown);
    TEST_ASSERT(cover != NULL, "a long line is covered");
    TEST_ASSERT(strlen(line.saved) == UI_CHAT_LINE_TEXT_CAP - 1, "the covered text is cut to fit");
    TEST_ASSERT(strncmp(line.saved, shown, UI_CHAT_LINE_TEXT_CAP - 1) == 0, "cut, not changed");
    TEST_ASSERT(
        cover &&
            strcmp(cover, "Zezima:\xa0<col=0000ff>Click here or press Enter to chat...</col>") == 0,
        "the prompt is built from the uncut lead-in");
    /* The line shows the whole text, not the cut copy: opening puts nothing back
     * unless the prompt is what it shows. */
    TEST_ASSERT(
        UIChatLine_CoveredText(&line, shown) == NULL, "the uncovered line puts nothing back");
}

int
main(void)
{
    test_reset_is_unfocused_closed_and_empty();
    test_focus();
    test_cover_writes_the_prompt();
    test_cover_leaves_lines_with_nothing_typed();
    test_open_restores_only_under_the_prompt();
    test_fresh_write_is_covered_again();
    test_long_line_is_cut();

    if( g_failures )
    {
        printf("chat_line_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("chat_line_test: ok\n");
    return 0;
}
