/*
 * Workbench 6.6, part 1, content-free: the field register's types and a
 * record's typed rows (editor/editor_cfg.h), and Check's job lines
 * (editor/builder/builder_ssc.h "AS A BUILD JOB").
 *
 *   register   a SYNTHETIC fields/npc.ini: number (a u2 band home: 0..65535),
 *              bool, name (ref = obj), param (text = param), enum (values),
 *              indexed, client-side (scope = client, client = native,
 *              client = param:<p>); the kinds, ranges, words, ref types
 *   record     a synthetic npc over a cache block, a generated overlay and an
 *              authored one: every row's register field, kind, value kind,
 *              client side and issue (unknown key, unknown param, out of
 *              range, not a word, not yes or no; the cache half's own
 *              opcodes are never unknown), typed guesses for keys the
 *              register lacks, a param the caller knows
 *   search     terms over the field name, the value, the layer's file, the
 *              kind; all terms must match
 *   changed    against the record as it was: a value, a layer, a field
 *              added, a field removed
 *   check      a diagnostic and the summaries formatted and read back
 *
 *   ./editor_records_test
 */

#include "editor/builder/builder_ssc.h"
#include "editor/editor_cfg.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
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

static const char k_register[] =
    "; fields/npc.ini (synthetic)\n"
    "[npc.hitpoints]\n"
    "scope = server\n"
    "server = opcode:77:u2\n"
    "\n"
    "[npc.huntmode]\n"
    "values = aggressive:1, cowardly:2\n"
    "\n"
    "[npc.death_drop]\n"
    "server = opcode:151:u4\n"
    "ref = obj\n"
    "text = param\n"
    "\n"
    "[npc.nomove]\n"
    "type = boolean\n"
    "\n"
    "[npc.patrol]\n"
    "text = indexed\n"
    "type = coord,int\n"
    "\n"
    "[npc.desc]\n"
    "scope = client\n"
    "client = native\n"
    "\n"
    "[npc.attackrate]\n"
    "type = int\n"
    "client = param:attackrate\n"
    "\n"
    "[npc.respawnrate]\n"
    "type = int\n"
    "\n"
    "[obj.cost]\n"
    "type = int\n";

static const char k_cache[] = "[goblin]\nname=Goblin\nsize=1\ndesc=An ugly green creature.\nparam=attackrate,4\n"
                              "members=no\n\n";
static const char k_generated[] = "[goblin]\nhitpoints=5\n";
static const char k_authored[] = "[goblin]\nhitpoints=70000\nhuntmode=sleepy\nnomove=maybe\ncategory=goblins\n"
                                 "param=death_drop,bones\nparam=huntrange2,4\npatrol1=3200_3200,5\nrespawnrate=25\n";

static struct Editor_CfgRegisterField g_fields[EDITOR_CFG_REGISTER_MAX];
static int g_field_count;

static const struct Editor_CfgRecordRow*
row_of(
    const struct Editor_CfgRecord* record,
    const char* key,
    const char* sub)
{
    int const at = Editor_CfgRecordFindRow(record, key, sub, 0);
    return at >= 0 ? &record->rows[at] : NULL;
}

static void
make_record(
    struct Editor_CfgRecord* record,
    const char* authored)
{
    Editor_CfgRecordInit(record, "npc", "goblin");
    CHECK(Editor_CfgRecordAddLayer(record, NULL, k_cache, strlen(k_cache)), "the cache layer");
    CHECK(Editor_CfgRecordAddLayer(record, "areas/lumbridge/configs/lumbridge.npc", authored, strlen(authored)),
        "the authored overlay");
    CHECK(Editor_CfgRecordAddLayer(record, "_explicit/configs/server_fields.generated.npc", k_generated,
              strlen(k_generated)),
        "the generated overlay");
    Editor_CfgRecordResolve(record, g_fields, g_field_count);
}

static void
test_register(void)
{
    long long lo;
    long long hi;
    char words[8][32];
    int at;

    g_field_count = Editor_CfgRegisterParse("npc", k_register, strlen(k_register), g_fields, EDITOR_CFG_REGISTER_MAX);
    CHECK(g_field_count == 8, "register: the eight npc sections (obj.cost is another type's): %d", g_field_count);

    at = Editor_CfgRegisterFind(g_fields, g_field_count, "hitpoints");
    CHECK(at >= 0 && strcmp(g_fields[at].server, "opcode:77:u2") == 0, "register: server = is kept verbatim");
    CHECK(at >= 0 && Editor_CfgRegisterValueKind(&g_fields[at]) == EDITOR_CFG_FIELD_NUMBER,
        "register: a u2 band home is a number");
    CHECK(at >= 0 && Editor_CfgRegisterRange(&g_fields[at], &lo, &hi) && lo == 0 && hi == 65535,
        "register: its range is 0..65535");
    CHECK(at >= 0 && !Editor_CfgRegisterClientSide(&g_fields[at]), "register: hitpoints is the server's");

    at = Editor_CfgRegisterFind(g_fields, g_field_count, "huntmode");
    CHECK(at >= 0 && Editor_CfgRegisterValueKind(&g_fields[at]) == EDITOR_CFG_FIELD_ENUM, "register: values = is an enum");
    CHECK(at >= 0 && Editor_CfgRegisterWords(&g_fields[at], words, 8) == 2 && strcmp(words[0], "aggressive") == 0
              && strcmp(words[1], "cowardly") == 0,
        "register: its words, in order, without their numbers");
    CHECK(at >= 0 && !Editor_CfgRegisterRange(&g_fields[at], &lo, &hi), "register: an enum has no range");

    at = Editor_CfgRegisterFind(g_fields, g_field_count, "death_drop");
    CHECK(at >= 0 && g_fields[at].text_param && strcmp(g_fields[at].ref, "obj") == 0, "register: ref = and text = param");
    CHECK(at >= 0 && Editor_CfgRegisterValueKind(&g_fields[at]) == EDITOR_CFG_FIELD_NAME
              && strcmp(Editor_CfgRegisterRefType(&g_fields[at]), "obj") == 0,
        "register: a ref is a name picked from that kind (before its u4 width)");

    at = Editor_CfgRegisterFind(g_fields, g_field_count, "nomove");
    CHECK(at >= 0 && Editor_CfgRegisterValueKind(&g_fields[at]) == EDITOR_CFG_FIELD_BOOL, "register: boolean is a bool");

    at = Editor_CfgRegisterFind(g_fields, g_field_count, "patrol");
    CHECK(at >= 0 && g_fields[at].text_indexed && Editor_CfgRegisterValueKind(&g_fields[at]) == EDITOR_CFG_FIELD_TEXT,
        "register: a tuple type is text");

    at = Editor_CfgRegisterFind(g_fields, g_field_count, "desc");
    CHECK(at >= 0 && Editor_CfgRegisterClientSide(&g_fields[at]), "register: scope = client is client side");
    at = Editor_CfgRegisterFind(g_fields, g_field_count, "attackrate");
    CHECK(at >= 0 && Editor_CfgRegisterClientSide(&g_fields[at]) && strcmp(g_fields[at].client, "param:attackrate") == 0,
        "register: client = param:<p> reaches the client");
    CHECK(at >= 0 && Editor_CfgRegisterRange(&g_fields[at], &lo, &hi) && lo == -2147483648LL && hi == 2147483647LL,
        "register: type = int is the int32 range");
    CHECK(Editor_CfgRegisterFind(g_fields, g_field_count, "cost") < 0, "register: no obj field in the npc register");

    CHECK(strcmp(Editor_CfgFieldKindName(EDITOR_CFG_FIELD_PARAM), "param") == 0
              && strcmp(Editor_CfgFieldKindName(EDITOR_CFG_FIELD_TEXT), "text") == 0,
        "register: the kind names");
    at = Editor_CfgRegisterFind(g_fields, g_field_count, "hitpoints");
    CHECK(Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_NUMBER, "12") == EDITOR_CFG_ISSUE_NONE
              && Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_NUMBER, "1.5") == EDITOR_CFG_ISSUE_NOT_NUMBER
              && Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_NUMBER, "-1") == EDITOR_CFG_ISSUE_OUT_OF_RANGE,
        "register: a number's issues");
    at = Editor_CfgRegisterFind(g_fields, g_field_count, "nomove");
    CHECK(Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_BOOL, "yes") == EDITOR_CFG_ISSUE_NONE
              && Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_BOOL, "on") == EDITOR_CFG_ISSUE_NOT_BOOL,
        "register: a bool's issues");
    at = Editor_CfgRegisterFind(g_fields, g_field_count, "huntmode");
    CHECK(Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_ENUM, "cowardly") == EDITOR_CFG_ISSUE_NONE
              && Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_ENUM, "2") == EDITOR_CFG_ISSUE_NONE
              && Editor_CfgValueIssue(&g_fields[at], EDITOR_CFG_FIELD_ENUM, "sleepy") == EDITOR_CFG_ISSUE_NOT_WORD,
        "register: an enum takes its words or a number");
    CHECK(strcmp(Editor_CfgRowIssueText(EDITOR_CFG_ISSUE_UNKNOWN_KEY), "unknown key") == 0
              && strcmp(Editor_CfgRowIssueText(EDITOR_CFG_ISSUE_UNKNOWN_PARAM), "unknown param") == 0
              && Editor_CfgRowIssueText(EDITOR_CFG_ISSUE_NONE)[0] == '\0',
        "register: the issue texts the Problems rows carry");
}

static void
test_record(void)
{
    struct Editor_CfgRecord record;
    const struct Editor_CfgRecordRow* row;
    const char* known[] = { "huntrange2" };
    int issues;

    make_record(&record, k_authored);

    row = row_of(&record, "hitpoints", "");
    CHECK(row && strcmp(row->value, "70000") == 0 && row->shadowed == 1, "record: authored beats generated");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_NUMBER && row->reg >= 0 && !row->client_side && row->in_overlay,
        "record: hitpoints is a server number row");
    CHECK(row && row->issue == EDITOR_CFG_ISSUE_OUT_OF_RANGE, "record: 70000 is past the u2 range");

    row = row_of(&record, "huntmode", "");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_ENUM && row->issue == EDITOR_CFG_ISSUE_NOT_WORD,
        "record: sleepy is not a huntmode word");
    row = row_of(&record, "nomove", "");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_BOOL && row->issue == EDITOR_CFG_ISSUE_NOT_BOOL,
        "record: maybe is not yes or no");

    row = row_of(&record, "category", "");
    CHECK(row && row->reg < 0 && row->issue == EDITOR_CFG_ISSUE_UNKNOWN_KEY && row->kind == EDITOR_CFG_FIELD_TEXT,
        "record: an overlay key the register lacks is an unknown key");

    row = row_of(&record, "param", "death_drop");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_PARAM && row->value_kind == EDITOR_CFG_FIELD_NAME
              && row->issue == EDITOR_CFG_ISSUE_NONE && strcmp(row->value, "bones") == 0,
        "record: a text = param field is a two-column param row with a name value");
    row = row_of(&record, "param", "huntrange2");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_PARAM && row->issue == EDITOR_CFG_ISSUE_UNKNOWN_PARAM,
        "record: an overlay param the register lacks is an unknown param");
    row = row_of(&record, "param", "attackrate");
    CHECK(row && row->reg >= 0 && row->value_kind == EDITOR_CFG_FIELD_NUMBER && row->client_side
              && row->issue == EDITOR_CFG_ISSUE_NONE && !row->in_overlay,
        "record: a cache param bound by client = param:<p> is that field, client side");

    row = row_of(&record, "patrol1", "");
    CHECK(row && row->reg == Editor_CfgRegisterFind(g_fields, g_field_count, "patrol") && row->issue == 0,
        "record: patrol1 is the indexed patrol field");
    row = row_of(&record, "respawnrate", "");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_NUMBER && row->issue == 0 && !row->client_side,
        "record: an int field in range");

    row = row_of(&record, "name", "");
    CHECK(row && row->reg < 0 && row->kind == EDITOR_CFG_FIELD_TEXT && row->client_side
              && row->issue == EDITOR_CFG_ISSUE_NONE,
        "record: the cache half's own opcodes are never unknown, and are client side");
    row = row_of(&record, "size", "");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_NUMBER && row->issue == 0, "record: an unlisted integer is a number");
    row = row_of(&record, "members", "");
    CHECK(row && row->kind == EDITOR_CFG_FIELD_BOOL, "record: an unlisted yes / no is a bool");
    row = row_of(&record, "desc", "");
    CHECK(row && row->client_side && row->reg >= 0, "record: desc is the register's client field");

    issues = Editor_CfgRecordType(&record, g_fields, g_field_count, known, 1);
    row = row_of(&record, "param", "huntrange2");
    CHECK(row && row->issue == EDITOR_CFG_ISSUE_NONE, "record: a param the catalog knows is not unknown");
    CHECK(issues == 4, "record: four issues are left (hitpoints, huntmode, nomove, category): %d", issues);
    Editor_CfgRecordFree(&record);
}

static int
count_matches(
    const struct Editor_CfgRecord* record,
    const char* query)
{
    int n = 0;
    for( int r = 0; r < record->row_count; r++ )
        n += Editor_CfgRecordRowMatches(record, r, query);
    return n;
}

static void
test_search(void)
{
    struct Editor_CfgRecord record;
    char name[160];

    make_record(&record, k_authored);
    CHECK(count_matches(&record, "") == record.row_count, "search: an empty query is every row");
    CHECK(count_matches(&record, "HUNT") == 2, "search: field names, case-insensitively (huntmode, param:huntrange2): %d",
        count_matches(&record, "HUNT"));
    CHECK(count_matches(&record, "bones") == 1, "search: values");
    CHECK(count_matches(&record, "lumbridge 700") == 1, "search: every term must match (the file and the value)");
    CHECK(count_matches(&record, "generated") == 0, "search: a field the generated layer lost is not its file's");
    CHECK(count_matches(&record, "bool") == 2, "search: the kind (nomove, members): %d", count_matches(&record, "bool"));
    CHECK(count_matches(&record, "param:death") == 1, "search: the param row's name");
    CHECK(count_matches(&record, "zzz") == 0, "search: nothing");
    Editor_CfgRecordRowName(&record.rows[Editor_CfgRecordFindRow(&record, "param", "death_drop", 0)], name, sizeof(name));
    CHECK(strcmp(name, "param:death_drop") == 0, "search: a param row is named param:<p>");
    Editor_CfgRecordFree(&record);
}

static void
test_changed(void)
{
    struct Editor_CfgRecord base;
    struct Editor_CfgRecord now;
    static const char k_edited[] = "[goblin]\nhitpoints=80\nhuntmode=sleepy\nnomove=maybe\n"
                                   "param=death_drop,bones\nparam=huntrange2,4\npatrol1=3200_3200,5\nrespawnrate=25\n"
                                   "huntrange=6\n";
    int changed = 0;

    make_record(&base, k_authored);
    make_record(&now, k_authored);
    for( int r = 0; r < now.row_count; r++ )
        changed += Editor_CfgRecordRowChanged(&now, r, &base);
    CHECK(changed == 0 && Editor_CfgRecordRemovedSince(&now, &base) == 0, "changed: nothing against itself");
    Editor_CfgRecordFree(&now);

    make_record(&now, k_edited);
    changed = 0;
    for( int r = 0; r < now.row_count; r++ )
        changed += Editor_CfgRecordRowChanged(&now, r, &base);
    CHECK(Editor_CfgRecordRowChanged(&now, Editor_CfgRecordFindRow(&now, "hitpoints", "", 0), &base),
        "changed: a new value");
    CHECK(Editor_CfgRecordRowChanged(&now, Editor_CfgRecordFindRow(&now, "huntrange", "", 0), &base),
        "changed: a field added");
    CHECK(!Editor_CfgRecordRowChanged(&now, Editor_CfgRecordFindRow(&now, "nomove", "", 0), &base),
        "changed: an untouched field is not");
    CHECK(changed == 2, "changed: two rows (%d)", changed);
    CHECK(Editor_CfgRecordRemovedSince(&now, &base) == 1, "changed: category went (no register row keeps it)");
    Editor_CfgRecordFree(&now);

    /* The same value moved to another layer is a change. */
    Editor_CfgRecordInit(&now, "npc", "goblin");
    (void)Editor_CfgRecordAddLayer(&now, NULL, k_cache, strlen(k_cache));
    (void)Editor_CfgRecordAddLayer(&now, "areas/lumbridge/configs/lumbridge.npc", "[goblin]\nname=Goblin\n", 21);
    Editor_CfgRecordResolve(&now, g_fields, g_field_count);
    CHECK(Editor_CfgRecordRowChanged(&now, Editor_CfgRecordFindRow(&now, "name", "", 0), &base),
        "changed: a field another layer now wins");
    Editor_CfgRecordFree(&now);
    Editor_CfgRecordFree(&base);
}

static void
test_check_lines(void)
{
    struct Builder_SscResult* made = calloc(1, sizeof(*made));
    struct Builder_SscResult* read = calloc(1, sizeof(*read));
    char line[512];

    if( !made || !read )
        abort();
    snprintf(made->diags[0].file, sizeof(made->diags[0].file), "%s", "areas/lumbridge/scripts/hans.rs2");
    made->diags[0].line = 19;
    snprintf(made->diags[0].message, sizeof(made->diags[0].message), "%s", "unknown command `mes2`");
    snprintf(made->diags[1].file, sizeof(made->diags[1].file), "%s", "quests/x.rs2");
    made->diags[1].line = 3;
    made->diags[1].warning = 1;
    snprintf(made->diags[1].message, sizeof(made->diags[1].message), "%s", "unused label");
    made->diag_count = 2;
    made->status = BUILDER_SSC_FAILED;
    made->seconds = 0.6;

    Builder_SscFormatDiag(&made->diags[0], line, sizeof(line));
    CHECK(strcmp(line, "areas/lumbridge/scripts/hans.rs2:19: unknown command `mes2`") == 0, "check: a diag line (%s)",
        line);
    CHECK(Builder_SscTakeLine(read, line) == 1 && read->diag_count == 1 && read->diags[0].line == 19
              && strcmp(read->diags[0].file, "areas/lumbridge/scripts/hans.rs2") == 0 && !read->diags[0].warning,
        "check: read back as the same diag");
    Builder_SscFormatDiag(&made->diags[1], line, sizeof(line));
    CHECK(Builder_SscTakeLine(read, line) == 1 && read->diags[1].warning && strcmp(read->diags[1].message, "unused label") == 0,
        "check: a warning read back");
    CHECK(Builder_SscTakeLine(read, "check: staging /c/server/scripts under /w/scripts") == 0
              && Builder_SscTakeLine(read, "check: sscompile: 3184 compiled, 0 reused") == 0 && read->diag_count == 2,
        "check: the job's progress lines are nothing");
    Builder_SscFormatSummary(made, line, sizeof(line));
    CHECK(strcmp(line, "check: 1 error(s), 1 warning(s), 0.6 s") == 0, "check: the failed summary (%s)", line);
    CHECK(Builder_SscTakeLine(read, line) == 2 && read->status == BUILDER_SSC_FAILED && read->seconds > 0.55,
        "check: read back as failed");
    made->diag_count = 0;
    made->status = BUILDER_SSC_OK;
    Builder_SscFormatSummary(made, line, sizeof(line));
    CHECK(strcmp(line, "check: ok, 0.6 s") == 0, "check: the ok summary (%s)", line);
    CHECK(Builder_SscTakeLine(read, line) == 2 && read->status == BUILDER_SSC_OK, "check: read back as ok");
    made->status = BUILDER_SSC_NOT_RUN;
    Builder_SscFormatSummary(made, line, sizeof(line));
    CHECK(Builder_SscTakeLine(read, line) == 2 && read->status == BUILDER_SSC_NOT_RUN, "check: could not run (%s)", line);
    free(made);
    free(read);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_register();
    test_record();
    test_search();
    test_changed();
    test_check_lines();
    printf("editor_records_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
