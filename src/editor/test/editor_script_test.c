/*
 * The Script panel's pure half (src/editor/editor_script.c):
 *
 *   ./editor_script_test [<sscompile> <content dir> <work dir>]
 *
 * Header parsing, the trigger kinds of every type and their menu fields, the
 * template for every kind, the "add op + trigger" plan (which field, which
 * file, the appended text, the header's line, each refusal), the "Open in
 * editor" command line, and a fake sscompile output through
 * Builder_SscParseOutput mapped onto lines. With
 * the three arguments it also compiles every template against a stage of
 * the content tree's scripts (written under <work dir>; the tree is only
 * read).
 */

#include "editor/builder/builder_ssc.h"
#include "editor/editor_open_cmd.h"
#include "editor/editor_script.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void
test_parse(void)
{
    static const char k_text[] = "// Hans\n"
                                 "\n"
                                 "[opnpc1,hans]\n"
                                 "~chatnpc(\"Hello.\");\n"
                                 "  [opnpc2,hans]\n"            /* indented: not a header */
                                 "[label,hans_talk]\r\n"        /* CRLF */
                                 "[proc,thing](int $a)(int)\n"  /* a proc header */
                                 "[nocomma]\n"                  /* no comma */
                                 "[,empty_kind]\n"              /* an empty kind still parses */
                                 "[oploc3,]\n"                  /* an empty subject does not */
                                 "[ai_queue5,hans]";            /* no final line break */
    struct Editor_ScriptTrigger t[16];
    int n = Editor_ScriptParseTriggers(k_text, strlen(k_text), t, 16);

    CHECK(n == 5, "parse: five headers (%d)", n);
    if( n != 5 )
        return;
    CHECK(strcmp(t[0].kind, "opnpc1") == 0 && strcmp(t[0].subject, "hans") == 0 && t[0].line == 3,
        "parse: [opnpc1,hans] on line 3 (%s,%s %d)", t[0].kind, t[0].subject, t[0].line);
    CHECK(strcmp(t[1].kind, "label") == 0 && strcmp(t[1].subject, "hans_talk") == 0 && t[1].line == 6,
        "parse: a CRLF line's header keeps no \\r (%s %d)", t[1].subject, t[1].line);
    CHECK(strcmp(t[2].kind, "proc") == 0 && strcmp(t[2].subject, "thing") == 0 && t[2].line == 7,
        "parse: a proc header stops at its ']'");
    CHECK(t[3].kind[0] == '\0' && strcmp(t[3].subject, "empty_kind") == 0 && t[3].line == 9,
        "parse: the catalog's rule, an empty kind kept");
    CHECK(strcmp(t[4].kind, "ai_queue5") == 0 && t[4].line == 11, "parse: the last line without a break");
    CHECK(Editor_ScriptParseTriggers(k_text, strlen(k_text), NULL, 0) == 5, "parse: counting with no array");
    CHECK(Editor_ScriptParseTriggers(k_text, strlen(k_text), t, 2) == 5 && t[1].line == 6,
        "parse: past max still counted, the first two written");
    CHECK(Editor_ScriptParseTriggers("", 0, t, 16) == 0, "parse: empty text");
}

static void
test_kinds(void)
{
    const char* kinds[EDITOR_SCRIPT_KINDS_MAX];
    char field[16];
    static const char* const k_types[] = { "loc", "npc", "obj" };
    static const int k_counts[] = { 12, 12, 18 };

    for( int t = 0; t < 3; t++ )
    {
        int const n = Editor_ScriptTriggerKinds(k_types[t], kinds, EDITOR_SCRIPT_KINDS_MAX);
        int fields = 0;
        CHECK(n == k_counts[t], "kinds: %s has %d (%d)", k_types[t], k_counts[t], n);
        for( int i = 0; i < n; i++ )
        {
            CHECK(Editor_ScriptKindValid(k_types[t], kinds[i]), "kinds: %s valid for %s", kinds[i], k_types[t]);
            fields += Editor_ScriptKindOpField(kinds[i], field, sizeof(field));
        }
        /* Every kind but the use-on ones (one op, one ap; obj also opheldu)
         * answers a menu entry. */
        CHECK(fields == n - (t == 2 ? 3 : 2), "kinds: %s's menu fields (%d)", k_types[t], fields);
    }
    CHECK(Editor_ScriptTriggerKinds("seq", kinds, EDITOR_SCRIPT_KINDS_MAX) == 0, "kinds: no kinds for a seq");
    CHECK(!Editor_ScriptKindValid("loc", "opnpc1") && !Editor_ScriptKindValid("npc", "oploc1")
              && !Editor_ScriptKindValid("loc", "opheld1") && Editor_ScriptKindValid("obj", "opheld5"),
        "kinds: each type's own");

    CHECK(Editor_ScriptKindOpField("oploc3", field, sizeof(field)) && strcmp(field, "op3") == 0, "field: oploc3 op3");
    CHECK(Editor_ScriptKindOpField("aploc2", field, sizeof(field)) && strcmp(field, "op2") == 0,
        "field: aploc2 answers op2 too");
    CHECK(Editor_ScriptKindOpField("opnpc5", field, sizeof(field)) && strcmp(field, "op5") == 0, "field: opnpc5");
    CHECK(Editor_ScriptKindOpField("opobj1", field, sizeof(field)) && strcmp(field, "op1") == 0,
        "field: opobj1 is the ground op1");
    CHECK(Editor_ScriptKindOpField("opheld4", field, sizeof(field)) && strcmp(field, "ifop4") == 0,
        "field: opheld4 is the inventory ifop4");
    CHECK(!Editor_ScriptKindOpField("oplocu", field, sizeof(field)) && field[0] == '\0', "field: use-on has none");

    CHECK(!Editor_ScriptOpStated(NULL) && !Editor_ScriptOpStated("") && !Editor_ScriptOpStated("default")
              && !Editor_ScriptOpStated("null") && Editor_ScriptOpStated("Search")
              && Editor_ScriptOpStated("hidden"),
        "op stated: default / null / empty are no entry; hidden is one");
}

static void
test_paths(void)
{
    char out[EDITOR_SCRIPT_PATH_MAX];

    CHECK(Editor_ScriptEditedPath("crate", out, sizeof(out)) && strcmp(out, "edited/crate/scripts/crate.rs2") == 0,
        "path: the builders' edited script (%s)", out);
    CHECK(Editor_ScriptPathValid("areas/lumbridge/scripts/hans.rs2"), "path: hans.rs2 valid");
    CHECK(!Editor_ScriptPathValid("/abs/x.rs2") && !Editor_ScriptPathValid("../x.rs2")
              && !Editor_ScriptPathValid("a/../x.rs2") && !Editor_ScriptPathValid("x.cs2")
              && !Editor_ScriptPathValid("a b.rs2") && !Editor_ScriptPathValid(".rs2")
              && !Editor_ScriptPathValid("a//b.rs2"),
        "path: absolute, .., not .rs2, a space, empty stem, // refused");
    CHECK(Editor_ScriptRelFromContentPath("server/scripts/areas/x/scripts/y.rs2", out, sizeof(out))
              && strcmp(out, "areas/x/scripts/y.rs2") == 0,
        "path: a catalog source path to the document's");
    CHECK(!Editor_ScriptRelFromContentPath("configs/all.loc", out, sizeof(out)) && out[0] == '\0',
        "path: a config is not a script");
}

static void
test_templates(void)
{
    static const char* const k_types[] = { "loc", "npc", "obj" };
    char text[256];
    size_t length;

    for( int t = 0; t < 3; t++ )
    {
        const char* kinds[EDITOR_SCRIPT_KINDS_MAX];
        int const n = Editor_ScriptTriggerKinds(k_types[t], kinds, EDITOR_SCRIPT_KINDS_MAX);
        for( int i = 0; i < n; i++ )
        {
            struct Editor_ScriptTrigger parsed[2];
            size_t const kl = strlen(kinds[i]);
            int const use = kinds[i][kl - 1] == 'u';
            length = Editor_ScriptTemplate(kinds[i], "my_thing", text, sizeof(text));
            CHECK(length > 0 && length == strlen(text) && text[length - 1] == '\n', "template: %s ends a line",
                kinds[i]);
            CHECK(Editor_ScriptParseTriggers(text, length, parsed, 2) == 1 && strcmp(parsed[0].kind, kinds[i]) == 0
                      && strcmp(parsed[0].subject, "my_thing") == 0 && parsed[0].line == 1,
                "template: %s opens with its own header", kinds[i]);
            CHECK(use ? strstr(text, "~displaymessage(^dm_default);") != NULL
                      : strstr(text, "mes(\"Nothing interesting happens.\");") != NULL,
                "template: %s's body (%s)", kinds[i], use ? "use-on default" : "a message");
        }
    }
    CHECK(strcmp((Editor_ScriptTemplate("oploc3", "crate", text, sizeof(text)), text),
              "[oploc3,crate]\nmes(\"Nothing interesting happens.\");\n")
              == 0,
        "template: oploc3 exactly");
    CHECK(Editor_ScriptTemplate("oploc3", "crate", text, 8) == 0 && text[0] == '\0', "template: too small refused");
}

static void
test_plan(void)
{
    struct Editor_ScriptAddPlan plan;
    enum Editor_ScriptPlanStatus st;
    static const char k_existing[] = "// mine\n[oploc1,crate]\nmes(\"x\");\n";
    static const char k_generated[] = "// Generated by the ToriRSEd loc builder from edited/crate/crate.interaction.\n";
    char after[1024];

    /* A new file, a loc with no op3: the field and the trigger. */
    st = Editor_ScriptPlanAddTrigger("loc", "crate", "oploc3", "default", " Search ", "", 0, &plan);
    CHECK(st == EDITOR_SCRIPT_PLAN_OK, "plan: oploc3 on a new file (%s)", Editor_ScriptPlanStatusText(st));
    CHECK(strcmp(plan.rel_path, "edited/crate/scripts/crate.rs2") == 0
              && strcmp(plan.doc_key, "rs2:edited/crate/scripts/crate.rs2") == 0,
        "plan: the subject's edited script (%s)", plan.doc_key);
    CHECK(strcmp(plan.field, "op3") == 0 && strcmp(plan.field_value, "Search") == 0,
        "plan: op3=Search (trimmed) because the record states none (%s=%s)", plan.field, plan.field_value);
    CHECK(plan.offset == 0
              && strcmp(plan.insert, "// crate: triggers written by hand, started from the ToriRSEd Record panel.\n"
                                     "\n[oploc3,crate]\nmes(\"Nothing interesting happens.\");\n")
                     == 0
              && plan.insert_length == strlen(plan.insert),
        "plan: a header comment, a blank line, the template");
    CHECK(plan.header_line == 3, "plan: the header lands on line 3 (%d)", plan.header_line);

    /* An existing file: appended after a blank line; the record has op2. */
    st = Editor_ScriptPlanAddTrigger(
        "loc", "crate", "oploc2", "Open", "", k_existing, strlen(k_existing), &plan);
    snprintf(after, sizeof(after), "%s%s", k_existing, plan.insert);
    {
        struct Editor_ScriptTrigger t[4];
        int const n = Editor_ScriptParseTriggers(after, strlen(after), t, 4);
        CHECK(st == EDITOR_SCRIPT_PLAN_OK && plan.field[0] == '\0' && plan.offset == strlen(k_existing)
                  && strncmp(plan.insert, "\n[oploc2,crate]\n", 16) == 0,
            "plan: op2 already stated, so only the trigger, after a blank line");
        CHECK(n == 2 && t[1].line == plan.header_line && plan.header_line == 5,
            "plan: the reported line is where the header is (%d)", plan.header_line);
    }

    /* No final line break: the file's last line is ended first. */
    st = Editor_ScriptPlanAddTrigger("loc", "crate", "oplocu", NULL, NULL, "// x", 4, &plan);
    snprintf(after, sizeof(after), "// x%s", plan.insert);
    CHECK(st == EDITOR_SCRIPT_PLAN_OK && strncmp(plan.insert, "\n\n[oplocu,crate]\n~displaymessage(^dm_default);\n", 47) == 0
              && plan.field[0] == '\0' && plan.header_line == 3,
        "plan: use-on needs no menu entry; the line break first (%d)", plan.header_line);

    /* An obj's held op is the inventory field. */
    st = Editor_ScriptPlanAddTrigger("obj", "knife", "opheld2", NULL, "Sharpen", "", 0, &plan);
    CHECK(st == EDITOR_SCRIPT_PLAN_OK && strcmp(plan.field, "ifop2") == 0 && strcmp(plan.field_value, "Sharpen") == 0,
        "plan: opheld2 writes ifop2");
    st = Editor_ScriptPlanAddTrigger("npc", "guard", "apnpc1", "Talk-to", NULL, "", 0, &plan);
    CHECK(st == EDITOR_SCRIPT_PLAN_OK && plan.field[0] == '\0', "plan: apnpc1 on an npc that has op1");

    /* The refusals. */
    CHECK(Editor_ScriptPlanAddTrigger("loc", "crate", "opnpc1", NULL, "x", "", 0, &plan) == EDITOR_SCRIPT_PLAN_BAD_KIND,
        "plan: an npc kind on a loc");
    CHECK(Editor_ScriptPlanAddTrigger("seq", "crate", "oploc1", NULL, "x", "", 0, &plan) == EDITOR_SCRIPT_PLAN_BAD_KIND,
        "plan: a type with no triggers");
    CHECK(Editor_ScriptPlanAddTrigger("loc", "Crate", "oploc1", NULL, "x", "", 0, &plan)
              == EDITOR_SCRIPT_PLAN_BAD_SUBJECT,
        "plan: not a record name");
    CHECK(Editor_ScriptPlanAddTrigger("loc", "crate", "oploc1", NULL, "x", k_existing, strlen(k_existing), &plan)
              == EDITOR_SCRIPT_PLAN_DUPLICATE,
        "plan: the script already has [oploc1,crate]");
    CHECK(Editor_ScriptPlanAddTrigger("loc", "crate", "oploc4", NULL, "x", k_generated, strlen(k_generated), &plan)
              == EDITOR_SCRIPT_PLAN_GENERATED,
        "plan: a builder's output is not hand-edited");
    CHECK(Editor_ScriptPlanAddTrigger("loc", "crate", "oploc4", "default", "  ", "", 0, &plan)
              == EDITOR_SCRIPT_PLAN_NO_LABEL,
        "plan: no op4 and no text for it");
    CHECK(Editor_ScriptPlanAddTrigger("loc", "crate", "oploc4", NULL, "a\nb", "", 0, &plan) == EDITOR_SCRIPT_PLAN_BAD_TEXT,
        "plan: a line break in the menu text");
    for( int s = 0; s <= EDITOR_SCRIPT_PLAN_BAD_TEXT; s++ )
        CHECK(Editor_ScriptPlanStatusText((enum Editor_ScriptPlanStatus)s)[0] != '?', "plan: status %d has words", s);
}

static void
test_diags(void)
{
    static const char k_stage[] = "/tmp/work/scripts";
    static const char k_output[] =
        "sscompile: reading 3184 units\n"
        "/tmp/work/scripts/areas/lumbridge/scripts/hans.rs2:21: unknown command 'mes_typo'\n"
        "/tmp/work/scripts/areas/lumbridge/scripts/hans.rs2:21: expected ';'\n"
        "/tmp/work/scripts/areas/lumbridge/scripts/hans.rs2:9: warning: unused local $x\n"
        "/tmp/work/scripts/areas/lumbridge/scripts/hans.rs2:4: type mismatch\n"
        "/tmp/work/scripts/edited/crate/scripts/crate.rs2:3: unknown loc 'crate'\n"
        "sscompile: 3183 compiled, 0 reused, 2 failed\n";
    struct Builder_SscResult* result = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*result));
    int lines[8];
    int n;
    char text[512];

    assert(result);
    Builder_SscParseOutput(k_stage, k_output, strlen(k_output), result);
    CHECK(result->diag_count == 5 && strcmp(result->summary, "3183 compiled, 0 reused, 2 failed") == 0,
        "diags: five located lines and the summary (%d)", result->diag_count);
    n = Editor_ScriptDiagLines(result, "areas/lumbridge/scripts/hans.rs2", lines, 8);
    CHECK(n == 2 && lines[0] == 3 && lines[1] == 20,
        "diags: hans.rs2's errors on 0-based lines 3 and 20, sorted, once; the warning left out (%d: %d %d)", n,
        n > 0 ? lines[0] : -1, n > 1 ? lines[1] : -1);
    n = Editor_ScriptDiagLines(result, "areas/lumbridge/scripts/hans.rs2", lines, 1);
    CHECK(n == 1 && lines[0] == 3, "diags: max respected (the lowest kept)");
    n = Editor_ScriptDiagLines(result, "edited/crate/scripts/crate.rs2", lines, 8);
    CHECK(n == 1 && lines[0] == 2, "diags: another file's own");
    CHECK(Editor_ScriptDiagLines(result, "areas/x.rs2", lines, 8) == 0, "diags: none for a clean file");
    Editor_ScriptDiagFormat(&result->diags[0], text, sizeof(text));
    CHECK(strcmp(text, "areas/lumbridge/scripts/hans.rs2:21: unknown command 'mes_typo'") == 0,
        "diags: the row text is relative to server/scripts (%s)", text);
    Editor_ScriptDiagFormat(&result->diags[2], text, sizeof(text));
    CHECK(strstr(text, ":9: warning: unused local $x") != NULL, "diags: a warning says so");
    ToriMem_Free(ToriMem_Editor(), result);
}

static void
test_open_command(void)
{
    char out[256];
    size_t n;

    n = Editor_OpenCommandLine("code --goto {path}:{line}", "/a/b/hans.rs2", 17, out, sizeof(out));
    CHECK(n == strlen(out) && strcmp(out, "code --goto '/a/b/hans.rs2':17") == 0, "open: VS Code's form (%s)", out);
    Editor_OpenCommandLine("code --goto {path}:{line}", "/my dir/it's.rs2", 3, out, sizeof(out));
    CHECK(strcmp(out, "code --goto '/my dir/it'\\''s.rs2':3") == 0,
        "open: a space stays inside the quotes, a ' is closed, escaped, reopened (%s)", out);
    Editor_OpenCommandLine("subl {path}:{line} # {line}", "/x.rs2", 0, out, sizeof(out));
    CHECK(strcmp(out, "subl '/x.rs2':1 # 1") == 0, "open: every placeholder, a line below 1 is 1 (%s)", out);
    Editor_OpenCommandLine("open {path}", "/x;rm -rf ~.rs2", 9, out, sizeof(out));
    CHECK(strcmp(out, "open '/x;rm -rf ~.rs2'") == 0, "open: shell syntax in a path is inert inside the quotes");
    Editor_OpenCommandLine("vim +{line} {pathx}", "/x.rs2", 2, out, sizeof(out));
    CHECK(strcmp(out, "vim +2 {pathx}") == 0, "open: only the exact placeholders are replaced");
    CHECK(Editor_OpenCommandLine("code {path}", "/a/very/long/path.rs2", 1, out, 10) == 0 && out[0] == '\0',
        "open: a command that does not fit is refused, not cut");
    CHECK(Editor_OpenCommandLine("", "/x.rs2", 1, out, sizeof(out)) == 0, "open: an empty template is refused");
    CHECK(strcmp(Editor_OpenCommandDefault(1, 1), "code --goto {path}:{line}") == 0
              && strcmp(Editor_OpenCommandDefault(0, 1), "open {path}") == 0
              && strcmp(Editor_OpenCommandDefault(0, 0), "xdg-open {path}") == 0,
        "open: the defaults (code on PATH, else open / xdg-open)");
}

/* Every template compiles: one file per type with every kind, against a
 * stage of the real tree. Subjects chosen with no trigger in the tree (a
 * duplicate is a compile error, and would say so). */
static void
test_compile(
    const char* sscompile,
    const char* content,
    const char* work)
{
    static const char* const k_types[] = { "loc", "npc", "obj" };
    static const char* const k_subjects[] = { "multicannon_stand", "molanisk", "ammo_mould" };
    struct Builder_Ssc ssc;
    struct Builder_SscResult* result;
    char path[256];

    if( access(sscompile, X_OK) != 0 )
    {
        printf("editor_script_test: no sscompile at %s, the compile check is skipped\n", sscompile);
        return;
    }
    CHECK(Builder_SscInit(&ssc, sscompile, content, work), "compile: the runner takes the work dir");
    CHECK(Builder_SscStage(&ssc), "compile: staged");
    for( int t = 0; t < 3; t++ )
    {
        const char* kinds[EDITOR_SCRIPT_KINDS_MAX];
        int const n = Editor_ScriptTriggerKinds(k_types[t], kinds, EDITOR_SCRIPT_KINDS_MAX);
        char text[4096];
        size_t length = 0;
        for( int i = 0; i < n; i++ )
        {
            struct Editor_ScriptAddPlan plan;
            enum Editor_ScriptPlanStatus st =
                Editor_ScriptPlanAddTrigger(k_types[t], k_subjects[t], kinds[i], "Thing", NULL, text, length, &plan);
            CHECK(st == EDITOR_SCRIPT_PLAN_OK, "compile: plan %s", kinds[i]);
            if( st != EDITOR_SCRIPT_PLAN_OK || length + plan.insert_length >= sizeof(text) )
                break;
            memcpy(text + length, plan.insert, plan.insert_length + 1);
            length += plan.insert_length;
            snprintf(path, sizeof(path), "%s", plan.rel_path);
        }
        CHECK(Builder_SscPut(&ssc, path, text, length), "compile: %s written over the stage", path);
    }
    result = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*result));
    assert(result);
    Builder_SscRun(&ssc, BUILDER_SSC_COMPILE, result);
    for( int t = 0; t < 3; t++ )
    {
        snprintf(path, sizeof(path), "edited/%s/scripts/%s.rs2", k_subjects[t], k_subjects[t]);
        CHECK(Builder_SscErrorsFor(result, path) == 0, "compile: every %s template compiles (%d errors in %s)",
            k_types[t], Builder_SscErrorsFor(result, path), path);
    }
    for( int i = 0; i < result->diag_count && i < 6; i++ )
        if( strncmp(result->diags[i].file, "edited/", 7) == 0 )
            printf("  %s:%d: %s\n", result->diags[i].file, result->diags[i].line, result->diags[i].message);
    printf("editor_script_test: compile %s in %.1f s (%s)\n",
        result->status == BUILDER_SSC_OK ? "ok" : result->status == BUILDER_SSC_FAILED ? "failed" : "not run",
        result->seconds, result->summary);
    CHECK(result->status == BUILDER_SSC_OK || result->status == BUILDER_SSC_FAILED, "compile: sscompile ran");
    ToriMem_Free(ToriMem_Editor(), result);
}

int
main(
    int argc,
    char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_parse();
    test_kinds();
    test_paths();
    test_templates();
    test_plan();
    test_diags();
    test_open_command();
    if( argc >= 4 )
        test_compile(argv[1], argv[2], argv[3]);
    printf("editor_script_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
