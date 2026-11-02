/*
 * enum LibToriRS_KeyCode's config names, without a window.
 *
 * One vocabulary serves the manifest's [debug:hotkeys], the editor's hotkeys
 * file and TORIRS_SIM_HOTKEY, so what is pinned here is that the vocabulary
 * is complete and unambiguous:
 *
 *   1. Every code from TORIRSK_A to TORIRSK_F12 has a name, and that name
 *      reads back as the same code -- a key the editor can bind is a key the
 *      file can write and the next boot can read.
 *   2. No two codes share a name (the round trip would catch the second).
 *   3. The spellings older manifests use ("enter", "page_up", "page_down")
 *      still resolve, and case is ignored.
 *   4. Anything else is TORIRSK_UNKNOWN, never a near miss.
 */

#include "input/torirs_input.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, what, #cond);           \
        }                                                                                          \
    } while( 0 )

int
main(void)
{
    /* ---- every code round-trips ---- */
    for( int k = TORIRSK_A; k <= TORIRSK_F12; k++ )
    {
        char const* name = LibToriRS_KeyCodeName((enum LibToriRS_KeyCode)k);
        char upper[32];
        size_t n;

        CHECK(name != NULL, "every key code has a name");
        if( !name )
            continue;
        CHECK(name[0] != '\0', "and the name is not empty");
        CHECK(LibToriRS_KeyCodeFromName(name) == (enum LibToriRS_KeyCode)k,
            "the name reads back as the same code");

        n = strlen(name);
        CHECK(n < sizeof(upper), "names are short");
        if( n >= sizeof(upper) )
            continue;
        for( size_t i = 0; i <= n; i++ )
            upper[i] = (char)toupper((unsigned char)name[i]);
        CHECK(LibToriRS_KeyCodeFromName(upper) == (enum LibToriRS_KeyCode)k,
            "and so does its upper-case spelling");
    }
    CHECK(TORIRSK_F12 + 1 == TORIRSK_COUNT, "the loop above covers the whole enum");

    /* ---- the names the editor's defaults and docs use ---- */
    CHECK(strcmp(LibToriRS_KeyCodeName(TORIRSK_LBRACKET), "lbracket") == 0, "lbracket");
    CHECK(strcmp(LibToriRS_KeyCodeName(TORIRSK_F1), "f1") == 0, "f1");
    CHECK(strcmp(LibToriRS_KeyCodeName(TORIRSK_PAGE_UP), "pageup") == 0, "pageup is canonical");
    CHECK(strcmp(LibToriRS_KeyCodeName(TORIRSK_RETURN), "return") == 0, "return is canonical");
    CHECK(strcmp(LibToriRS_KeyCodeName(TORIRSK_CTRL), "ctrl") == 0, "ctrl");
    CHECK(strcmp(LibToriRS_KeyCodeName(TORIRSK_ALT), "alt") == 0, "alt");

    /* ---- aliases older manifests spell ---- */
    CHECK(LibToriRS_KeyCodeFromName("enter") == TORIRSK_RETURN, "enter is return");
    CHECK(LibToriRS_KeyCodeFromName("Enter") == TORIRSK_RETURN, "Enter, any case");
    CHECK(LibToriRS_KeyCodeFromName("page_up") == TORIRSK_PAGE_UP, "page_up");
    CHECK(LibToriRS_KeyCodeFromName("PAGE_DOWN") == TORIRSK_PAGE_DOWN, "PAGE_DOWN");
    CHECK(LibToriRS_KeyCodeFromName("F10") == TORIRSK_F10, "F10");
    CHECK(LibToriRS_KeyCodeFromName("LBracket") == TORIRSK_LBRACKET, "LBracket");

    /* ---- anything else is unknown ---- */
    CHECK(LibToriRS_KeyCodeFromName("") == TORIRSK_UNKNOWN, "empty");
    CHECK(LibToriRS_KeyCodeFromName("f13") == TORIRSK_UNKNOWN, "f13");
    CHECK(LibToriRS_KeyCodeFromName("f0") == TORIRSK_UNKNOWN, "f0");
    CHECK(LibToriRS_KeyCodeFromName("numpad5") == TORIRSK_UNKNOWN, "numpad5");
    CHECK(LibToriRS_KeyCodeFromName("ctrl+s") == TORIRSK_UNKNOWN, "a chord is not a key name");
    CHECK(LibToriRS_KeyCodeFromName("aa") == TORIRSK_UNKNOWN, "aa");
    CHECK(LibToriRS_KeyCodeFromName("esc") == TORIRSK_UNKNOWN, "esc is revconfig's, not ours");
    CHECK(LibToriRS_KeyCodeFromName("pageu") == TORIRSK_UNKNOWN, "a prefix is not a match");
    CHECK(LibToriRS_KeyCodeFromName("pageupx") == TORIRSK_UNKNOWN, "nor an extension");
    CHECK(LibToriRS_KeyCodeName(TORIRSK_UNKNOWN) == NULL, "TORIRSK_UNKNOWN has no name");
    CHECK(LibToriRS_KeyCodeName(TORIRSK_COUNT) == NULL, "nor does the count");

    printf("torirs_keymap_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
