/*
 * Workbench 6.6, part 1, headless: the Record panel's typed rows and tabs
 * (editor/panels/edpanel_record.c), the Problems panel
 * (editor/panels/edpanel_problems.c), Check and CS2 compile as the editor
 * server's BUILD jobs (torirsmaped/torirs_maped_build.h), and CATALOG_USES
 * (torirsmaped/torirs_maped_catalog.h), against the real chrome model and an
 * embedded ToriRSMapEd whose catalog indexes a SYNTHETIC content tree the
 * test writes and whose BUILD runner is the test runner (no make, no
 * sscompile, no cs2, no OSRS-Content):
 *
 *   ./editor_records_panels_test <tmp-dir>
 *
 *   record    goblin over a cache block, a generated overlay and an authored
 *             one with a synthetic fields/npc.ini: the six tabs, each layer
 *             tab's rows and badges, the issues ("!": an unknown key, an
 *             unknown param; a param the catalog knows is not), the typed
 *             editor per kind (number with step buttons and a range refusal,
 *             bool, enum, param with its name picker, a name picker over the
 *             catalog), Set as one undo step, the search, "changed only",
 *             "dirty, N fields" and the client-pack chip, undo back to clean
 *   problems  the record's issues as rows, a click back to the field; the
 *             push API (a duplicate is one row, Clear by source); a line
 *             read as a row; Find and "errors only"; the View action and the
 *             bottom strip's group
 *   jobs      Check sent and returning at once (the window never waits), the
 *             unsaved script the server writes for it, its lines as Problems
 *             rows and the Triggers panel's rows, a clean Check clearing
 *             them; a silent failed build leaving its DONE line; CS2 compile
 *             of a script on disk and of the open document, its FAIL line a
 *             row; the refusals (a bad name, a script nobody has)
 *   uses      Where used over the tree, a slice a frame: trigger, script,
 *             spawns per square, overlay, palette for an npc; placements and
 *             a stamp for a loc; a search past one slice's rows
 */

#include "editor/editor.h"
#include "editor/editor_cfg.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_ui.h"
#include "editor/live/live_panel.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "torirsmaped/torirs_maped_live.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static struct Editor g_editor;
static struct RSCache g_profile;
static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_app_stand_in;
static struct ToriRSMapEdBuildMock* g_mock;
static char g_content[1024];
static char g_repo[1024];
static char g_live[1100];
static struct ToriRSMapEd* g_maped;

/** The server's capability bits now (the client's are the HELLO's, which
 *  came before the test bound its runner). */
static uint32_t
maped_caps(void)
{
    return g_maped->capabilities;
}

/* ---- files ---------------------------------------------------------------------- */

static void
make_dirs(const char* path)
{
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for( char* p = buffer + 1; *p; p++ )
    {
        if( *p == '/' )
        {
            *p = '\0';
            mkdir(buffer, 0755);
            *p = '/';
        }
    }
    mkdir(buffer, 0755);
}

static void
write_at(
    const char* root,
    const char* relative,
    const char* text)
{
    char path[2048];
    char dir[2048];
    FILE* f;
    char* slash;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    assert(slash);
    *slash = '\0';
    make_dirs(dir);
    f = fopen(path, "wb");
    if( !f )
    {
        printf("FAIL: cannot write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static char*
read_at(
    const char* root,
    const char* relative)
{
    char path[2048];
    FILE* f;
    long size;
    char* text;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    f = fopen(path, "rb");
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)size + 1);
    assert(text);
    if( fread(text, 1, (size_t)size, f) != (size_t)size )
        size = 0;
    text[size] = '\0';
    fclose(f);
    return text;
}

static void
write_fixture(void)
{
    char rel[256];
    char text[1024];

    make_dirs(g_content);
    write_at(g_content, "configs/all.npc",
        "[goblin]\nname=Goblin\nsize=1\ndesc=An ugly green creature.\nop1=Attack\nmembers=no\n\n"
        "[hans]\nname=Hans\nop1=Talk-to\n");
    write_at(g_content, "configs/all.npc.compack", "3029=goblin\n3105=hans\n");
    write_at(g_content, "configs/all.obj", "[bones]\nname=Bones\ncost=1\n\n[bronze_sword]\nname=Bronze sword\ncost=26\n");
    write_at(g_content, "configs/all.obj.compack", "526=bones\n1277=bronze_sword\n");
    write_at(g_content, "configs/all.param", "[known_param]\ntype=int\n");
    write_at(g_content, "configs/all.param.compack", "2700=known_param\n");
    write_at(g_content, "configs/all.loc", "[fence_gate]\nname=Gate\nop1=Open\n");
    write_at(g_content, "configs/all.loc.compack", "100=fence_gate\n");
    write_at(g_content, "fields/npc.ini",
        "[npc.hitpoints]\nscope = server\nserver = opcode:77:u2\n\n"
        "[npc.huntmode]\nvalues = aggressive:1, cowardly:2\n\n"
        "[npc.death_drop]\nref = obj\ntext = param\n\n"
        "[npc.nomove]\ntype = boolean\n\n"
        "[npc.respawnrate]\ntype = int\n\n"
        "[npc.desc]\nscope = client\nclient = native\n\n"
        "[npc.name]\nscope = client\nclient = native\n");
    write_at(g_content, "server/scripts/_explicit/configs/server_fields.generated.npc",
        "[goblin]\nhitpoints=5\nrespawnrate=25\n");
    write_at(g_content, "server/scripts/areas/lumbridge/configs/lumbridge.npc",
        "[goblin]\nhitpoints=10\nhuntmode=aggressive\ncategory=goblins\nparam=death_drop,bones\nparam=huntrange2,4\n"
        "param=known_param,3\n\n[hans]\nnext=goblin\n");
    write_at(g_content, "server/scripts/areas/lumbridge/scripts/goblin.rs2",
        "[opnpc1,goblin]\n~chatnpc(^chat_neutral, \"Grr.\");\n\n[proc,goblin_helper]\n"
        "npc_add(0_50_50_10_10, goblin, 100);\n");
    write_at(g_content, "server/scripts/areas/lumbridge/lumbridge.spawn",
        "goblin 3222 3218 0\ngoblin 3223 3218 0\nhans 3221 3218 0\ngoblin 3300 3300 0\n");
    write_at(g_content, "maps/m50_50.jl2", "==== LOC ====\n0 1 2: 100 10 0\n0 3 4: 101 10 1\n0 5 5: 100 0 2\n");
    write_at(g_content, "editor/palettes.txt",
        "# ToriRSMapEd catalog palettes (editor/palettes.txt).\npalette Lumbridge | slot 1 | npc:goblin\n"
        "palette Lumbridge | slot 2 | loc:fence_gate | shape 0 rot 1\n");
    write_at(g_content, "editor/stamps/gate.stamp", "# stamp\nloc 0 0 0 100 0 1\nloc 1 0 0 100 0 1\n");
    write_at(g_content, "scripts/bank_sort.cs2", "// 5\n[clientscript,bank_sort]\nreturn;\n");
    /* Enough mentions that one Where used slice cannot list them all. */
    for( int f = 0; f < 40; f++ )
    {
        size_t at = 0;
        for( int l = 0; l < 8; l++ )
            at += (size_t)snprintf(text + at, sizeof(text) - at, "[proc,noise%d_%d]\nnpc_find(goblin);\n", f, l);
        snprintf(rel, sizeof(rel), "server/scripts/noise/scripts/noise%02d.rs2", f);
        write_at(g_content, rel, text);
    }
    make_dirs(g_repo);
}

/* ---- the harness ------------------------------------------------------------------ */

static void
frame(void)
{
    Editor_PumpFacts(&g_editor);
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
frames(int n)
{
    for( int i = 0; i < n; i++ )
        frame();
}

static int
module_panel(const char* name)
{
    struct EditorUiPanel* p = Editor_UiFindPanel(&g_ui, name);
    return p ? p->chrome_panel : -1;
}

static int
widget_in(
    const char* module,
    int kind,
    const char* label)
{
    int const panel = module_panel(module);
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel == panel && w->kind == kind && (strcmp(w->label, label) == 0 || strcmp(w->text, label) == 0) )
            return i;
    }
    return -1;
}

static void
type_into(
    const char* module,
    const char* label,
    const char* text)
{
    int const widget = widget_in(module, TORIRS_CHROME_W_TEXTINPUT, label);
    assert(widget >= 0);
    ToriRSChrome_SetText(&g_chrome, widget, text);
}

static const char*
text_of(
    const char* module,
    const char* label)
{
    int const widget = widget_in(module, TORIRS_CHROME_W_TEXTINPUT, label);
    assert(widget >= 0);
    return ToriRSChrome_Text(&g_chrome, widget);
}

static void
press(
    const char* module,
    const char* caption)
{
    int const widget = widget_in(module, TORIRS_CHROME_W_BUTTON, caption);
    if( widget < 0 )
    {
        printf("FAIL: no %s button '%s'\n", module, caption);
        exit(1);
    }
    g_chrome.activated = widget;
    frame();
}

static int
hidden(
    const char* module,
    int kind,
    const char* label)
{
    int const widget = widget_in(module, kind, label);
    assert(widget >= 0);
    return g_chrome.widgets[widget].hidden;
}

static const char*
vrow(
    int widget,
    int row)
{
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
        if( g_chrome.vrows[i].owner == widget && g_chrome.vrows[i].index == row )
            return g_chrome.vrows[i].text;
    return NULL;
}

static const char*
status(void)
{
    return Editor_UiStatusText(&g_ui);
}

static int
history_applied(void)
{
    return Editor_GetHistory(&g_editor)->applied;
}

static int
row_named(
    const struct Editor_CfgRecord* record,
    const char* key,
    const char* sub)
{
    return Editor_CfgRecordFindRow(record, key, sub, 0);
}

/** Visible rows of the current tab by field name, as "name,name,...". */
static void
visible_names(
    char* out,
    size_t out_size)
{
    int rows[256];
    int const n = Editor_Record_VisibleRows(&g_ui, rows, 256);
    const struct Editor_CfgRecord* record = Editor_Record_Current(&g_ui);
    size_t at = 0;

    out[0] = '\0';
    for( int i = 0; i < n && record; i++ )
    {
        char name[160];
        Editor_CfgRecordRowName(&record->rows[rows[i]], name, sizeof(name));
        at += (size_t)snprintf(out + at, out_size - at, "%s%s", i ? "," : "", name);
    }
}

/* A pushed row's own click (Editor_Problems_ReportOpen). */
static int g_open_calls;
static int g_open_index;

static void
test_problem_open(
    struct EditorUi* ui,
    void* user,
    int index)
{
    (void)ui;
    (*(int*)user)++;
    g_open_index = index;
}

static int
problem_index(
    const char* source,
    const char* needle)
{
    for( int i = 0; i < Editor_Problems_Count(&g_ui); i++ )
    {
        const struct Editor_ProblemRow* row = Editor_Problems_Row(&g_ui, i);
        if( strcmp(row->source, source) == 0 && strstr(row->text, needle) )
            return i;
    }
    return -1;
}

/* ---- the record --------------------------------------------------------------------- */

static void
test_record(void)
{
    const struct Editor_CfgRecord* record;
    int role;
    int list;
    int at;
    int applied;
    char names[2048];

    CHECK(Editor_Record_Open(&g_ui, TORIRSMAPED_CATALOG_NPC, "goblin") == 1, "record: goblin opens");
    frames(2);
    record = Editor_Record_Current(&g_ui);
    CHECK(record && record->layer_count == 3, "record: three layers (%d)", record ? record->layer_count : -1);
    CHECK(Editor_Record_TabCount(&g_ui) == 6, "record: six tabs (%d)", Editor_Record_TabCount(&g_ui));
    CHECK(strcmp(Editor_Record_TabTitle(&g_ui, 0, &role), "Cache block") == 0 && role == EDITOR_RECORD_TAB_CACHE,
        "record: tab 0 is the cache block");
    CHECK(strcmp(Editor_Record_TabTitle(&g_ui, 1, &role), "Server overlay server_fields.generated.npc") == 0
              && role == EDITOR_RECORD_TAB_OVERLAY,
        "record: tab 1 is the generated overlay, named by its file (%s)", Editor_Record_TabTitle(&g_ui, 1, &role));
    CHECK(strcmp(Editor_Record_TabTitle(&g_ui, 2, &role), "Server overlay lumbridge.npc") == 0
              && role == EDITOR_RECORD_TAB_OVERLAY,
        "record: tab 2 is the authored overlay");
    CHECK(strcmp(Editor_Record_TabTitle(&g_ui, 3, &role), "Triggers") == 0 && role == EDITOR_RECORD_TAB_TRIGGERS
              && strcmp(Editor_Record_TabTitle(&g_ui, 4, &role), "Shell") == 0 && role == EDITOR_RECORD_TAB_SHELL
              && strcmp(Editor_Record_TabTitle(&g_ui, 5, &role), "Where used") == 0 && role == EDITOR_RECORD_TAB_USES,
        "record: then Triggers, Shell, Where used");

    /* The cache block's own fields. */
    visible_names(names, sizeof(names));
    CHECK(strcmp(names, "name,size,desc,op1,members") == 0, "record: the cache tab lists its block (%s)", names);
    list = widget_in("record", TORIRS_CHROME_W_VLIST, "-\x1f" "field\x1f" "value\x1f\x1f" "source");
    CHECK(list >= 0 && vrow(list, 0) && strstr(vrow(list, 0), "name\x1fGoblin\x1f\x1f" "cache"),
        "record: a row is field, value and its source badge (%s)", list >= 0 && vrow(list, 0) ? vrow(list, 0) : "-");

    /* The generated overlay: hitpoints is shadowed by the authored one. */
    Editor_Record_ShowTab(&g_ui, 1);
    frame();
    visible_names(names, sizeof(names));
    CHECK(strcmp(names, "hitpoints,respawnrate") == 0, "record: the generated tab (%s)", names);
    CHECK(list >= 0 && vrow(list, 0) && strstr(vrow(list, 0), "shadowed") && strstr(vrow(list, 0), "lumbridge.npc wins: 10"),
        "record: its hitpoints is shadowed by lumbridge.npc's 10 (%s)", list >= 0 && vrow(list, 0) ? vrow(list, 0) : "-");

    /* The authored overlay: its fields, then the register's nobody states. */
    Editor_Record_ShowTab(&g_ui, 2);
    frame();
    visible_names(names, sizeof(names));
    CHECK(strcmp(names, "hitpoints,huntmode,category,param:death_drop,param:huntrange2,param:known_param,nomove") == 0,
        "record: the top tab adds the register's unstated fields (%s)", names);
    record = Editor_Record_Current(&g_ui);
    at = row_named(record, "category", "");
    CHECK(at >= 0 && record->rows[at].issue == EDITOR_CFG_ISSUE_UNKNOWN_KEY, "record: category is an unknown key");
    CHECK(list >= 0 && vrow(list, 2) && strstr(vrow(list, 2), "unknown key") && strstr(vrow(list, 2), "\x1f!"),
        "record: and its row says so, marked (%s)", list >= 0 && vrow(list, 2) ? vrow(list, 2) : "-");
    at = row_named(record, "param", "huntrange2");
    CHECK(at >= 0 && record->rows[at].issue == EDITOR_CFG_ISSUE_UNKNOWN_PARAM, "record: huntrange2 is an unknown param");
    at = row_named(record, "param", "known_param");
    CHECK(at >= 0 && record->rows[at].issue == EDITOR_CFG_ISSUE_NONE,
        "record: a param the catalog knows (configs/all.param) is not unknown");

    /* Problems: the two issues, each with its file and line. */
    at = problem_index("record", "category");
    CHECK(Editor_Problems_CountOf(&g_ui, "record", -1) == 2 && at >= 0, "problems: the record's two issues are rows (%d)",
        Editor_Problems_CountOf(&g_ui, "record", -1));
    if( at >= 0 )
    {
        const struct Editor_ProblemRow* row = Editor_Problems_Row(&g_ui, at);
        CHECK(strcmp(row->file, "areas/lumbridge/configs/lumbridge.npc") == 0 && row->line == 4
                  && row->severity == EDITOR_PROBLEM_WARNING && strcmp(row->record_field, "category") == 0,
            "problems: category is lumbridge.npc line 4, a warning, about goblin's category (%s:%d)", row->file,
            row->line);
    }

    /* The typed editor: a number. */
    CHECK(Editor_Record_SelectField(&g_ui, "hitpoints", "") >= 0, "editor: pick hitpoints");
    frame();
    CHECK(Editor_Record_EditorKind(&g_ui) == EDITOR_CFG_FIELD_NUMBER, "editor: hitpoints is a number");
    CHECK(!hidden("record", TORIRS_CHROME_W_BUTTON, "+1") && !hidden("record", TORIRS_CHROME_W_BUTTON, "-1")
              && hidden("record", TORIRS_CHROME_W_CHECKBOX, "yes") && hidden("record", TORIRS_CHROME_W_TEXTINPUT, "Param"),
        "editor: a number shows its step buttons, not the checkbox or the param column");
    {
        char tip[128];
        ToriRSChrome_TooltipText(&g_chrome, widget_in("record", TORIRS_CHROME_W_BUTTON, "+1"), tip, sizeof(tip));
        CHECK(strstr(tip, "range 0 to 65535") != NULL, "editor: the step's tooltip is the range (%s)", tip);
    }
    applied = history_applied();
    type_into("record", "Value", "70000");
    press("record", "Set field");
    CHECK(history_applied() == applied && strstr(status(), "Set refused: hitpoints = 70000 is out of range"),
        "editor: past the range is refused, and says why (%s)", status());
    type_into("record", "Value", "10");
    press("record", "+1");
    CHECK(strcmp(text_of("record", "Value"), "11") == 0, "editor: +1 steps the value (%s)", text_of("record", "Value"));
    press("record", "Set field");
    frames(2);
    record = Editor_Record_Current(&g_ui);
    at = row_named(record, "hitpoints", "");
    CHECK(history_applied() == applied + 1 && at >= 0 && strcmp(record->rows[at].value, "11") == 0,
        "editor: Set writes 11, one undo step");
    CHECK(Editor_Record_ChangedCount(&g_ui) == 1 && !Editor_Record_NeedsClientPack(&g_ui),
        "editor: one field changed, a server one (%d)", Editor_Record_ChangedCount(&g_ui));

    /* A bool the register lists and nobody states: added to the top layer. */
    CHECK(Editor_Record_SelectField(&g_ui, "nomove", "") >= 0, "editor: pick nomove");
    frame();
    CHECK(Editor_Record_EditorKind(&g_ui) == EDITOR_CFG_FIELD_BOOL && !hidden("record", TORIRS_CHROME_W_CHECKBOX, "yes")
              && hidden("record", TORIRS_CHROME_W_TEXTINPUT, "Value"),
        "editor: a bool is a checkbox");
    ToriRSChrome_SetChecked(&g_chrome, widget_in("record", TORIRS_CHROME_W_CHECKBOX, "yes"), 1);
    press("record", "Set field");
    frames(2);
    record = Editor_Record_Current(&g_ui);
    at = row_named(record, "nomove", "");
    CHECK(at >= 0 && strcmp(record->rows[at].value, "yes") == 0
              && strstr(record->layers[record->rows[at].layer].label, "lumbridge.npc"),
        "editor: the checkbox writes yes into lumbridge.npc");

    /* An enum: a dropdown of its words. */
    CHECK(Editor_Record_SelectField(&g_ui, "huntmode", "") >= 0, "editor: pick huntmode");
    frame();
    CHECK(Editor_Record_EditorKind(&g_ui) == EDITOR_CFG_FIELD_ENUM && !hidden("record", TORIRS_CHROME_W_DROPDOWN, "Value"),
        "editor: an enum is a dropdown");
    {
        int const dropdown = widget_in("record", TORIRS_CHROME_W_DROPDOWN, "Value");
        CHECK(ToriRSChrome_DropdownSelected(&g_chrome, dropdown) == 0, "editor: on aggressive, its first word");
        ToriRSChrome_DropdownSetSelected(&g_chrome, dropdown, 1);
    }
    press("record", "Set field");
    frames(2);
    record = Editor_Record_Current(&g_ui);
    at = row_named(record, "huntmode", "");
    CHECK(at >= 0 && strcmp(record->rows[at].value, "cowardly") == 0, "editor: the dropdown writes cowardly");

    /* A param: two columns, the value a name picked from the catalog. */
    CHECK(Editor_Record_SelectField(&g_ui, "param", "death_drop") >= 0, "editor: pick param:death_drop");
    frame();
    CHECK(Editor_Record_EditorKind(&g_ui) == EDITOR_CFG_FIELD_PARAM && !hidden("record", TORIRS_CHROME_W_TEXTINPUT, "Param")
              && strcmp(text_of("record", "Param"), "death_drop") == 0 && strcmp(text_of("record", "Value"), "bones") == 0,
        "editor: a param row is its name and its value");
    CHECK(Editor_Record_PickCount(&g_ui) >= 1 && strcmp(Editor_Record_PickRow(&g_ui, 0), "bones") == 0,
        "editor: the name picker holds the obj named (%d)", Editor_Record_PickCount(&g_ui));
    type_into("record", "Value", "bronze");
    frame();
    CHECK(Editor_Record_PickCount(&g_ui) == 1 && strcmp(Editor_Record_PickRow(&g_ui, 0), "bronze_sword") == 0,
        "editor: typing filters the catalog's objs (%d)", Editor_Record_PickCount(&g_ui));
    {
        int const pick = widget_in("record", TORIRS_CHROME_W_VLIST, "");
        (void)pick;
    }

    /* The cache half is the client's: the chip. */
    Editor_Record_ShowTab(&g_ui, 0);
    frame();
    CHECK(Editor_Record_SelectField(&g_ui, "name", "") >= 0, "editor: pick name");
    frame();
    type_into("record", "Value", "Goblin chief");
    press("record", "Set field");
    frames(2);
    CHECK(Editor_Record_NeedsClientPack(&g_ui), "editor: a changed client field raises the chip");
    {
        int label = -1;
        for( int i = 0; i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].panel == module_panel("record") && g_chrome.widgets[i].kind == TORIRS_CHROME_W_LABEL
                && strstr(g_chrome.widgets[i].text, "dirty, ") )
                label = i;
        CHECK(label >= 0 && strstr(g_chrome.widgets[label].text, "dirty, 4 fields")
                  && strstr(g_chrome.widgets[label].text, "needs a client pack on RUN"),
            "editor: \"dirty, 4 fields   needs a client pack on RUN\" (%s)", label >= 0 ? g_chrome.widgets[label].text : "-");
    }

    /* Search and "changed only". */
    Editor_Record_ShowTab(&g_ui, 2);
    Editor_Record_SetFind(&g_ui, "hunt", 0);
    frame();
    visible_names(names, sizeof(names));
    CHECK(strcmp(names, "huntmode,param:huntrange2") == 0, "search: hunt (%s)", names);
    Editor_Record_SetFind(&g_ui, "", 1);
    frame();
    visible_names(names, sizeof(names));
    CHECK(strcmp(names, "hitpoints,huntmode,nomove") == 0, "search: changed only on this tab (%s)", names);
    Editor_Record_SetFind(&g_ui, "bool", 1);
    frame();
    visible_names(names, sizeof(names));
    CHECK(strcmp(names, "nomove") == 0, "search: both at once (%s)", names);
    Editor_Record_SetFind(&g_ui, "", 0);

    /* Undo back to clean. */
    for( int i = 0; i < 4; i++ )
        (void)Editor_Undo(&g_editor);
    frames(3);
    CHECK(Editor_Record_ChangedCount(&g_ui) == 0 && !Editor_Record_NeedsClientPack(&g_ui),
        "editor: four undos and the record is clean again (%d)", Editor_Record_ChangedCount(&g_ui));

    /* Click a Problems row: back to the field. */
    Editor_Record_ShowTab(&g_ui, 0);
    frame();
    at = problem_index("record", "huntrange2");
    CHECK(at >= 0 && Editor_Problems_Activate(&g_ui, at) == 1, "problems: the huntrange2 row's click");
    frame();
    CHECK(Editor_Record_EditorKind(&g_ui) == EDITOR_CFG_FIELD_PARAM && strcmp(text_of("record", "Param"), "huntrange2") == 0
              && ToriRSChrome_PanelActiveTab(&g_chrome, module_panel("record")) == 2,
        "problems: opens the record at param:huntrange2 on lumbridge.npc's tab");

    /* The Shell and Triggers tabs. */
    Editor_Record_ShowTab(&g_ui, 4);
    frame();
    {
        int label = -1;
        for( int i = 0; i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].panel == module_panel("record") && g_chrome.widgets[i].kind == TORIRS_CHROME_W_LABEL
                && strstr(g_chrome.widgets[i].text, "is not a shell") )
                label = i;
        CHECK(label >= 0 && !g_chrome.widgets[label].hidden, "shell: goblin is not a shell, and the tab says so");
        CHECK(hidden("record", TORIRS_CHROME_W_BUTTON, "Set field") && !hidden("record", TORIRS_CHROME_W_BUTTON, "Shell editor..."),
            "shell: the field editor is put away, the Shell editor button shown");
    }
    Editor_Record_ShowTab(&g_ui, 3);
    frame();
    {
        int const triggers = widget_in("record", TORIRS_CHROME_W_VLIST, "");
        CHECK(!hidden("record", TORIRS_CHROME_W_BUTTON, "Check"), "triggers: Check is on the Triggers tab");
        (void)triggers;
    }
}

/* ---- Problems --------------------------------------------------------------------- */

static void
test_problems(void)
{
    struct Editor_ProblemRow row;
    int const panel = module_panel("problems");
    int list;
    int before;

    CHECK(panel >= 0, "problems: the panel registers");
    CHECK(Editor_UiGroupOf(&g_ui, "problems") == Editor_UiGroupOf(&g_ui, "console")
              && Editor_UiGroupOf(&g_ui, "problems") >= 0,
        "problems: in the bottom strip's group with Console and History");
    CHECK(Editor_HotkeyForPanel("problems") == EDITOR_HK_VIEW_PROBLEMS
              && Editor_HotkeyInfo(EDITOR_HK_VIEW_PROBLEMS)->defaults[0] == NULL,
        "problems: View > Problems is an action, unbound by default");
    CHECK(Editor_HotkeyRun(&g_ui, EDITOR_HK_VIEW_PROBLEMS) == 1, "problems: the action runs");
    frames(2);
    CHECK(g_chrome.panels[panel].visible, "problems: and shows the panel");

    /* Lines read as rows. */
    CHECK(Editor_ProblemsParseLine("areas/x/scripts/hans.rs2:19: unknown command `mes2`", &row) && row.line == 19
              && strcmp(row.file, "areas/x/scripts/hans.rs2") == 0 && row.severity == EDITOR_PROBLEM_ERROR
              && strcmp(row.text, "unknown command `mes2`") == 0,
        "parse: <file>:<line>: <message>");
    CHECK(Editor_ProblemsParseLine("sscompile: quests/q.rs2:3: warning: unused label", &row)
              && row.severity == EDITOR_PROBLEM_WARNING && strcmp(row.text, "unused label") == 0,
        "parse: sscompile's prefix, and a warning");
    CHECK(Editor_ProblemsParseLine("FAIL bank_sort: unknown proc ~foo", &row)
              && strcmp(row.file, "scripts/bank_sort.cs2") == 0 && strcmp(row.text, "unknown proc ~foo") == 0,
        "parse: cs2's FAIL line");
    CHECK(Editor_ProblemsParseLine("error: no lane bound", &row) && row.file[0] == '\0', "parse: a placeless error");
    CHECK(!Editor_ProblemsParseLine("make: *** [maped-run-build] Error 2", &row)
              && !Editor_ProblemsParseLine("compiled 3184, failed 0", &row)
              && !Editor_ProblemsParseLine("see build log: /a b/c.log:12: x", &row),
        "parse: make's own line, a summary and a spaced path are not rows");

    /* The push API. */
    before = Editor_Problems_Count(&g_ui);
    Editor_Problems_Report(&g_ui, "lint", "quests/q/scripts/q.rs2", 3, EDITOR_PROBLEM_WARNING, "an orphan node");
    Editor_Problems_Report(&g_ui, "lint", "quests/q/scripts/q.rs2", 3, EDITOR_PROBLEM_WARNING, "an orphan node");
    Editor_Problems_Report(&g_ui, "shop", "", 0, EDITOR_PROBLEM_ERROR, "inv lumbridge_general holds 2, the shop 3");
    CHECK(Editor_Problems_Count(&g_ui) == before + 2, "push: a duplicate is one row (%d)", Editor_Problems_Count(&g_ui) - before);
    frames(2);
    list = widget_in("problems", TORIRS_CHROME_W_VLIST, "-\x1fproblem\x1fwhere\x1f\x1fsource");
    CHECK(list >= 0 && vrow(list, 0) && strstr(vrow(list, 0), "\x1f!"), "push: errors list first, marked (%s)",
        list >= 0 && vrow(list, 0) ? vrow(list, 0) : "-");
    type_into("problems", "Find", "orphan");
    frame();
    CHECK(list >= 0 && g_chrome.widgets[list].imax == 1 && vrow(list, 0) && strstr(vrow(list, 0), "q.rs2:3")
              && strstr(vrow(list, 0), "lint"),
        "find: filters as you type (%s)", list >= 0 && vrow(list, 0) ? vrow(list, 0) : "-");
    type_into("problems", "Find", "");
    ToriRSChrome_SetChecked(&g_chrome, widget_in("problems", TORIRS_CHROME_W_CHECKBOX, "errors only"), 1);
    frame();
    CHECK(list >= 0 && g_chrome.widgets[list].imax == Editor_Problems_Count(&g_ui) - 3,
        "errors only: the warnings go (%d of %d)", list >= 0 ? g_chrome.widgets[list].imax : -1, Editor_Problems_Count(&g_ui));
    ToriRSChrome_SetChecked(&g_chrome, widget_in("problems", TORIRS_CHROME_W_CHECKBOX, "errors only"), 0);
    CHECK(Editor_Problems_Activate(&g_ui, problem_index("shop", "holds 2")) == 0 && strstr(status(), "names no file"),
        "click: a row with no file says so (%s)", status());
    CHECK(Editor_Problems_Clear(&g_ui, "lint") == 1 && Editor_Problems_Clear(&g_ui, "shop") == 1
              && Editor_Problems_Count(&g_ui) == before,
        "push: Clear takes back one source's rows");

    /* A row with its reporter's own click (the builders' lint selects the
     * node), under a source longer than a build's name. */
    g_open_calls = 0;
    Editor_Problems_ReportOpen(&g_ui, "npcbuilder:a_rather_long_npc_name_for_a_source", "", 0, EDITOR_PROBLEM_ERROR,
        "node n4: unknown proc: ~nosuch", test_problem_open, &g_open_calls, 7);
    CHECK(Editor_Problems_CountOf(&g_ui, "npcbuilder:a_rather_long_npc_name_for_a_source", -1) == 1,
        "push: a long source keeps its whole name");
    CHECK(Editor_Problems_Activate(&g_ui, problem_index("npcbuilder:a_rather_long_npc_name_for_a_source", "~nosuch"))
                  == 1
              && g_open_calls == 1 && g_open_index == 7,
        "click: a pushed row's own open runs, with its index (%d, %d)", g_open_calls, g_open_index);
    CHECK(Editor_Problems_Clear(&g_ui, "npcbuilder:a_rather_long_npc_name_for_a_source") == 1
              && Editor_Problems_Count(&g_ui) == before,
        "push: and its source's Clear takes it back");
    frame();
}

/* ---- the jobs -------------------------------------------------------------------- */

static int
run_until_idle(struct Editor_BuildJob* job)
{
    for( int i = 0; i < 40; i++ )
    {
        if( Editor_BuildJobPoll(&g_ui, job, NULL, NULL) )
            return 1;
        frame();
    }
    return 0;
}

static void
test_jobs(void)
{
    static const char* const k_check_lines[] = {
        "check: staging /c/server/scripts under /l/check/scripts",
        "check: 1 unsaved script(s) over the stage",
        "areas/lumbridge/scripts/goblin.rs2:2: unknown command `chatnpc2`",
        "quests/q.rs2:3: warning: unused label",
        "check: 1 error(s), 1 warning(s), 0.6 s",
    };
    static const char* const k_clean_lines[] = { "check: staged (copied 0, dropped 0, kept 44)", "check: ok, 0.2 s" };
    static const char* const k_packs_lines[] = { "== packs", "something went sideways" };
    static const char* const k_cs2_lines[] = { "cs2: compiling bank_sort.cs2", "compiled 0, failed 1",
        "FAIL bank_sort: unknown proc ~foo" };
    const struct Editor_LiveModel* live = Editor_LivePanelModel(&g_ui);
    struct Editor_BuildJob job;
    char why[160];
    char* text;
    int commands;
    int problems;

    CHECK(live != NULL, "jobs: the window has a Live model");
    CHECK(maped_caps() & TORIRSMAPED_CAP_BUILD_CHECK, "jobs: the server offers check and cs2 (with its runner)");

    /* An unsaved script: a trigger added and not saved. */
    CHECK(Editor_Triggers_ShowSubject(&g_ui, "npc", "goblin"), "jobs: Triggers on goblin");
    frames(2);
    CHECK(Editor_Triggers_AddTrigger(&g_ui, "opnpc3", "Poke") == 1, "jobs: add [opnpc3,goblin] (%s)", status());
    frames(2);

    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].lines = k_check_lines;
    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].line_count = 5;
    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].status = EDITOR_HOST_BAKE_FAILED;
    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].idle_polls = 3;
    commands = g_mock->command_count;
    problems = Editor_Problems_CountOf(&g_ui, "check", -1);
    CHECK(Editor_Triggers_Check(&g_ui) == 1, "check: sent (%s)", status());
    CHECK(Editor_Triggers_CheckRunning(&g_ui) && Editor_Problems_CountOf(&g_ui, "check", -1) == problems,
        "check: the call returned before any line: the window never waits");
    frame();
    CHECK(Editor_Triggers_CheckRunning(&g_ui), "check: still running a frame later (the slow job)");
    for( int i = 0; i < 30 && Editor_Triggers_CheckRunning(&g_ui); i++ )
        frame();
    CHECK(!Editor_Triggers_CheckRunning(&g_ui), "check: it ends on a later frame");
    CHECK(g_mock->command_count == commands + 1 && strstr(g_mock->commands[commands], " maped-script-check ")
              && strstr(g_mock->commands[commands], "MAPED_CHECK_DIR="),
        "check: the job is make maped-script-check (%s)", g_mock->command_count > commands ? g_mock->commands[commands] : "-");
    text = read_at(g_live, "check/puts/edited/goblin/scripts/goblin.rs2");
    CHECK(text && strstr(text, "[opnpc3,goblin]"), "check: the server wrote the unsaved script for the job to stage");
    free(text);
    CHECK(Editor_Problems_CountOf(&g_ui, "check", EDITOR_PROBLEM_ERROR) == 1
              && Editor_Problems_CountOf(&g_ui, "check", EDITOR_PROBLEM_WARNING) == 1,
        "check: its lines are Problems rows, an error and a warning (%d)", Editor_Problems_CountOf(&g_ui, "check", -1));
    {
        int const at = problem_index("check", "chatnpc2");
        const struct Editor_ProblemRow* row = at >= 0 ? Editor_Problems_Row(&g_ui, at) : NULL;
        CHECK(row && row->line == 2 && strcmp(row->file, "areas/lumbridge/scripts/goblin.rs2") == 0,
            "check: the error's file and line");
    }
    {
        int label = -1;
        for( int i = 0; i < g_chrome.widget_count; i++ )
            if( g_chrome.widgets[i].panel == module_panel("triggers") && g_chrome.widgets[i].kind == TORIRS_CHROME_W_LABEL
                && strncmp(g_chrome.widgets[i].text, "check: ", 7) == 0 )
                label = i;
        CHECK(label >= 0 && strstr(g_chrome.widgets[label].text, "check: 1 error(s), 0.6 s; first areas/lumbridge"),
            "check: the Triggers panel says the count and the first error (%s)",
            label >= 0 ? g_chrome.widgets[label].text : "-");
        CHECK(strstr(status(), "check: 1 error(s)") != NULL, "check: so does the status bar (%s)", status());
    }

    /* A put a failed Check left behind (the tool never got to it) is not
     * staged again: the next Check empties puts/ first. */
    write_at(g_live, "check/puts/areas/old/stale.rs2", "[opnpc1,nobody]\nmes2(\"gone\");\n");

    /* A clean Check clears the kind's rows. */
    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].lines = k_clean_lines;
    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].line_count = 2;
    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].status = EDITOR_HOST_OK;
    g_mock->scripts[TORIRSMAPED_BUILD_CHECK].idle_polls = 0;
    /* Again, from the Record panel's Triggers tab: the Triggers panel's own
     * Check job. */
    CHECK(Editor_Record_Open(&g_ui, TORIRSMAPED_CATALOG_NPC, "goblin") == 1, "check: goblin's record");
    commands = g_mock->command_count;
    Editor_Record_ShowTab(&g_ui, 3);
    press("record", "Check");
    CHECK(g_mock->command_count == commands + 1 && strstr(g_mock->commands[commands], " maped-script-check "),
        "check: the Record panel's Check sends the Triggers panel's job (%s; %s)",
        g_mock->command_count > commands ? g_mock->commands[commands] : "-", status());
    for( int i = 0; i < 30 && Editor_Triggers_CheckRunning(&g_ui); i++ )
        frame();
    CHECK(Editor_Problems_CountOf(&g_ui, "check", -1) == 0, "check: a clean one clears its rows (%d)",
        Editor_Problems_CountOf(&g_ui, "check", -1));
    CHECK(strstr(status(), "check: ok, 0.2 s") != NULL, "check: and says ok (%s)", status());
    text = read_at(g_live, "check/puts/areas/old/stale.rs2");
    CHECK(text == NULL, "check: a put an earlier Check left behind is gone before the next one");
    free(text);
    text = read_at(g_live, "check/puts/edited/goblin/scripts/goblin.rs2");
    CHECK(text && strstr(text, "[opnpc3,goblin]"), "check: ...and the still-unsaved script is put again");
    free(text);

    /* A failed build that said nothing a row reads: its DONE line. */
    g_mock->scripts[TORIRSMAPED_BUILD_PACKS].lines = k_packs_lines;
    g_mock->scripts[TORIRSMAPED_BUILD_PACKS].line_count = 2;
    g_mock->scripts[TORIRSMAPED_BUILD_PACKS].status = EDITOR_HOST_BAKE_FAILED;
    memset(&job, 0, sizeof(job));
    CHECK(Editor_BuildJobSend(&g_ui, &job, TORIRSMAPED_BUILD_PACKS, NULL, why, sizeof(why)), "packs: sent (%s)", why);
    CHECK(run_until_idle(&job) && job.status == EDITOR_HOST_BAKE_FAILED && job.lines == 2,
        "packs: ends failed, its two lines handed over (%u, %d)", job.status, job.lines);
    frame();
    CHECK(Editor_Problems_CountOf(&g_ui, "packs", EDITOR_PROBLEM_ERROR) == 1 && problem_index("packs", "FAILED") >= 0,
        "packs: the DONE line is the row (%d)", Editor_Problems_CountOf(&g_ui, "packs", -1));

    /* CS2: a script on disk, then the open document. */
    g_mock->scripts[TORIRSMAPED_BUILD_CS2].lines = k_cs2_lines;
    g_mock->scripts[TORIRSMAPED_BUILD_CS2].line_count = 3;
    g_mock->scripts[TORIRSMAPED_BUILD_CS2].status = EDITOR_HOST_BAKE_FAILED;
    commands = g_mock->command_count;
    memset(&job, 0, sizeof(job));
    CHECK(Editor_BuildJobSend(&g_ui, &job, TORIRSMAPED_BUILD_CS2, "bank_sort", why, sizeof(why)), "cs2: sent (%s)", why);
    CHECK(run_until_idle(&job) && job.status != EDITOR_HOST_OK, "cs2: ends declined");
    frame();
    CHECK(g_mock->command_count == commands + 1 && strstr(g_mock->commands[commands], " maped-cs2-compile ")
              && strstr(g_mock->commands[commands], "MAPED_CS2_SCRIPT=bank_sort"),
        "cs2: the job is make maped-cs2-compile for the script (%s)",
        g_mock->command_count > commands ? g_mock->commands[commands] : "-");
    CHECK(g_mock->command_count > commands && strstr(g_mock->commands[commands], "MAPED_CS2_SCRIPTS=")
              && strstr(g_mock->commands[commands], "/scripts"),
        "cs2: the job names the content's scripts, for the names the compile resolves a ~call through");
    text = read_at(g_live, "cs2/src/bank_sort.cs2");
    CHECK(text && strcmp(text, "// 5\n[clientscript,bank_sort]\nreturn;\n") == 0, "cs2: the disk's text, written for it");
    free(text);
    {
        int const at = problem_index("cs2 compile", "unknown proc");
        CHECK(at >= 0 && strcmp(Editor_Problems_Row(&g_ui, at)->file, "scripts/bank_sort.cs2") == 0,
            "cs2: its FAIL line is a row on the script");
    }
    CHECK(edp_ensure_doc(&g_ui, EDITOR_DOC_TEXT, "cs2:bank_sort"), "cs2: open the script's document");
    {
        struct Editor_Cmd cmd;
        Editor_CmdInit(&cmd, EDITOR_DOC_TEXT, "cs2:bank_sort", EDITOR_CMD_TEXT_SPLICE);
        cmd.u.text_splice.offset = (uint32_t)strlen("// 5\n[clientscript,bank_sort]\n");
        memcpy(cmd.u.text_splice.text, "return;~foo; return;", 20);
        cmd.u.text_splice.before_len = 7;
        cmd.u.text_splice.after_len = 13;
        CHECK(edp_apply(&g_ui, &cmd, 1, "test splice") == 1, "cs2: an unsaved edit");
    }
    frames(2);
    memset(&job, 0, sizeof(job));
    CHECK(Editor_BuildJobSend(&g_ui, &job, TORIRSMAPED_BUILD_CS2, "bank_sort", why, sizeof(why)), "cs2: again");
    (void)run_until_idle(&job);
    text = read_at(g_live, "cs2/src/bank_sort.cs2");
    CHECK(text && strstr(text, "~foo; return;"), "cs2: the server wrote the document's unsaved text");
    free(text);

    /* Refusals. */
    memset(&job, 0, sizeof(job));
    CHECK(!Editor_BuildJobSend(&g_ui, &job, TORIRSMAPED_BUILD_CS2, "../etc", why, sizeof(why)) && strstr(why, "cannot name"),
        "refuse: a name no make line may carry (%s)", why);
    commands = g_mock->command_count;
    memset(&job, 0, sizeof(job));
    CHECK(Editor_BuildJobSend(&g_ui, &job, TORIRSMAPED_BUILD_CS2, "nosuch", why, sizeof(why)), "refuse: nosuch sent");
    CHECK(run_until_idle(&job) && job.status == EDITOR_HOST_ABSENT && g_mock->command_count == commands,
        "refuse: a script nobody has is ABSENT and runs nothing (%u)", job.status);
    {
        struct ToriRSMapEdBuf payload;
        int const serial = live->build_serial;
        memset(&payload, 0, sizeof(payload));
        ToriRSMapEd_PutU32(&payload, TORIRSMAPED_BUILD_CHECK);
        ToriRSMapEd_PutString(&payload, "extra");
        CHECK(Editor_HostMapEdLiveSend(&g_editor.host, TORIRSMAPED_REQ_BUILD, payload.data + payload.head,
                  (uint32_t)ToriRSMapEd_BufAvailable(&payload)),
            "refuse: a check with an argument sent raw");
        ToriRSMapEd_BufFree(&payload);
        for( int i = 0; i < 20 && live->build_serial == serial; i++ )
            frame();
        CHECK(live->last_build_status == EDITOR_HOST_REFUSED, "refuse: check takes no argument (%u)",
            live->last_build_status);
    }
    CHECK(ToriRSMapEd_BuildArgValid(TORIRSMAPED_BUILD_CS2, "bank_sort") && ToriRSMapEd_BuildArgValid(TORIRSMAPED_BUILD_CS2, "a.b-c")
              && !ToriRSMapEd_BuildArgValid(TORIRSMAPED_BUILD_CS2, "-rf") && !ToriRSMapEd_BuildArgValid(TORIRSMAPED_BUILD_CS2, "a b")
              && !ToriRSMapEd_BuildArgValid(TORIRSMAPED_BUILD_CHECK, "x"),
        "refuse: the argument rule");
    CHECK(strcmp(ToriRSMapEd_BuildWhatName(TORIRSMAPED_BUILD_CHECK), "check") == 0
              && strcmp(ToriRSMapEd_BuildWhatName(TORIRSMAPED_BUILD_CS2), "cs2 compile") == 0,
        "jobs: the names Problems files them under");
}

/* ---- Where used -------------------------------------------------------------------- */

static int
uses_of(
    uint32_t use,
    const char* needle)
{
    int searching;
    int const n = Editor_Record_UsesCount(&g_ui, &searching);
    int found = 0;
    for( int i = 0; i < n; i++ )
    {
        const struct EditorHost_CatalogUse* row = Editor_Record_UsesRow(&g_ui, i);
        if( row->use == use && (!needle || strstr(row->path, needle) || strstr(row->text, needle)) )
            found++;
    }
    return found;
}

static const struct EditorHost_CatalogUse*
use_row(
    uint32_t use,
    const char* needle)
{
    int searching;
    int const n = Editor_Record_UsesCount(&g_ui, &searching);
    for( int i = 0; i < n; i++ )
    {
        const struct EditorHost_CatalogUse* row = Editor_Record_UsesRow(&g_ui, i);
        if( row->use == use && (!needle || strstr(row->path, needle) || strstr(row->text, needle)) )
            return row;
    }
    return NULL;
}

static void
test_uses(void)
{
    struct EditorHost_CatalogUses answer;
    int searching = 1;
    int slices = 0;
    const struct EditorHost_CatalogUse* row;

    CHECK(g_editor.capabilities & TORIRSMAPED_CAP_CATALOG_USES, "uses: the server offers CATALOG_USES");
    CHECK(Editor_Record_Open(&g_ui, TORIRSMAPED_CATALOG_NPC, "goblin") == 1, "uses: goblin");
    Editor_Record_ShowTab(&g_ui, 5);
    frame();
    while( searching && slices < 100 )
    {
        (void)Editor_Record_UsesCount(&g_ui, &searching);
        frame();
        slices++;
    }
    CHECK(!searching && slices >= 2, "uses: the search ran a slice a frame and ended (%d frames)", slices);
    row = use_row(TORIRSMAPED_USE_TRIGGER, "goblin.rs2");
    CHECK(row && row->line == 1 && strstr(row->text, "[opnpc1,goblin]"), "uses: the trigger header");
    row = use_row(TORIRSMAPED_USE_SCRIPT, "goblin.rs2");
    CHECK(row && row->line == 5 && strstr(row->text, "npc_add"), "uses: a script line naming it");
    row = use_row(TORIRSMAPED_USE_SPAWN, "m50_50");
    CHECK(row && row->count == 2 && strstr(row->text, "m50_50: 2 spawns"), "uses: spawns per square (%s)", row ? row->text : "-");
    CHECK(uses_of(TORIRSMAPED_USE_SPAWN, "m51_51") == 1, "uses: the other square's spawn");
    CHECK(uses_of(TORIRSMAPED_USE_CONFIG, "lumbridge.npc") == 1, "uses: hans's overlay field names it, not goblin's own header");
    CHECK(uses_of(TORIRSMAPED_USE_PALETTE, "palettes.txt") == 1, "uses: the palette slot");
    CHECK(uses_of(TORIRSMAPED_USE_SCRIPT, "noise") == 40 * 8 && uses_of(TORIRSMAPED_USE_MORE, NULL) == 0,
        "uses: every noise line, past one answer's rows (%d)", uses_of(TORIRSMAPED_USE_SCRIPT, "noise"));
    {
        int containing = 0;
        for( uint32_t use = TORIRSMAPED_USE_SPAWN; use <= TORIRSMAPED_USE_MORE; use++ )
            containing += uses_of(use, "goblin_helper");
        CHECK(containing == 0 && uses_of(TORIRSMAPED_USE_SPAWN, "hans") == 0,
            "uses: a word that only contains the name (goblin_helper) is not a use (%d)", containing);
    }

    /* The wire's own slices. */
    memset(&answer, 0, sizeof(answer));
    CHECK(Editor_HostMapEdCatalogUses(&g_editor.host, TORIRSMAPED_CATALOG_NPC, "goblin", 0, &answer) == EDITOR_HOST_OK
              && answer.next_cursor > 0 && answer.row_count <= TORIRSMAPED_CATALOG_USES_ROWS + TORIRSMAPED_CATALOG_USES_PER_FILE,
        "wire: the first slice stops at the row cap and says where to go on (%u, %d rows)", answer.next_cursor,
        answer.row_count);
    Editor_HostCatalogUsesFree(&answer);

    /* A loc: its placements and the stamp. */
    CHECK(Editor_Record_Open(&g_ui, TORIRSMAPED_CATALOG_LOC, "fence_gate") == 1, "uses: fence_gate");
    Editor_Record_ShowTab(&g_ui, Editor_Record_TabCount(&g_ui) - 1);
    searching = 1;
    for( slices = 0; searching && slices < 100; slices++ )
    {
        frame();
        (void)Editor_Record_UsesCount(&g_ui, &searching);
    }
    row = use_row(TORIRSMAPED_USE_PLACEMENT, "m50_50");
    CHECK(row && row->count == 2 && row->line == 2, "uses: two placements in m50_50 by id (%s)", row ? row->text : "-");
    row = use_row(TORIRSMAPED_USE_STAMP, "gate.stamp");
    CHECK(row && row->count == 2, "uses: the stamp holds it twice");
    CHECK(uses_of(TORIRSMAPED_USE_PALETTE, NULL) == 1, "uses: its palette slot");
    type_into("record", "Find use", "stamp");
    frame();
    {
        int const list = widget_in("record", TORIRS_CHROME_W_VLIST, "-\x1fuse\x1fwhere\x1f\x1f" "count");
        CHECK(list >= 0 && g_chrome.widgets[list].imax == 1, "uses: the list's search box filters (%d)",
            list >= 0 ? g_chrome.widgets[list].imax : -1);
    }
}

int
main(
    int argc,
    char** argv)
{
    struct EditorHost host;
    struct ToriRSMapEd* maped;
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildRunner runner;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <tmp-dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    snprintf(g_content, sizeof(g_content), "%s/content", argv[1]);
    snprintf(g_repo, sizeof(g_repo), "%s/repo", argv[1]);
    snprintf(g_live, sizeof(g_live), "%s/live", argv[1]);
    if( !RSCache_ProfileByName("osrs241", &g_profile) )
    {
        printf("FAIL: no osrs241 profile\n");
        return 1;
    }
    write_fixture();
    if( !Editor_HostOpenMapEdEmbed(&host, g_content, NULL) )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open %s\n", g_content);
        return 1;
    }
    maped = Editor_HostMapEdEmbedServer(&host);
    assert(maped);
    g_maped = maped;
    ToriRSMapEd_LaneDefaults(&lane, g_repo, g_content, g_live);
    CHECK(ToriRSMapEd_SetLane(maped, &lane), "the lane binds");
    g_mock = calloc(1, sizeof(*g_mock));
    assert(g_mock);
    g_mock->lane = lane;
    ToriRSMapEd_BuildRunnerMock(&runner, g_mock);
    ToriRSMapEd_SetBuildRunner(maped, &runner);

    Editor_OpenHost(&g_editor, &host, g_content, &g_profile);
    CHECK(g_editor.writable, "the tmp tree is writable");

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1530, 1006);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.app_services.user_data = &g_app_stand_in;
    Editor_UiRegisterToolsPanels(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    frame();
    (void)Editor_Triggers_SetPaths(&g_ui, g_content, NULL, NULL);
    CHECK(Editor_UiFindPanel(&g_ui, "record") && Editor_UiFindPanel(&g_ui, "problems"), "the panels register");

    test_record();
    test_problems();
    test_jobs();
    test_uses();

    Editor_UiFree(&g_ui);
    Editor_Close(&g_editor);
    printf("editor_records_panels_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
