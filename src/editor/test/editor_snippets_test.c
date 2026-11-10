/*
 * The RuneScript snippet catalogue (src/editor/editor_snippets.c), content
 * free: no content tree, no compiler, no chrome.
 *
 *   ./editor_snippets_test
 *
 * Every snippet of every kind renders for a short and for the longest record
 * name; its text names the record, starts with the `//` line that says which
 * kind of file it goes in, leaves no template mark behind, balances its
 * braces, parentheses, brackets and string quotes, keeps every line inside
 * what a chrome row holds, and ends with a line break. The titles fit a
 * dropdown row and do not repeat within a list. That the scripts COMPILE is
 * not checked here (the catalogue cites the files each idiom is copied
 * from); `make -C src test-editor-script` is the gate that runs sscompile.
 */

#include "editor/editor_snippets.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

/** A chrome label / dropdown row (TORIRS_CHROME_LABEL_MAX) and a list row
 *  (TORIRS_CHROME_INPUT_MAX), terminator included: restated, so this test
 *  links nothing but the catalogue. */
#define TITLE_MAX 64
#define LINE_MAX_BYTES 192

static const char* const k_kind_names[EDITOR_SNIPPET_KIND_COUNT] = { "npc", "loc", "obj", "any" };

/**
 * 1 when `text` balances: (), {} and [] nest and close outside string
 * literals, and every double quote opened on a line is closed on it (a
 * RuneScript string does not span lines; neither does a quoted title in a
 * comment). `why` says what did not.
 */
static int
balanced(
    const char* text,
    char* why,
    size_t why_size)
{
    char stack[128];
    int depth = 0;
    int in_string = 0;
    int line = 1;

    why[0] = '\0';
    for( const char* p = text; *p; p++ )
    {
        char const c = *p;
        if( c == '\n' )
        {
            if( in_string )
            {
                snprintf(why, why_size, "line %d ends inside a string", line);
                return 0;
            }
            line++;
            continue;
        }
        if( c == '"' )
        {
            in_string = !in_string;
            continue;
        }
        if( in_string )
            continue;
        if( c == '(' || c == '{' || c == '[' )
        {
            if( depth == (int)sizeof(stack) )
            {
                snprintf(why, why_size, "line %d nests too deep", line);
                return 0;
            }
            stack[depth++] = c;
        }
        else if( c == ')' || c == '}' || c == ']' )
        {
            char const open = c == ')' ? '(' : c == '}' ? '{' : '[';
            if( depth == 0 || stack[depth - 1] != open )
            {
                snprintf(why, why_size, "line %d: '%c' closes nothing", line, c);
                return 0;
            }
            depth--;
        }
    }
    if( depth != 0 )
    {
        snprintf(why, why_size, "'%c' is never closed", stack[depth - 1]);
        return 0;
    }
    return 1;
}

/** The longest line of `text`, in bytes. */
static size_t
longest_line(const char* text)
{
    size_t longest = 0;
    size_t run = 0;
    for( const char* p = text; *p; p++ )
    {
        if( *p == '\n' )
        {
            longest = run > longest ? run : longest;
            run = 0;
        }
        else
            run++;
    }
    return run > longest ? run : longest;
}

static void
check_snippet(
    enum Editor_SnippetKind kind,
    int index,
    const char* subject)
{
    char text[EDITOR_SNIPPET_MAX];
    char why[96];
    const char* title = Editor_SnippetTitle(kind, index);
    enum Editor_SnippetFile const file = Editor_SnippetFileKind(kind, index);
    const char* extension = Editor_SnippetFileExtension(file);
    char first[64];
    size_t length;

    memset(text, 'x', sizeof(text));
    length = Editor_SnippetRender(kind, index, subject, text, sizeof(text));
    CHECK(length > 0 && length < sizeof(text) && text[length] == '\0' && strlen(text) == length,
        "%s %d (%s): renders, and returns its length (%u)", k_kind_names[kind], index, title, (unsigned)length);
    if( length == 0 || length >= sizeof(text) )
        return;
    CHECK(strstr(text, subject) != NULL, "%s %d (%s): names the record %s", k_kind_names[kind], index, title, subject);
    CHECK(text[length - 1] == '\n', "%s %d (%s): ends with a line break", k_kind_names[kind], index, title);
    CHECK(!strstr(text, "{s}") && !strstr(text, "{t}") && !strstr(text, "{T}"),
        "%s %d (%s): no template mark is left in the text", k_kind_names[kind], index, title);
    CHECK(!strchr(text, '\r') && !strchr(text, '\t'), "%s %d (%s): LF line ends, spaces to indent",
        k_kind_names[kind], index, title);
    /* Where it goes, said first: "// Goes in a .rs2 script file." */
    snprintf(first, sizeof(first), "// Goes in a %s ", extension);
    CHECK(strncmp(text, first, strlen(first)) == 0, "%s %d (%s): the first line says it goes in a %s file",
        k_kind_names[kind], index, title, extension);
    CHECK(balanced(text, why, sizeof(why)), "%s %d (%s): %s", k_kind_names[kind], index, title, why);
    CHECK(longest_line(text) < LINE_MAX_BYTES, "%s %d (%s): every line fits a preview row (%u bytes)",
        k_kind_names[kind], index, title, (unsigned)longest_line(text));
    /* A script snippet declares a trigger, a proc or a cheat for the record;
     * a varp block and a constant are named for it too. */
    {
        char header[EDITOR_SNIPPET_SUBJECT_MAX + 8];
        if( file == EDITOR_SNIPPET_FILE_VARP )
            snprintf(header, sizeof(header), "\n[%s", subject);
        else if( file == EDITOR_SNIPPET_FILE_CONSTANT )
            snprintf(header, sizeof(header), "\n^%s", subject);
        else
            snprintf(header, sizeof(header), ",%s", subject);
        CHECK(strstr(text, header) != NULL, "%s %d (%s): declares something named for %s", k_kind_names[kind], index,
            title, subject);
    }
}

static void
test_catalogue(void)
{
    /* 63 bytes: the longest name a record has (EDITOR_SNIPPET_SUBJECT_MAX). */
    static const char k_long[] = "a_record_name_that_is_exactly_as_long_as_a_record_name_can_be_x";
    int total = 0;

    CHECK(sizeof(k_long) == EDITOR_SNIPPET_SUBJECT_MAX, "the long name is the longest a record has (%u)",
        (unsigned)sizeof(k_long));
    for( int k = 0; k < EDITOR_SNIPPET_KIND_COUNT; k++ )
    {
        enum Editor_SnippetKind const kind = (enum Editor_SnippetKind)k;
        int const count = Editor_SnippetCount(kind);
        CHECK(count > 0, "%s: has snippets", k_kind_names[k]);
        for( int i = 0; i < count; i++ )
        {
            const char* title = Editor_SnippetTitle(kind, i);
            CHECK(title && title[0] && strlen(title) < TITLE_MAX, "%s %d: a title that fits a dropdown row",
                k_kind_names[k], i);
            for( int j = 0; j < i; j++ )
                CHECK(strcmp(title, Editor_SnippetTitle(kind, j)) != 0, "%s: \"%s\" is listed once", k_kind_names[k],
                    title);
            check_snippet(kind, i, "fenwick");
            check_snippet(kind, i, "fenwick_crate");
            check_snippet(kind, i, k_long);
            total++;
        }
    }
    /* The brief's floor: five ways to talk, four things a loc does, an
     * inventory op, and the four things no record owns. */
    CHECK(Editor_SnippetCount(EDITOR_SNIPPET_NPC) >= 5 && Editor_SnippetCount(EDITOR_SNIPPET_LOC) >= 4
              && Editor_SnippetCount(EDITOR_SNIPPET_OBJ) >= 1 && Editor_SnippetCount(EDITOR_SNIPPET_ANY) >= 4,
        "the catalogue covers npc, loc, obj and any (%d in all)", total);
}

static void
test_kinds(void)
{
    CHECK(Editor_SnippetKindOf("npc") == EDITOR_SNIPPET_NPC && Editor_SnippetKindOf("loc") == EDITOR_SNIPPET_LOC
              && Editor_SnippetKindOf("obj") == EDITOR_SNIPPET_OBJ,
        "kinds: npc, loc and obj have their own lists");
    CHECK(Editor_SnippetKindOf("") == EDITOR_SNIPPET_ANY && Editor_SnippetKindOf("varp") == EDITOR_SNIPPET_ANY,
        "kinds: anything else has the any list only");
    CHECK(strcmp(Editor_SnippetFileExtension(EDITOR_SNIPPET_FILE_SCRIPT), ".rs2") == 0
              && strcmp(Editor_SnippetFileExtension(EDITOR_SNIPPET_FILE_VARP), ".varp") == 0
              && strcmp(Editor_SnippetFileExtension(EDITOR_SNIPPET_FILE_CONSTANT), ".constant") == 0,
        "files: the three extensions");
}

/** What the text says, spot-checked against the tree's own spellings. */
static void
test_words(void)
{
    char text[EDITOR_SNIPPET_MAX];
    int found_start = 0;
    int found_varp = 0;
    int found_constant = 0;
    int found_proc = 0;
    int found_cheat = 0;
    int found_loc_change = 0;
    int found_space = 0;
    int found_choice = 0;
    int found_held = 0;

    for( int k = 0; k < EDITOR_SNIPPET_KIND_COUNT; k++ )
        for( int i = 0; i < Editor_SnippetCount((enum Editor_SnippetKind)k); i++ )
        {
            (void)Editor_SnippetRender((enum Editor_SnippetKind)k, i, "fenwick_crate", text, sizeof(text));
            found_start |= k == EDITOR_SNIPPET_NPC
                           && strstr(text, "~p_choice2_header(\"Yes.\", 1, \"No.\", 2, \"Start the Fenwick crate quest?\")")
                                  != NULL;
            found_choice |= k == EDITOR_SNIPPET_NPC && strstr(text, "[opnpc1,fenwick_crate]\n")
                            && strstr(text, "if (~p_choice2(") != NULL;
            found_space |= strstr(text, "if (inv_freespace(inv) < 1) {\n") && strstr(text, "inv_add(inv, ");
            found_loc_change |= k == EDITOR_SNIPPET_LOC && strstr(text, "[oploc1,fenwick_crate]\nloc_change(") != NULL;
            found_held |= k == EDITOR_SNIPPET_OBJ && strstr(text, "[opheld1,fenwick_crate]\nmes(\"") != NULL;
            found_varp |= k == EDITOR_SNIPPET_ANY && strstr(text, "\n[fenwick_crate_quest]\nscope=perm\n")
                          && strstr(text, "first RUN") && strstr(text, "[varp<N>_fenwick_crate_quest]");
            found_constant |= k == EDITOR_SNIPPET_ANY && strstr(text, "\n^fenwick_crate_not_started = 0\n") != NULL;
            found_proc |= k == EDITOR_SNIPPET_ANY && strstr(text, "\n[proc,fenwick_crate_ready]()(boolean)\n")
                          && strstr(text, "return(true);\n");
            found_cheat |= k == EDITOR_SNIPPET_ANY && strstr(text, "\n[debugproc,fenwick_crate]\n") != NULL;
        }
    CHECK(found_choice, "npc: a Talk-to with a two-option choice");
    CHECK(found_start, "npc: the quest offer asks \"Start the <Quest> quest?\", the record's name in words");
    CHECK(found_space, "an item is given only after inv_freespace says there is room");
    CHECK(found_loc_change, "loc: an op that changes the loc for a while");
    CHECK(found_held, "obj: an inventory op that prints a message");
    CHECK(found_varp, "any: a varp block written [<name>], with the first RUN's renaming said");
    CHECK(found_constant, "any: the stage list");
    CHECK(found_proc, "any: a proc that answers true or false");
    CHECK(found_cheat, "any: a debugproc cheat");
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_kinds();
    test_catalogue();
    test_words();
    printf("editor_snippets_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
