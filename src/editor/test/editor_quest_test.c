/*
 * The quest board's reader (editor/editor_quest.h) over a SYNTHETIC
 * repository and content tree written under the directory given -- no
 * OSRS-Content, no quest-helper checkout:
 *
 *   ./editor_quest_test <tmp-dir>
 *
 *   board     every row of design section 11's figure for a hand-written
 *             Cook's Assistant: the dbrow (from a stand-in catalog), the var
 *             and its constants (from the .constant file, by the inventory's
 *             prefix), the start (the label that sets the first stage,
 *             walked up to its opnpc1), the npc builder's node count, each
 *             stage's trigger with its file and line ("no test row" for a
 *             stage the test file never names), the complete proc, the
 *             journal (a file only the catalog names), the test's row and
 *             shot counts
 *   verdict   the ledger's rows and SUMMARY; gate.py's word from the board's
 *             gate file; "stale" once the ledger is newer; "red", "not
 *             gated", "not run" and "missing" in turn
 *   words     every state's word
 *   shots     a shot's PNG path, a long name cut and hashed as gate.py cuts it
 *   refusals  Scaffold and Run gate refused with the reason where a
 *             prerequisite is missing, allowed where none is
 *   no index  without a catalog the dbrow is "not indexed" and the triggers
 *             are read from the quest's scripts/ directory alone
 */

#include "editor/editor_quest.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

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

static char g_repo[1024];
static char g_content[1024];

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

static void
remove_at(
    const char* root,
    const char* relative)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    unlink(path);
}

static void
set_mtime(
    const char* root,
    const char* relative,
    time_t when)
{
    char path[2048];
    struct utimbuf times;
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    times.actime = when;
    times.modtime = when;
    utime(path, &times);
}

static const char k_queue[] = "quest_dir\ttest_id\thelper_dir\thelper_file\ttier\tstatus\towner\tlast_failure\tclaimed_at\tclaim_prev\n"
                              "quest_cook\tcooks_assistant\tcooksassistant\t\t1\tgreen\tsomeone\t\t\t\n"
                              "quest_new\tnew_quest\tnewquest\t\t1\ttodo\t\t\t\t\n";

static const char k_inventory[] = "dir\thuman_name\tvarp\tconst_not_started\tconst_complete\thas_run_selftest\n"
                                  "quest_cook\tCook's Assistant\t%varp29_cookquest\t^cook_not_started\t^cook_complete\tyes\n";

static const char k_constants[] = "^cook_not_started = 0\n"
                                  "^cook_started = 1\n"
                                  "^cook_gathering = 2\n"
                                  "^cook_complete = 3\n"
                                  "^cook_questpoints = 1\n"
                                  "^cook_coord = 0_50_50_6_14\n";

static const char k_script[] = "// The cook.\n"
                               "[opnpc1,cook]\n"
                               "if (%varp29_cookquest = ^cook_not_started) {\n"
                               "    @cook_start;\n"
                               "}\n"
                               "if (%varp29_cookquest = ^cook_started) {\n"
                               "    @cook_talk;\n"
                               "}\n"
                               "~chatnpc(^chat_happy, \"Hello.\");\n"
                               "\n"
                               "[label,cook_start]\n"
                               "~chatnpc(^chat_sad, \"What am I to do?\");\n"
                               "def_int $option = ~p_choice2(\"What's wrong?\", 1, \"No.\", 2);\n"
                               "~chatplayer(^chat_neutral, \"What's wrong?\");\n"
                               "%varp29_cookquest = ^cook_started;\n"
                               "\n"
                               "[label,cook_talk]\n"
                               "switch_int (%varp29_cookquest) {\n"
                               "    case ^cook_gathering : ~chatnpc(^chat_neutral, \"Got them yet?\");\n"
                               "}\n"
                               "%varp29_cookquest = ^cook_gathering;\n"
                               "\n"
                               "[proc,cook_complete]\n"
                               "%varp29_cookquest = ^cook_complete;\n"
                               "~send_quest_complete(questlist:cook);\n";

static const char k_journal[] = "[questjournal,quest_cook]\n"
                                "~send_quest_journal(\"<str>I spoke to the cook.\");\n"
                                "~send_quest_journal(\"He needs milk, an egg and flour.\");\n"
                                "~send_quest_journal(\"<col=000080>QUEST COMPLETE!\");\n";

static const char k_lua[] = "return {\n"
                            "    id = \"cooks_assistant\",\n"
                            "    run = function(t)\n"
                            "        t.expect(\"cook.reset\", t.quest.expect_stage(\"not_started\"))\n"
                            "        t.exec(\"cook.greet\", t.player.talk_to, \"cook\")\n"
                            "        -- t.exec(\"cook.old\", t.player.talk_to, \"cook\")\n"
                            "        t.exec(\"cook.accept\", t.chat.play, {})\n"
                            "        t.expect(\"quest.stage.gathering\", t.quest.stage())\n"
                            "        t.check(\"cook.inv\", t.inv.count, \"egg\")\n"
                            "        t.quest.expect_complete()\n"
                            "        t.finish(0)\n"
                            "    end,\n"
                            "}\n";

static const char k_ledger[] = "quest-ledger-v1\n"
                               "index\tstep\tverdict\tticks\tshots\tdetail\n"
                               "1\tcook.reset\tPASS\t3\t\tstage not_started (0)\n"
                               "2\tcook.greet\tPASS\t10\t01-cook-greet\tok talked to Cook\n"
                               "3\tcook.accept\tPASS\t20\t02-cook-accept,03-cook-accept-b\tok 6 page(s): What am I\tto do\n"
                               "SUMMARY\t4\tPASS\t33\texit=0\tpass=3 fail=0\n";

static const char k_gate_green[] = "# Written by make maped-quest-gate (src/maped/live-editor.mk): tools/quest_gate/gate.py's output.\n"
                                   "cooks_assistant          green\n"
                                   "\n"
                                   "gate: all green\n";

static const char k_gate_red[] = "# Written by make maped-quest-gate (src/maped/live-editor.mk): tools/quest_gate/gate.py's output.\n"
                                 "cooks_assistant          RED\n"
                                 "    - shot 03-cook-accept-b not on disk\n";

static int
fake_record(
    void* user,
    uint32_t kind,
    const char* name,
    char* out_where,
    size_t where_size,
    char* out_summary,
    size_t summary_size)
{
    (void)user;
    if( kind != TORIRSMAPED_CATALOG_DBROW || strcmp(name, "quest_cook") != 0 )
        return 0;
    snprintf(out_where, where_size, "%s", "configs/all.dbrow:12");
    snprintf(out_summary, summary_size, "%s", "points 1");
    return 1;
}

static int
fake_triggers(
    void* user,
    const char* dir,
    struct Editor_QuestTrigger* out,
    int max)
{
    static const char* const k_rows[][2] = {
        { "opnpc1,cook", "server/scripts/quests/quest_cook/scripts/quest_cook.rs2" },
        { "label,cook_start", "server/scripts/quests/quest_cook/scripts/quest_cook.rs2" },
        { "questjournal,quest_cook", "server/scripts/quests/quest_cook/journal/quest_cook_journal.rs2" },
    };
    int count = 0;
    (void)user;
    if( strcmp(dir, "server/scripts/quests/quest_cook/") != 0 )
        return 0;
    for( size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]) && count < max; i++ )
    {
        snprintf(out[count].header, sizeof(out[count].header), "%s", k_rows[i][0]);
        snprintf(out[count].file, sizeof(out[count].file), "%s", k_rows[i][1]);
        out[count].line = 1;
        count++;
    }
    return count;
}

static const struct Editor_QuestRow*
row_of(
    const struct Editor_QuestBoard* board,
    enum Editor_QuestPart part,
    int nth)
{
    for( int i = 0; i < board->row_count; i++ )
    {
        if( board->rows[i].part == part && nth-- == 0 )
            return &board->rows[i];
    }
    return NULL;
}

static void
write_tree(void)
{
    write_at(g_repo, "test/quests/QUEUE.tsv", k_queue);
    write_at(g_repo, "tools/quest_gate/quest_inventory.tsv", k_inventory);
    write_at(g_repo, "tools/quest_gate/new_quest.py", "# stand-in\n");
    write_at(g_repo, "tools/quest_gate/run.py", "# stand-in\n");
    write_at(g_repo, "tools/quest_gate/gate.py", "# stand-in\n");
    write_at(g_repo, "test/quests/cooks_assistant.lua", k_lua);
    write_at(g_repo, "build/quest_gate/cooks_assistant/ledger.tsv", k_ledger);
    write_at(g_repo, "build/maped_quest_board/cooks_assistant.gate", k_gate_green);
    write_at(g_repo, "quest-helper/cooksassistant/CooksAssistant.java", "// stand-in\n");
    write_at(g_content, "server/scripts/quests/quest_cook/configs/quest_cook.constant", k_constants);
    write_at(g_content, "server/scripts/quests/quest_cook/scripts/quest_cook.rs2", k_script);
    write_at(g_content, "server/scripts/quests/quest_cook/journal/quest_cook_journal.rs2", k_journal);
    {
        char model[4096] = "[interaction]\nversion=1\nsubject=npc\nname=cook\n\n[op1]\ndialogue=1\n\n[dialogue]\nstart=n0\n";
        for( int i = 0; i < 14; i++ )
        {
            size_t at = strlen(model);
            snprintf(model + at, sizeof(model) - at, "\n[node,n%d]\nkind=npc\ntext=line %d\n", i, i);
        }
        write_at(g_content, "server/scripts/edited/cook/cook.interaction", model);
    }
    /* The gate's verdict is newer than the ledger it graded. */
    set_mtime(g_repo, "build/quest_gate/cooks_assistant/ledger.tsv", 1700000000);
    set_mtime(g_repo, "build/maped_quest_board/cooks_assistant.gate", 1700000100);
}

static void
test_board(void)
{
    static struct Editor_QuestBoard board;
    struct Editor_QuestCatalog catalog = { NULL, fake_record, fake_triggers };
    const struct Editor_QuestRow* row;
    int rows;

    Editor_QuestBoardInit(&board);
    rows = Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", &catalog);
    CHECK(rows == 8, "board: dbrow, var, start, two stages, complete, journal, test (got %d rows)", rows);
    CHECK(board.queue_found && strcmp(board.quest_dir, "quest_cook") == 0, "board: QUEUE.tsv names the quest's dir");
    CHECK(strcmp(board.display, "Cook's Assistant") == 0 && strcmp(board.var, "varp29_cookquest") == 0
              && strcmp(board.prefix, "cook_") == 0,
        "board: the inventory names the quest, its var and the constants' prefix");
    CHECK(board.points == 1, "board: ^cook_questpoints is the points (got %d)", board.points);
    CHECK(board.constant_count == 4 && strcmp(board.constants[0].name, "not_started") == 0
              && strcmp(board.constants[3].name, "complete") == 0 && board.constants[3].value == 3,
        "board: four stage constants by value, the coord skipped (got %d)", board.constant_count);

    row = row_of(&board, EDITOR_QUEST_PART_DBROW, 0);
    CHECK(row && row->state == EDITOR_QUEST_OK && strcmp(row->what, "dbrow quest_cook (points 1)") == 0
              && strcmp(row->where, "configs/all.dbrow:12") == 0 && row->open == EDITOR_QUEST_OPEN_RECORD
              && row->open_kind == TORIRSMAPED_CATALOG_DBROW,
        "board: dbrow row (%s | %s)", row ? row->what : "-", row ? row->where : "-");
    row = row_of(&board, EDITOR_QUEST_PART_VAR, 0);
    CHECK(row && row->state == EDITOR_QUEST_OK
              && strcmp(row->what, "var varp29_cookquest \xc2\xb7 not_started 0 \xc2\xb7 started 1 \xc2\xb7 gathering 2 "
                                   "\xc2\xb7 complete 3")
                     == 0
              && strcmp(row->where, "server/scripts/quests/quest_cook/configs/quest_cook.constant") == 0
              && row->open == EDITOR_QUEST_OPEN_VARS,
        "board: var row (%s | %s)", row ? row->what : "-", row ? row->where : "-");
    row = row_of(&board, EDITOR_QUEST_PART_START, 0);
    CHECK(row && row->state == EDITOR_QUEST_OK && strcmp(row->what, "start: opnpc1 cook (npc builder, 14 nodes)") == 0
              && strcmp(row->where, "server/scripts/quests/quest_cook/scripts/quest_cook.rs2:2") == 0
              && row->open == EDITOR_QUEST_OPEN_NPC_BUILDER && strcmp(row->open_name, "cook") == 0,
        "board: the start is the opnpc1 that jumps to the label setting ^cook_started (%s | %s)",
        row ? row->what : "-", row ? row->where : "-");
    row = row_of(&board, EDITOR_QUEST_PART_STAGE, 0);
    CHECK(row && row->state == EDITOR_QUEST_NO_TEST_ROW && strcmp(row->what, "stage started: opnpc1 cook") == 0
              && strcmp(row->where, "server/scripts/quests/quest_cook/scripts/quest_cook.rs2:6") == 0
              && row->open == EDITOR_QUEST_OPEN_TRIGGERS
              && strcmp(row->open_file, "quests/quest_cook/scripts/quest_cook.rs2") == 0,
        "board: stage started is tested by opnpc1 cook and no test row names it (%s | %s | %s)",
        row ? row->what : "-", row ? row->where : "-", row ? Editor_QuestStateWord(row->state) : "-");
    row = row_of(&board, EDITOR_QUEST_PART_STAGE, 1);
    CHECK(row && row->state == EDITOR_QUEST_OK && strcmp(row->what, "stage gathering: label cook_talk") == 0
              && strcmp(row->where, "server/scripts/quests/quest_cook/scripts/quest_cook.rs2:19") == 0,
        "board: stage gathering is a switch_int case and quest.stage.gathering names it (%s | %s | %s)",
        row ? row->what : "-", row ? row->where : "-", row ? Editor_QuestStateWord(row->state) : "-");
    row = row_of(&board, EDITOR_QUEST_PART_COMPLETE, 0);
    CHECK(row && row->state == EDITOR_QUEST_OK && strcmp(row->what, "complete: ~cook_complete") == 0
              && strcmp(row->where, "server/scripts/quests/quest_cook/scripts/quest_cook.rs2:24") == 0,
        "board: complete row (%s | %s)", row ? row->what : "-", row ? row->where : "-");
    row = row_of(&board, EDITOR_QUEST_PART_JOURNAL, 0);
    CHECK(row && row->state == EDITOR_QUEST_OK && strcmp(row->what, "journal lines (3)") == 0
              && strcmp(row->where, "server/scripts/quests/quest_cook/journal/quest_cook_journal.rs2:1") == 0,
        "board: the journal is read from the file the catalog names (%s | %s)", row ? row->what : "-",
        row ? row->where : "-");

    CHECK(board.test_present && board.test_rows == 9 && board.test_shots == 4,
        "board: 5 row calls plus expect_complete's 4, 3 shooting plus its scroll (got %d rows, %d shots)",
        board.test_rows, board.test_shots);
    CHECK(board.ledger.present && board.ledger.count == 3 && board.ledger.summary_present
              && strcmp(board.ledger.summary_verdict, "PASS") == 0 && board.ledger.pass == 3 && board.ledger.fail == 0
              && board.ledger.shots == 3,
        "board: the ledger's three rows, three shots and its SUMMARY");
    CHECK(board.ledger.count == 3 && strcmp(board.ledger.rows[2].detail, "ok 6 page(s): What am I\tto do") == 0
              && strcmp(board.ledger.rows[2].shots, "02-cook-accept,03-cook-accept-b") == 0
              && board.ledger.rows[2].ticks == 20,
        "board: a detail keeps its tabs");
    CHECK(board.verdict == EDITOR_QUEST_GREEN && strcmp(board.gate_word, "green") == 0,
        "board: gate.py said green after the ledger (%s)", Editor_QuestStateWord(board.verdict));
    row = row_of(&board, EDITOR_QUEST_PART_TEST, 0);
    CHECK(row && row->state == EDITOR_QUEST_GREEN
              && strcmp(row->what, "test/quests/cooks_assistant.lua \xc2\xb7 9 rows \xc2\xb7 4 shots \xc2\xb7 ledger PASS 3 "
                                   "pass 0 fail \xc2\xb7 gate: green")
                     == 0
              && row->open == EDITOR_QUEST_OPEN_CODE && strcmp(row->open_file, "test/quests/cooks_assistant.lua") == 0,
        "board: the test row (%s)", row ? row->what : "-");

    /* The verdict goes stale when the ledger is newer than the gate's. */
    set_mtime(g_repo, "build/quest_gate/cooks_assistant/ledger.tsv", 1700000200);
    Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", &catalog);
    CHECK(board.verdict == EDITOR_QUEST_STALE, "verdict: a ledger newer than the gate's word is stale (%s)",
        Editor_QuestStateWord(board.verdict));
    write_at(g_repo, "build/maped_quest_board/cooks_assistant.gate", k_gate_red);
    set_mtime(g_repo, "build/maped_quest_board/cooks_assistant.gate", 1700000300);
    Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", &catalog);
    CHECK(board.verdict == EDITOR_QUEST_RED && strcmp(board.gate_word, "RED") == 0, "verdict: gate.py's RED (%s)",
        Editor_QuestStateWord(board.verdict));
    remove_at(g_repo, "build/maped_quest_board/cooks_assistant.gate");
    Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", &catalog);
    CHECK(board.verdict == EDITOR_QUEST_NOT_GATED, "verdict: a ledger with no gate run is not gated (%s)",
        Editor_QuestStateWord(board.verdict));
    remove_at(g_repo, "build/quest_gate/cooks_assistant/ledger.tsv");
    Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", &catalog);
    CHECK(board.verdict == EDITOR_QUEST_NOT_RUN && !board.ledger.present, "verdict: no ledger is not run (%s)",
        Editor_QuestStateWord(board.verdict));

    /* Without the npc builder's model the dialogue is counted from the
     * scripts: opnpc1's ~chatnpc and the labels it jumps to. */
    remove_at(g_content, "server/scripts/edited/cook/cook.interaction");
    Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", &catalog);
    row = row_of(&board, EDITOR_QUEST_PART_START, 0);
    CHECK(row && strcmp(row->what, "start: opnpc1 cook (dialogue, 5 calls)") == 0
              && row->open == EDITOR_QUEST_OPEN_TRIGGERS,
        "board: without a model the dialogue calls are counted (%s)", row ? row->what : "-");

    /* No index: the dbrow cannot be asked and the journal file, outside
     * scripts/, is not found. */
    Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", NULL);
    row = row_of(&board, EDITOR_QUEST_PART_DBROW, 0);
    CHECK(row && row->state == EDITOR_QUEST_UNKNOWN, "no index: the dbrow is not indexed");
    row = row_of(&board, EDITOR_QUEST_PART_JOURNAL, 0);
    CHECK(row && row->state == EDITOR_QUEST_MISSING, "no index: the journal outside scripts/ is not found");
    row = row_of(&board, EDITOR_QUEST_PART_START, 0);
    CHECK(row && row->state == EDITOR_QUEST_OK, "no index: the scripts/ directory still gives the start");

    /* A quest with a QUEUE row and nothing else. */
    Editor_QuestRead(&board, g_repo, g_content, "new_quest", &catalog);
    CHECK(board.queue_found && board.verdict == EDITOR_QUEST_MISSING, "board: no test file is missing");
    row = row_of(&board, EDITOR_QUEST_PART_DBROW, 0);
    CHECK(row && row->state == EDITOR_QUEST_MISSING && strcmp(row->what, "dbrow quest_new") == 0,
        "board: a dbrow the catalog lacks is missing (%s)", row ? row->what : "-");
    Editor_QuestRead(&board, g_repo, g_content, "nobody", &catalog);
    row = row_of(&board, EDITOR_QUEST_PART_DBROW, 0);
    CHECK(!board.queue_found && row && strcmp(row->what, "dbrow: nobody has no QUEUE.tsv row") == 0,
        "board: an id QUEUE.tsv lacks says so (%s)", row ? row->what : "-");
    Editor_QuestBoardFree(&board);

    /* Put the ledger and the gate back for the refusals. */
    write_at(g_repo, "build/quest_gate/cooks_assistant/ledger.tsv", k_ledger);
}

static void
test_words(void)
{
    static const char* const k_words[] = { "ok", "missing", "not indexed", "no test row", "not run", "not gated",
        "stale", "green", "red", "blocked" };
    for( int s = EDITOR_QUEST_OK; s <= EDITOR_QUEST_BLOCKED; s++ )
        CHECK(strcmp(Editor_QuestStateWord((enum Editor_QuestState)s), k_words[s]) == 0, "words: state %d is '%s' (%s)",
            s, k_words[s], Editor_QuestStateWord((enum Editor_QuestState)s));
    {
        char word[16];
        CHECK(Editor_QuestGateParse(k_gate_green, sizeof(k_gate_green) - 1, "cooks_assistant", word, sizeof(word))
                  && strcmp(word, "green") == 0,
            "words: gate.py's padded line reads");
        CHECK(!Editor_QuestGateParse(k_gate_green, sizeof(k_gate_green) - 1, "cooks", word, sizeof(word)),
            "words: a prefix of the id is not the quest");
    }
    {
        struct Editor_QuestLedger ledger;
        memset(&ledger, 0, sizeof(ledger));
        CHECK(!Editor_QuestLedgerParse(&ledger, "index\tstep\n", 11), "words: a ledger needs its quest-ledger-v1 header");
        static const char k_blocked[] =
            "quest-ledger-v1\n1\tx\tBLOCKED\t0\t\tseam\nSUMMARY\t2\tFAIL\t0\texit=3\tpass=0 fail=0 blocked=1\n";
        CHECK(Editor_QuestLedgerParse(&ledger, k_blocked, sizeof(k_blocked) - 1)
                  && ledger.blocked == 1 && ledger.exit_code == 3 && strcmp(ledger.summary_verdict, "FAIL") == 0,
            "words: the SUMMARY's blocked count and exit code");
        ToriMem_Free(ToriMem_Editor(), ledger.rows);
    }
}

static void
test_shots(void)
{
    char path[512];
    Editor_QuestShotPath("cooks_assistant", "01-cook-greet", path, sizeof(path));
    CHECK(strcmp(path, "build/quest_gate/cooks_assistant/shots/01-cook-greet.png") == 0, "shots: %s", path);
    /* gate.py: shot_file_stem("cook.accept.a-very-long-...-limit"). */
    Editor_QuestShotPath("cooks_assistant",
        "cook.accept.a-very-long-row-name-that-goes-on-and-on-past-the-sixty-seven-byte-limit", path, sizeof(path));
    CHECK(strcmp(path, "build/quest_gate/cooks_assistant/shots/"
                       "cook.accept.a-very-long-row-name-that-goes-on-and~9231f666~te-limit.png")
              == 0,
        "shots: a long name is cut and hashed as gate.py cuts it (%s)", path);
}

static void
test_refusals(void)
{
    static struct Editor_QuestBoard board;
    struct Editor_QuestCatalog catalog = { NULL, fake_record, fake_triggers };
    char helper[1100];
    char why[512];

    snprintf(helper, sizeof(helper), "%s/quest-helper", g_repo);
    Editor_QuestBoardInit(&board);
    Editor_QuestRead(&board, g_repo, g_content, "cooks_assistant", &catalog);
    CHECK(!Editor_QuestCanScaffold(&board, g_repo, helper, why, sizeof(why)) && strstr(why, "refuses to clobber"),
        "refusals: Scaffold refuses an existing test file (%s)", why);
    CHECK(!Editor_QuestCanGate(&board, g_repo, g_content, why, sizeof(why)) && strstr(why, "src/torirs_questtest"),
        "refusals: Run gate wants the quest client (%s)", why);
    write_at(g_repo, "src/torirs_questtest", "stand-in\n");
    CHECK(Editor_QuestCanGate(&board, g_repo, g_content, why, sizeof(why)), "refusals: then Run gate may run (%s)", why);
    CHECK(!Editor_QuestCanGate(&board, g_repo, g_repo, why, sizeof(why)) && strstr(why, "OSRS-Content"),
        "refusals: a content root without scripts is refused (%s)", why);
    remove_at(g_repo, "tools/quest_gate/gate.py");
    CHECK(!Editor_QuestCanGate(&board, g_repo, g_content, why, sizeof(why)) && strstr(why, "gate.py"),
        "refusals: no gate.py is refused (%s)", why);

    Editor_QuestRead(&board, g_repo, g_content, "new_quest", &catalog);
    CHECK(Editor_QuestCanScaffold(&board, g_repo, helper, why, sizeof(why)), "refusals: new_quest may be scaffolded (%s)",
        why);
    CHECK(!Editor_QuestCanScaffold(&board, g_repo, "/nowhere/quest-helper", why, sizeof(why))
              && strstr(why, "Quest Helper"),
        "refusals: without the guides Scaffold is refused (%s)", why);
    CHECK(!Editor_QuestCanGate(&board, g_repo, g_content, why, sizeof(why)), "refusals: no test file, no gate");
    Editor_QuestRead(&board, g_repo, g_content, "nobody", &catalog);
    CHECK(!Editor_QuestCanScaffold(&board, g_repo, helper, why, sizeof(why)) && strstr(why, "QUEUE.tsv"),
        "refusals: no QUEUE row, no scaffold (%s)", why);
    CHECK(Editor_QuestIdValid("cooks_assistant") && !Editor_QuestIdValid("Cooks") && !Editor_QuestIdValid("a b")
              && !Editor_QuestIdValid(""),
        "refusals: quest ids are [a-z0-9_]");
    Editor_QuestBoardFree(&board);
}

int
main(
    int argc,
    char** argv)
{
    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <tmp-dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    snprintf(g_repo, sizeof(g_repo), "%s/repo", argv[1]);
    snprintf(g_content, sizeof(g_content), "%s/content", argv[1]);
    make_dirs(g_repo);
    make_dirs(g_content);
    write_tree();
    test_board();
    test_words();
    test_shots();
    test_refusals();
    printf("editor_quest_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
