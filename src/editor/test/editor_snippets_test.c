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
 * dropdown row and do not repeat within a list. A record's snippet reads the
 * variable of the quest being worked on; the five a quest with a fight, an
 * arena and a scene needs say what their model files say; and a new quest's
 * three files agree with the snippets. That the scripts COMPILE is
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
    length = Editor_SnippetRender(kind, index, subject, NULL, NULL, text, sizeof(text));
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
        if( file == EDITOR_SNIPPET_FILE_VARP || file == EDITOR_SNIPPET_FILE_NPC )
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
              && strcmp(Editor_SnippetFileExtension(EDITOR_SNIPPET_FILE_CONSTANT), ".constant") == 0
              && strcmp(Editor_SnippetFileExtension(EDITOR_SNIPPET_FILE_NPC), ".npc") == 0,
        "files: the four extensions");
}

/** The snippet of `kind` titled `title`, rendered for `subject` and `quest`
 *  into `text` (EDITOR_SNIPPET_MAX). 1 when the catalogue has it. */
static int
render_titled(
    enum Editor_SnippetKind kind,
    const char* title,
    const char* subject,
    const char* quest,
    char* text)
{
    for( int i = 0; i < Editor_SnippetCount(kind); i++ )
        if( strcmp(Editor_SnippetTitle(kind, i), title) == 0 )
        {
            (void)Editor_SnippetRender(kind, i, subject, quest, NULL, text, EDITOR_SNIPPET_MAX);
            return 1;
        }
    text[0] = '\0';
    return 0;
}

/** The quest being worked on: a record's snippet reads THAT quest's variable
 *  and stages, and still names the record where a record's name belongs. */
static void
test_quest_names(void)
{
    static char text[EDITOR_SNIPPET_MAX];
    static char plain[EDITOR_SNIPPET_MAX];

    CHECK(render_titled(EDITOR_SNIPPET_NPC, "Talk-to: offer a quest", "fenwick", "thistle", text),
        "quest: the npc's quest offer is in the catalogue");
    CHECK(strstr(text, "[opnpc1,fenwick]\n") && strstr(text, "if (%thistle_quest ! ^thistle_not_started) {\n")
              && strstr(text, "    %thistle_quest = ^thistle_started;\n")
              && strstr(text, "\"Start the Thistle quest?\"") && strstr(text, "@q_mes@Thistle</col>"),
        "quest: the offer is the npc's trigger and the quest's variable, stages and title");
    CHECK(!strstr(text, "fenwick_quest") && !strstr(text, "^fenwick_"), "quest: nothing of the quest is named for the npc");
    CHECK(!strstr(text, "{q}") && !strstr(text, "{Q}"), "quest: no quest mark is left in the text");
    (void)render_titled(EDITOR_SNIPPET_NPC, "Talk-to: offer a quest", "fenwick", NULL, plain);
    (void)render_titled(EDITOR_SNIPPET_NPC, "Talk-to: offer a quest", "fenwick", "", text);
    CHECK(strcmp(text, plain) == 0 && strstr(plain, "%fenwick_quest = ^fenwick_started;"),
        "quest: with none named (NULL or \"\") the record's name stands in");
    CHECK(render_titled(EDITOR_SNIPPET_LOC, "Op: by quest stage, then move on", "fenwick_crate", "thistle", text)
              && strstr(text, "[oploc1,fenwick_crate]\nif (%thistle_quest ! ^thistle_started) {\n")
              && strstr(text, "%thistle_quest = ^thistle_complete;\n"),
        "quest: a loc's op moves the quest on, not a variable named for the loc");
    CHECK(render_titled(EDITOR_SNIPPET_ANY, "New quest variable (.varp)", "fenwick", "thistle", text)
              && strstr(text, "\n[thistle_quest]\nscope=perm\n")
              && strstr(text, "quests/quest_thistle/configs/quest_thistle.varp"),
        "quest: the variable snippet declares the quest's variable, in the quest's folder");
    CHECK(render_titled(EDITOR_SNIPPET_ANY, "Quest stage list (.constant)", "fenwick", "thistle", text)
              && strstr(text, "\n^thistle_not_started = 0\n^thistle_started = 1\n^thistle_complete = 2\n"),
        "quest: the stage list is the quest's");
}

/** The quest's variable has a name of its own once it has an id: the
 *  snippets and a new quest's files write that name, and it is read off the
 *  quest's .varp file. */
static void
test_quest_variable(void)
{
    static char text[EDITOR_SNIPPET_MAX];
    char name[64];
    char small[8];

    (void)Editor_SnippetRender(EDITOR_SNIPPET_NPC, 3, "fenwick", "thistle", "varp9517_thistle_quest", text, sizeof(text));
    CHECK(strstr(text, "[opnpc1,fenwick]\nif (%varp9517_thistle_quest = ^thistle_not_started) {\n")
              && strstr(text, "} else if (%varp9517_thistle_quest = ^thistle_started) {\n")
              && !strstr(text, "%thistle_quest") && !strstr(text, "{v}"),
        "variable: a Talk-to by stage reads the variable under its own name, the stages under the quest's");
    (void)Editor_SnippetRender(EDITOR_SNIPPET_NPC, 3, "fenwick", "thistle", "", text, sizeof(text));
    CHECK(strstr(text, "if (%thistle_quest = ^thistle_not_started) {\n") != NULL,
        "variable: with none known it is <quest>_quest");
    (void)Editor_SnippetRender(EDITOR_SNIPPET_NPC, 3, "fenwick", NULL, NULL, text, sizeof(text));
    CHECK(strstr(text, "if (%fenwick_quest = ^fenwick_not_started) {\n") != NULL,
        "variable: with no quest either it is <record>_quest, as before");

    (void)Editor_QuestFileText("thistle", "varp9517_thistle_quest", EDITOR_SNIPPET_FILE_VARP, text, sizeof(text));
    CHECK(strstr(text, "\n[varp9517_thistle_quest]\nscope=perm\n") && !strstr(text, "\n[thistle_quest]"),
        "variable: a new quest's variable is declared under the name it was allocated");
    CHECK(Editor_QuestVarName(text, strlen(text), name, sizeof(name)) && strcmp(name, "varp9517_thistle_quest") == 0,
        "variable: ...and read back off that file, past its comment lines (%s)", name);
    (void)Editor_QuestFileText("thistle", "varp9517_thistle_quest", EDITOR_SNIPPET_FILE_SCRIPT, text, sizeof(text));
    CHECK(strstr(text, "\n[debugproc,thistle]\n%varp9517_thistle_quest = ^thistle_not_started;\n")
              && strstr(text, "\n//if (%varp9517_thistle_quest = ^thistle_not_started) {\n") && !strstr(text, "%thistle_quest"),
        "variable: the new quest's script reads it under that name, the cheats keep the quest's");
    (void)Editor_QuestFileText("thistle", NULL, EDITOR_SNIPPET_FILE_VARP, text, sizeof(text));
    CHECK(Editor_QuestVarName(text, strlen(text), name, sizeof(name)) && strcmp(name, "thistle_quest") == 0,
        "variable: an unnumbered one is read back too");

    CHECK(Editor_QuestVarName("[a]\r\ntype=int\r\n", 15, name, sizeof(name)) && strcmp(name, "a") == 0,
        "variable: CRLF text");
    CHECK(Editor_QuestVarName("// only\n\n[second]\n[third]\n", 26, name, sizeof(name)) && strcmp(name, "second") == 0,
        "variable: the first block, not a later one");
    CHECK(!Editor_QuestVarName("", 0, name, sizeof(name)) && name[0] == '\0'
              && !Editor_QuestVarName("// a comment\nscope=perm\n", 24, name, sizeof(name))
              && !Editor_QuestVarName("[]\n", 3, name, sizeof(name)) && !Editor_QuestVarName("[open\n", 6, name, sizeof(name)),
        "variable: no block, an empty header and an unclosed one have none");
    CHECK(!Editor_QuestVarName("[a_name_too_long]\n", 18, small, sizeof(small)) && small[0] == '\0',
        "variable: a name that does not fit is not cut");
}

/** The five snippets a quest with a fight, a private arena and a scene
 *  needs, spot-checked against the files their idioms come from. */
static void
test_quest_kit(void)
{
    static char text[EDITOR_SNIPPET_MAX];

    CHECK(render_titled(EDITOR_SNIPPET_ANY, "Boss: swing, telegraphed special, death", "thorn_beast", "thistle", text),
        "kit: the boss fight");
    CHECK(strstr(text, "\n[ai_opplayer2,thorn_beast]\n") && strstr(text, "npc_anim(npc_param(attack_anim), 0);\n~npc_meleeattack;\n"),
        "kit: the boss restates its ordinary swing (naming ai_opplayer2 opts out of the default)");
    CHECK(strstr(text, "npc_bind(3);\n") && strstr(text, "npc_queue(6, 0, 3);\n") && strstr(text, "\n[ai_queue6,thorn_beast]\n")
              && strstr(text, "spotanim_map(") && strstr(text, "huntall(npc_coord, 2, 0);\n")
              && strstr(text, "damage(uid, hitsplat_damage, ~random_range(8, 14));\n"),
        "kit: the special is bound, marked, queued, and lands on whoever still stands there");
    CHECK(strstr(text, "\n[ai_queue3,thorn_beast]\nif (npc_findhero = true) {\n")
              && strstr(text, "%thistle_quest = ^thistle_complete;\n") && strstr(text, "~npc_default_death;\n"),
        "kit: the death moves the quest on for the hero, then the default death");
    CHECK(render_titled(EDITOR_SNIPPET_ANY, "Instance: enter a private copy, leave", "thistle", "thistle", text)
              && strstr(text, "~map_instance_from_square_block(0_50_50_0_0, 1, 1);\n")
              && strstr(text, "if ($handle = ^map_instance_none) {\n")
              && strstr(text, "p_telejump(map_instance_coord($handle, 24, 18, 0));\n") && strstr(text, "npc_add(map_instance_coord(")
              && strstr(text, "map_instance_free($handle);\n") && strstr(text, "\n[debugproc,thistle_enter]\n")
              && strstr(text, "\n[debugproc,thistle_leave]\n"),
        "kit: the instance is built, entered, given an npc, and freed on the way out");
    CHECK(render_titled(EDITOR_SNIPPET_ANY, "Cutscene frame: fade, lock, camera, unlock", "thistle", "thistle", text)
              && strstr(text, "~fade_screen(0, 255, 0, 0);\n") && strstr(text, "~cutscene_lock(3, 2);\n~cutscene_hud_lock;\n")
              && strstr(text, "cam_moveto(") && strstr(text, "cam_lookat(")
              && strstr(text, "~cutscene_unlock;\n~cutscene_hud_unlock;\n") && strstr(text, "~fade_screen(0, 0, 0, 255);\n"),
        "kit: the cutscene fades, locks, holds the camera, and puts everything back");
    CHECK(render_titled(EDITOR_SNIPPET_ANY, "Npc: patrol route (.npc)", "fenwick", "thistle", text)
              && strncmp(text, "// Goes in a .npc ", 18) == 0 && strstr(text, "\n[fenwick]\nmaxrange=30\ndefaultmode=patrol\npatrol1=")
              && strstr(text, "quests/quest_thistle/configs/quest_thistle.npc"),
        "kit: the patrol route is an .npc overlay for the npc, in the quest's folder");
    CHECK(render_titled(EDITOR_SNIPPET_ANY, "Quest journal proc", "thistle", "thistle", text)
              && strstr(text, "\n[proc,thistle_journal]\n") && strstr(text, "if (%thistle_quest = ^thistle_not_started) {\n")
              && strstr(text, "~quest_journal(\"Thistle\", $text);\n") && strstr(text, "^journal_todo"),
        "kit: the journal page has a paragraph a stage and is shown by ~quest_journal");
}

/** A new quest's files: the name's rule, the paths, and text that agrees
 *  with what the snippets write for that quest. */
static void
test_new_quest(void)
{
    static char text[EDITOR_SNIPPET_MAX];
    static char snippet[EDITOR_SNIPPET_MAX];
    /* 31 bytes: the longest a quest's name is. */
    static const char k_long[] = "a_quest_name_as_long_as_it_gets";
    char name[EDITOR_QUEST_NAME_MAX];
    char path[160];
    char why[96];
    size_t length;
    int line = 1;

    CHECK(sizeof(k_long) == EDITOR_QUEST_NAME_MAX, "new quest: the long name is the longest (%u)", (unsigned)sizeof(k_long));
    CHECK(Editor_QuestNameFrom("thistle", name, sizeof(name)) && strcmp(name, "thistle") == 0, "new quest: a plain name");
    CHECK(Editor_QuestNameFrom("quest_thistle", name, sizeof(name)) && strcmp(name, "thistle") == 0,
        "new quest: the folder's own prefix is dropped, not doubled");
    CHECK(Editor_QuestNameFrom("the_3rd_age", name, sizeof(name)) && Editor_QuestNameFrom(k_long, name, sizeof(name)),
        "new quest: digits, underscores and the longest name are taken");
    CHECK(!Editor_QuestNameFrom("", name, sizeof(name)) && name[0] == '\0' && !Editor_QuestNameFrom("quest_", name, sizeof(name))
              && !Editor_QuestNameFrom("Thistle", name, sizeof(name)) && !Editor_QuestNameFrom("my quest", name, sizeof(name))
              && !Editor_QuestNameFrom("3rd", name, sizeof(name)) && !Editor_QuestNameFrom("a/b", name, sizeof(name))
              && !Editor_QuestNameFrom("a_quest_name_as_long_as_it_gets_x", name, sizeof(name)),
        "new quest: empty, capitals, a space, a digit first, a slash and a name too long are refused");

    Editor_QuestFolder("thistle", path, sizeof(path));
    CHECK(strcmp(path, "quests/quest_thistle") == 0, "new quest: the folder (%s)", path);
    Editor_QuestFilePath("thistle", EDITOR_SNIPPET_FILE_VARP, path, sizeof(path));
    CHECK(strcmp(path, "quests/quest_thistle/configs/quest_thistle.varp") == 0, "new quest: the variable's file (%s)", path);
    Editor_QuestFilePath("thistle", EDITOR_SNIPPET_FILE_CONSTANT, path, sizeof(path));
    CHECK(strcmp(path, "quests/quest_thistle/configs/quest_thistle.constant") == 0, "new quest: the stages' file (%s)", path);
    Editor_QuestFilePath("thistle", EDITOR_SNIPPET_FILE_SCRIPT, path, sizeof(path));
    CHECK(strcmp(path, "quests/quest_thistle/scripts/quest_thistle.rs2") == 0, "new quest: the script (%s)", path);
    /* "rs2:" + the path is a TEXT document key: 128 bytes with its terminator. */
    Editor_QuestFilePath(k_long, EDITOR_SNIPPET_FILE_CONSTANT, path, sizeof(path));
    CHECK(strlen(path) + 4 < 128, "new quest: the longest path is a document key (%u bytes)", (unsigned)strlen(path));

    length = Editor_QuestFileText("thistle", NULL, EDITOR_SNIPPET_FILE_VARP, text, sizeof(text));
    CHECK(length == strlen(text) && strstr(text, "\n[thistle_quest]\nscope=perm\nclientcode=default\ntransmit=no\nprotect=no\n"
                                                 "wholewrite=deny\nwholeread=deny\n"),
        "new quest: the variable is the block the lane's build takes, with no id");
    (void)render_titled(EDITOR_SNIPPET_ANY, "New quest variable (.varp)", "thistle", "thistle", snippet);
    CHECK(strstr(snippet, strstr(text, "\n[thistle_quest]\n")) != NULL, "new quest: ...the very block the variable snippet writes");
    length = Editor_QuestFileText("thistle", NULL, EDITOR_SNIPPET_FILE_CONSTANT, text, sizeof(text));
    CHECK(length == strlen(text) && strstr(text, "\n^thistle_not_started = 0\n^thistle_started = 1\n^thistle_complete = 2\n"),
        "new quest: the three stages, named as the snippets name them");
    length = Editor_QuestFileText("thistle", NULL, EDITOR_SNIPPET_FILE_SCRIPT, text, sizeof(text));
    CHECK(length == strlen(text) && strstr(text, "\n[debugproc,thistle]\n%thistle_quest = ^thistle_not_started;\n")
              && strstr(text, "\n[debugproc,thistle_start]\n%thistle_quest = ^thistle_started;\n")
              && strstr(text, "\n[debugproc,thistle_done]\n%thistle_quest = ^thistle_complete;\n"),
        "new quest: the script sets each stage with a cheat");
    CHECK(strstr(text, "\n//[opnpc1,your_npc]\n//if (%thistle_quest = ^thistle_not_started) {\n")
              && !strstr(text, "\n[opnpc1,"),
        "new quest: the Talk-to by stage is there as a comment (it needs an npc's name to build)");
    for( const char* p = text; p < strstr(text, "\n[debugproc,thistle]\n") + 1; p++ )
        line += *p == '\n';
    CHECK(line == Editor_QuestScriptLine(), "new quest: the editor opens at the first cheat (line %d, said %d)", line,
        Editor_QuestScriptLine());
    for( int f = EDITOR_SNIPPET_FILE_SCRIPT; f <= EDITOR_SNIPPET_FILE_CONSTANT; f++ )
    {
        memset(text, 'x', sizeof(text));
        length = Editor_QuestFileText(k_long, NULL, (enum Editor_SnippetFile)f, text, sizeof(text));
        CHECK(length > 0 && length < sizeof(text) && strlen(text) == length && text[length - 1] == '\n',
            "new quest: file %d renders for the longest name and ends with a line break (%u)", f, (unsigned)length);
        CHECK(!strstr(text, "{s}") && !strstr(text, "{t}") && !strstr(text, "{T}") && !strstr(text, "{q}"),
            "new quest: file %d leaves no mark behind", f);
        CHECK(!strchr(text, '\r') && !strchr(text, '\t') && balanced(text, why, sizeof(why)),
            "new quest: file %d is LF, spaces, and balanced (%s)", f, why);
        CHECK(longest_line(text) < LINE_MAX_BYTES, "new quest: file %d keeps its lines short (%u)", f,
            (unsigned)longest_line(text));
    }
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
            (void)Editor_SnippetRender((enum Editor_SnippetKind)k, i, "fenwick_crate", NULL, NULL, text, sizeof(text));
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
    test_quest_names();
    test_quest_variable();
    test_quest_kit();
    test_new_quest();
    printf("editor_snippets_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
