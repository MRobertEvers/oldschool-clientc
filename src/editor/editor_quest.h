#ifndef SRC_EDITOR_EDITOR_QUEST_H
#define SRC_EDITOR_EDITOR_QUEST_H

/**
 * The QUEST BOARD's reader (workbench 6.7, design section 11 "Quest board"):
 * one quest's parts, where each lives and whether it exists. The board
 * writes nothing; it is the map of what the builders and the quest gate
 * wrote. Every part comes from a file the tree already keeps:
 *
 *   QUEUE.tsv           test/quests/QUEUE.tsv: test_id -> quest_dir (the
 *                       quest's directory under server/scripts/quests/ and
 *                       its dbrow's name)
 *   inventory           tools/quest_gate/quest_inventory.tsv: the display
 *                       name, the progress var (`%varp29_cookquest`) and the
 *                       not_started / complete constants
 *   dbrow               the catalog's DBROW record named quest_dir
 *   var and constants   server/scripts/quests/<dir>/configs/<dir>.constant
 *                       (`^cook_started = 1`): the stage constants share the
 *                       not_started constant's prefix
 *   triggers            the catalog's TRIGGER rows under the quest's
 *                       directory (or, with no catalog, its scripts/ read
 *                       from disk), each file read to find which block SETS
 *                       the var to a stage (`%var = ^stage`) and which TESTS it
 *   start               the block that sets the first stage after
 *                       not_started, walked up its `@label` / `~proc` callers
 *                       to the op trigger that runs it ("opnpc1 cook"); its
 *                       dialogue size from the npc builder's model when
 *                       edited/<npc>/<npc>.interaction exists, else the
 *                       dialogue calls it and its callees make
 *   stages              each constant between not_started and complete: the
 *                       block that tests it, and whether the test file names
 *                       the stage (`expect_stage("<s>")`, `quest.stage.<s>`)
 *   complete            the block that sets the complete constant
 *   journal             a `[questjournal,...]` block, or a proc named
 *                       `*_journal`: its string lines
 *   test                test/quests/<id>.lua: its row and shot call sites
 *   ledger              build/quest_gate/<id>/ledger.tsv (quest-ledger-v1:
 *                       index, step, verdict, ticks, shots, detail; SUMMARY)
 *   gate                build/maped_quest_board/<id>.gate: gate.py's output,
 *                       written by `make maped-quest-gate` (the board's Run
 *                       gate BUILD job, src/maped/live-editor.mk); a verdict
 *                       older than the ledger is STALE
 *
 * The reader opens files under the two roots and nothing else; it runs on
 * the panel's Refresh, never per frame. NULL arguments assert; a missing
 * file is a state ("missing", "not run"), never an error.
 */

#include <stddef.h>
#include <stdint.h>

#define EDITOR_QUEST_ID_MAX 64
#define EDITOR_QUEST_NAME_MAX 128
#define EDITOR_QUEST_TEXT_MAX 192
#define EDITOR_QUEST_PATH_MAX 512
#define EDITOR_QUEST_ROWS_MAX 48
#define EDITOR_QUEST_CONSTANTS_MAX 48
#define EDITOR_QUEST_TRIGGERS_MAX 256
#define EDITOR_QUEST_FILES_MAX 32

/** Where the board's Run gate job leaves gate.py's verdict, relative to the
 *  repository root (a directory the make target owns). */
#define EDITOR_QUEST_GATE_DIR "build/maped_quest_board"
/** The quest driver's artefacts, relative to the repository root. */
#define EDITOR_QUEST_LEDGER_DIR "build/quest_gate"

enum Editor_QuestPart
{
    EDITOR_QUEST_PART_DBROW = 0,
    EDITOR_QUEST_PART_VAR,
    EDITOR_QUEST_PART_START,
    EDITOR_QUEST_PART_STAGE,
    EDITOR_QUEST_PART_COMPLETE,
    EDITOR_QUEST_PART_JOURNAL,
    EDITOR_QUEST_PART_TEST,
};

/** A row's state word (Editor_QuestStateWord). */
enum Editor_QuestState
{
    /** The part exists. */
    EDITOR_QUEST_OK = 0,
    /** The part is not in the tree. */
    EDITOR_QUEST_MISSING,
    /** No catalog to ask (a window without the editor server's index). */
    EDITOR_QUEST_UNKNOWN,
    /** A stage with a trigger but no row of the test names it. */
    EDITOR_QUEST_NO_TEST_ROW,
    /** The test file exists and has no ledger yet. */
    EDITOR_QUEST_NOT_RUN,
    /** A ledger and no gate verdict for it: the ledger's own summary. */
    EDITOR_QUEST_NOT_GATED,
    /** gate.py's verdict is older than the ledger. */
    EDITOR_QUEST_STALE,
    /** gate.py's verdicts. */
    EDITOR_QUEST_GREEN,
    EDITOR_QUEST_RED,
    EDITOR_QUEST_BLOCKED,
};

/** The panel a row's "Open part" opens. */
enum Editor_QuestOpen
{
    EDITOR_QUEST_OPEN_NONE = 0,
    /** The Record panel on (kind, name): the dbrow, the var. */
    EDITOR_QUEST_OPEN_RECORD,
    /** The NPC builder on the start npc. */
    EDITOR_QUEST_OPEN_NPC_BUILDER,
    /** The Triggers panel on a file (`file`, under server/scripts). */
    EDITOR_QUEST_OPEN_TRIGGERS,
    /** The Vars editor on the progress var. */
    EDITOR_QUEST_OPEN_VARS,
    /** A CODE view of a file (`file`, under the repository). */
    EDITOR_QUEST_OPEN_CODE,
};

struct Editor_QuestRow
{
    enum Editor_QuestPart part;
    enum Editor_QuestState state;
    /** "dbrow quest_cook", "stage started: opnpc1 cook". */
    char what[EDITOR_QUEST_TEXT_MAX];
    /** "server/scripts/quests/quest_cook/scripts/quest_cook.rs2:12", or
     *  "" when there is nowhere. */
    char where[EDITOR_QUEST_PATH_MAX];
    enum Editor_QuestOpen open;
    /** OPEN_RECORD: the catalog kind (enum ToriRSMapEdCatalogKind) and name. */
    uint32_t open_kind;
    char open_name[EDITOR_QUEST_NAME_MAX];
    /** OPEN_TRIGGERS: relative to server/scripts; OPEN_CODE: relative to
     *  the repository root. */
    char open_file[EDITOR_QUEST_PATH_MAX];
};

struct Editor_QuestConstant
{
    /** Without the quest's prefix ("started"). */
    char name[64];
    int value;
};

/** One ledger.tsv step row. */
struct Editor_QuestLedgerRow
{
    int index;
    char step[96];
    /** PASS, FAIL, BLOCKED, ... as written. */
    char verdict[16];
    int ticks;
    /** Comma-separated shot names. */
    char shots[256];
    char detail[256];
};

struct Editor_QuestLedger
{
    int present;
    /** Owned, `count` of them. */
    struct Editor_QuestLedgerRow* rows;
    int count;
    int summary_present;
    /** The SUMMARY row: PASS or FAIL, the counts, the driver's exit code. */
    char summary_verdict[16];
    int pass;
    int fail;
    int blocked;
    int exit_code;
    int shots;
    /** Seconds since the epoch; 0 when absent. */
    int64_t mtime;
};

/** One TRIGGER row the catalog answered: the header ("opnpc1,cook") and
 *  where it is. */
struct Editor_QuestTrigger
{
    char header[EDITOR_QUEST_NAME_MAX];
    /** Relative to the content root ("server/scripts/quests/..."). */
    char file[EDITOR_QUEST_PATH_MAX];
    int line;
};

/**
 * What the reader asks of the catalog (edpanel_quest.c binds it to the
 * editor host; a test to stand-ins). NULL members, or a NULL catalog, make
 * those parts UNKNOWN (the record) or read from disk (the triggers).
 */
struct Editor_QuestCatalog
{
    void* user;
    /** Record (kind, name): 1 with its first source "path:line" in
     *  `out_where` (relative to the content root) and a few fields
     *  ("points 1, difficulty novice") in `out_summary`; 0 when absent. */
    int (*record)(
        void* user,
        uint32_t kind,
        const char* name,
        char* out_where,
        size_t where_size,
        char* out_summary,
        size_t summary_size);
    /** Every TRIGGER row whose file is under `dir` (relative to the content
     *  root), at most `max`. Returns the count. */
    int (*triggers)(
        void* user,
        const char* dir,
        struct Editor_QuestTrigger* out,
        int max);
};

struct Editor_QuestBoard
{
    char id[EDITOR_QUEST_ID_MAX];
    /** QUEUE.tsv's quest_dir ("quest_cook"): the directory and the dbrow. */
    char quest_dir[EDITOR_QUEST_NAME_MAX];
    int queue_found;
    /** quest_inventory.tsv's human_name ("Cook's Assistant"). */
    char display[EDITOR_QUEST_NAME_MAX];
    /** Without the '%' ("varp29_cookquest"); "" when unknown. */
    char var[EDITOR_QUEST_NAME_MAX];
    /** The constants' shared prefix ("cook_"). */
    char prefix[64];
    /** `^<prefix>questpoints`, -1 when the file states none. */
    int points;
    struct Editor_QuestConstant constants[EDITOR_QUEST_CONSTANTS_MAX];
    int constant_count;
    /** The .constant file, relative to the content root; "" when absent. */
    char constant_file[EDITOR_QUEST_PATH_MAX];

    struct Editor_QuestRow rows[EDITOR_QUEST_ROWS_MAX];
    int row_count;

    /* the test file */
    int test_present;
    /** Row call sites (t.exec, t.expect, t.check, t.step, t.blocked) and
     *  the ones that shoot (t.exec, t.check). */
    int test_rows;
    int test_shots;
    struct Editor_QuestLedger ledger;
    /** gate.py's word for this quest ("green", "RED", "blocked"), from the
     *  board's gate file; "" when there is none. */
    char gate_word[16];
    int64_t gate_mtime;
    /** The test row's state: the board's verdict. */
    enum Editor_QuestState verdict;
};

/** "ok", "missing", "not indexed", "no test row", "not run", "not gated",
 *  "stale", "green", "red", "blocked". */
const char*
Editor_QuestStateWord(enum Editor_QuestState state);

/** 1..63 bytes of [a-z0-9_]: a QUEUE.tsv test_id. */
int
Editor_QuestIdValid(const char* id);

void
Editor_QuestBoardInit(struct Editor_QuestBoard* board);

/** Frees the ledger rows. NULL-tolerant. */
void
Editor_QuestBoardFree(struct Editor_QuestBoard* board);

/**
 * Read quest `id` (Editor_QuestIdValid) from `repo_root` (test/quests,
 * tools/quest_gate, build/) and `content_root` (server/scripts), asking
 * `catalog` (may be NULL) for the dbrow and the triggers. Replaces what the
 * board held. Returns the row count.
 */
int
Editor_QuestRead(
    struct Editor_QuestBoard* board,
    const char* repo_root,
    const char* content_root,
    const char* id,
    const struct Editor_QuestCatalog* catalog);

/** Parse ledger.tsv text into `ledger` (replacing it). 1 when it is a
 *  quest-ledger-v1 file. */
int
Editor_QuestLedgerParse(
    struct Editor_QuestLedger* ledger,
    const char* text,
    size_t length);

/** The verdict word gate.py printed for `id` in a gate file's text
 *  ("green", "RED", "blocked") into `out`; 0 when the text has none. */
int
Editor_QuestGateParse(
    const char* text,
    size_t length,
    const char* id,
    char* out,
    size_t out_size);

/** Count a quest test file's row call sites and the shooting ones. */
void
Editor_QuestCountTestRows(
    const char* lua,
    size_t length,
    int* out_rows,
    int* out_shots);

/** The PNG a ledger row's shot `name` is on disk as, relative to the
 *  repository root: build/quest_gate/<id>/shots/<stem>.png, the stem cut
 *  as the driver cuts a name over 67 bytes (gate.py shot_file_stem). */
void
Editor_QuestShotPath(
    const char* id,
    const char* name,
    char* out,
    size_t out_size);

/** Whether "Scaffold test" can run here: tools/quest_gate/new_quest.py, a
 *  QUEUE.tsv row, no test file yet (new_quest.py refuses to clobber one) and
 *  the quest-helper guides at `helper_root`. 0 with the reason in `why`. */
int
Editor_QuestCanScaffold(
    const struct Editor_QuestBoard* board,
    const char* repo_root,
    const char* helper_root,
    char* why,
    size_t why_size);

/** Whether "Run gate" can run here: run.py and gate.py, the test file, the
 *  content tree's scripts and the quest client run.py --no-build drives
 *  (src/torirs_questtest). 0 with the reason in `why`. */
int
Editor_QuestCanGate(
    const struct Editor_QuestBoard* board,
    const char* repo_root,
    const char* content_root,
    char* why,
    size_t why_size);

#endif
