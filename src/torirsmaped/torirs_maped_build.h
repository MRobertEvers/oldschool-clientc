#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_BUILD_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_BUILD_H

/*
 * BUILD and RUN on the wire, and the BuildRunner seam under them
 * (docs/MAPED_DESIGN.html #save, #run; MAPED_IMPLEMENTATION.md section 3,
 * owner live-editor).
 *
 * The editor server never assembles a tool's command line in C beyond naming
 * a make target and its variables: the makefile is the authority on how a
 * build is done (`make -C src torirsserver-packs`, `make -C src
 * maped-client-cache`, src/maped/live-editor.mk). A BuildRunner is the seam
 * between "build this" and a process that does it: the native binding spawns
 * make and streams what it prints; the test binding records the command it
 * would have run and answers from a script, so no test in a checkout whose
 * content tree is read-only ever writes a pack.
 *
 * Nothing here blocks. A runner is started, then polled once per server
 * service pass (ToriRSMapEd_Service, torirs_maped_live.h) until it is done;
 * every line it printed in between goes out as FACT_BUILD_LINE.
 *
 * Frames (requests 53..64, facts 173..190 are this owner's):
 *
 *   BUILD  u32 what (enum ToriRSMapEdBuildWhat) [, string quest]
 *          (the quest kinds name their quest, a QUEUE.tsv test_id
 *          (ToriRSMapEd_BuildQuestValid); the other kinds take none)
 *          -> broadcast FACT_BUILD_LINE  u32 what, string line   (0+)
 *             then FACT_BUILD_DONE       u32 what, u32 status (EditorHost_Status),
 *                                        u32 unit_count, unit_count * string unit,
 *                                        u32 archive_count,
 *                                        archive_count * (u32 index, u32 archive)
 *          REFUSED while another build or a RUN is in flight; UNSUPPORTED with
 *          no runner bound (TORIRSMAPED_CAP_BUILD clear) or for a `what` the
 *          runner does not offer.
 *
 *   RUN    (empty)
 *          -> SAVE_ALL's broadcasts (FACT_SAVED), then ONE build's FACT_BUILD_*
 *             stream (TORIRSMAPED_BUILD_RUN: the packs and the client cache at
 *             once, `make -j2`; RUN_STEP_BUILD), then the live link's
 *             FACT_LIVE_LINE rows, then
 *             broadcast FACT_RUN_DONE   u32 status, u32 step (the step that
 *                                       failed, or RUN_STEP_DONE),
 *                                       string message,
 *                                       u32 scripts_swapped, u32 groups_replaced,
 *                                       u32 squares_reloaded, u32 spawns_changed,
 *                                       u32 restart_count, restart_count * string
 *             and FACT_LIVE_STATUS (torirs_maped_live.h).
 *          Stops at the FIRST failure: a save that failed builds nothing, a
 *          failed build reloads nothing.
 *          While a RUN is in flight, broadcast FACT_RUN_PROGRESS (180):
 *             u32 step (enum ToriRSMapEdRunStep), u32 step_index (1-based),
 *             u32 step_count, u32 percent (0..100 of the whole RUN),
 *             u32 step_percent, u32 elapsed_ms (in this step),
 *             u32 remaining_ms (0xFFFFFFFF: no earlier RUN to estimate from),
 *             string detail (the unit being built, the play client's state)
 *          at every step change and at most four times a second in between.
 *          The estimate weighs each step by its duration in the last RUN that
 *          finished on this lane (<live>/run_times.ini) and, for the build,
 *          by the units it has reported against that RUN's count.
 *
 * Build and live facts go to EVERY v2 connection (design #protocol): they
 * describe the shared tree and the shared live lane, so every window's Live
 * panel shows the same run.
 *
 * BUILD NOTE: torirs_maped_build.c is compiled INTO torirs_maped.c (an
 * #include at its end, like the catalog and the views), so every link line
 * that names torirs_maped.c carries it. Never link it on its own.
 */

#include "torirs_maped.h"

#include <stddef.h>
#include <stdint.h>

enum
{
    TORIRSMAPED_REQ_BUILD = 53,
    TORIRSMAPED_REQ_RUN = 54,

    TORIRSMAPED_FACT_BUILD_LINE = 173,
    TORIRSMAPED_FACT_BUILD_DONE = 174,
    /** RUN's outcome (178, inside live-editor's 173..190). */
    TORIRSMAPED_FACT_RUN_DONE = 178,
    /** Where the RUN in flight is (see the header comment). */
    TORIRSMAPED_FACT_RUN_PROGRESS = 180,

    /** The whole live-editor fact range: a client that does not interpret
     *  them still knows they are broadcasts (editor_host_remote.c). */
    TORIRSMAPED_FACT_LIVE_RANGE_FIRST = 173,
    TORIRSMAPED_FACT_LIVE_RANGE_LAST = 190,
    TORIRSMAPED_REQ_LIVE_RANGE_FIRST = 53,
    TORIRSMAPED_REQ_LIVE_RANGE_LAST = 64,
};

/** What a BUILD builds (design #protocol: packs, client cache, scripts lint,
 *  cs2 compile). */
enum ToriRSMapEdBuildWhat
{
    /** `make -C src torirsserver-packs`: allocation, both script packs and
     *  the server pack, incremental. */
    TORIRSMAPED_BUILD_PACKS = 1,
    /** `make -C src maped-client-cache`: the incremental client config pack
     *  (cachepack pack --incremental) plus the changed map squares. */
    TORIRSMAPED_BUILD_CLIENT_CACHE = 2,
    /** Reserved: no runner offers them yet (UNSUPPORTED). */
    TORIRSMAPED_BUILD_SCRIPTS_LINT = 3,
    TORIRSMAPED_BUILD_CS2 = 4,
    /** `make -C src maped-run-build`: PACKS and CLIENT_CACHE together under
     *  -j2 (src/maped/live-editor.mk), RUN's own build. One job, one
     *  FACT_BUILD_* stream, both builds' units and archives. */
    TORIRSMAPED_BUILD_RUN = 5,
    /* 16..: the quest board's jobs (workbench 6.7; src/maped/shop-quest.mk).
     * Each names one quest; neither touches the lane's packs or cache, and
     * neither is a RUN step. */
    /** `make -C src maped-quest-scaffold MAPED_QUEST=<id>`:
     *  tools/quest_gate/new_quest.py <id> writes test/quests/<id>.lua. */
    TORIRSMAPED_BUILD_QUEST_SCAFFOLD = 16,
    /** `make -C src maped-quest-gate MAPED_QUEST=<id>`: run.py <id>
     *  --no-build, then gate.py <id>, whose verdict lands in
     *  build/maped_quest_board/<id>.gate (the board reads it). */
    TORIRSMAPED_BUILD_QUEST_GATE = 17,
    /** The last `what`; the test runner keeps a script per value. */
    TORIRSMAPED_BUILD_WHAT_LAST = TORIRSMAPED_BUILD_QUEST_GATE,
};

/** A quest kind's argument: 1..63 bytes of [a-z0-9_] (a QUEUE.tsv test_id),
 *  which is also all a make variable on the command line may hold here.
 *  Inline, like ToriRSMapEdCatalog_KindName, so a client that links no
 *  server code (the web) can check what it sends. */
static inline int
ToriRSMapEd_BuildQuestValid(const char* quest)
{
    size_t length = 0;

    while( quest[length] )
    {
        char const c = quest[length];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
        length++;
    }
    return length > 0 && length < 64;
}

/** 1 for the `what`s that name a quest. */
static inline int
ToriRSMapEd_BuildWhatTakesQuest(uint32_t what)
{
    return what == TORIRSMAPED_BUILD_QUEST_SCAFFOLD || what == TORIRSMAPED_BUILD_QUEST_GATE;
}

/** RUN's steps, in order; FACT_RUN_DONE's `step`. */
enum ToriRSMapEdRunStep
{
    TORIRSMAPED_RUN_STEP_IDLE = 0,
    TORIRSMAPED_RUN_STEP_SAVE = 1,
    TORIRSMAPED_RUN_STEP_PACKS = 2,
    TORIRSMAPED_RUN_STEP_CLIENT_CACHE = 3,
    TORIRSMAPED_RUN_STEP_RELOAD = 4,
    /** Every step succeeded. */
    TORIRSMAPED_RUN_STEP_DONE = 5,
    /** After the reload: the lane's play client is checked (is it on the
     *  server?) and started or restarted (torirs_maped_live.h "The play
     *  client"). Runs between RELOAD and DONE; numbered after DONE so DONE
     *  keeps the value every older peer compares against. */
    TORIRSMAPED_RUN_STEP_PLAY = 6,
    /** RUN's build since 2026-11-02: packs and client cache at once
     *  (TORIRSMAPED_BUILD_RUN). Replaces PACKS then CLIENT_CACHE in a RUN's
     *  sequence (those two remain the manual BUILD buttons' steps and names);
     *  numbered after PLAY for the same reason PLAY is after DONE. */
    TORIRSMAPED_RUN_STEP_BUILD = 7,
};

/** The step's name, for logs and the Live panel ("save", "packs", ...). */
const char*
ToriRSMapEd_RunStepName(uint32_t step);

const char*
ToriRSMapEd_BuildWhatName(uint32_t what);

enum
{
    TORIRSMAPED_LANE_PATH_MAX = 1024,
    /** Squares one client build repacks by name. */
    TORIRSMAPED_BUILD_SQUARES_MAX = 512,
    /** A tool line longer than this is cut (the rest of it is dropped). */
    TORIRSMAPED_BUILD_LINE_MAX = 1024,
};

/**
 * The live LANE: where a build reads, where it writes, and what a launched
 * live server runs. Everything a build writes lands under `live_dir`, which
 * must not be inside `content_dir` (ToriRSMapEd_LaneValid):
 *
 *   <live_dir>/scripts         the base script pack (TORIRSSERVER_SCRIPT_OUT)
 *   <live_dir>/scripts_lanes   the lane script pack (TORIRSSERVER_PACKS_LANE_OUT)
 *   <live_dir>/servpack        the server pack (SERVPACK_OUT)
 *   <live_dir>/cache           the client cache the live JS5 serves
 *   <live_dir>/map_archives.txt the squares the next client build repacks
 *   <live_dir>/saves           the launched server's saves and accounts
 *                              (TORIRSSERVER_SAVES), the play account's too
 *   <live_dir>/play            the play client's manifest, cache and prefs
 *   <live_dir>/logs            server.log, js5.log, io.log, play.log
 *
 * The id allocation `torirsserver-packs` runs first still appends to the
 * content tree's pack/<ns>.alloc ledgers: that is what allocation is. A lane
 * whose content tree is read-only cannot build packs.
 */
struct ToriRSMapEdLane
{
    /** Repository root: builds run `make -C <repo_root>/src ...`. */
    char repo_root[TORIRSMAPED_LANE_PATH_MAX];
    /** The content tree the builds read (TORIRSSERVER_CONTENT_DIR). */
    char content_dir[TORIRSMAPED_LANE_PATH_MAX];
    char live_dir[TORIRSMAPED_LANE_PATH_MAX];
    /** The client cache a first client build copies (--base); "" = none. */
    char base_cache[TORIRSMAPED_LANE_PATH_MAX];
    /** "osrs241". */
    char rev[16];
    /** The `make` to run; "" = "make". */
    char make[TORIRSMAPED_LANE_PATH_MAX];

    /* What LIVE_ATTACH launch starts (torirs_maped_live.h). "" binaries are
     * not started; the game server is required. */
    char server_bin[TORIRSMAPED_LANE_PATH_MAX];
    char js5_bin[TORIRSMAPED_LANE_PATH_MAX];
    char io_bin[TORIRSMAPED_LANE_PATH_MAX];
    /** io_server's --manifest and --root; io_server is skipped without one. */
    char io_manifest[TORIRSMAPED_LANE_PATH_MAX];
    char io_root[TORIRSMAPED_LANE_PATH_MAX];
    int game_port;
    int js5_port;
    int io_port;

    /* The play client (torirs_maped_live.h "The play client"): a game client
     * logged into the lane's server through its JS5. "" play_bin = none. */
    char play_bin[TORIRSMAPED_LANE_PATH_MAX];
    /** The client manifest the play manifest is written from (its cache dir,
     *  net host and port and relative paths rewritten). */
    char play_manifest[TORIRSMAPED_LANE_PATH_MAX];
    /** The save copied in as the play account's first save, so it does not
     *  start on Tutorial Island; "" = none (a fresh character). */
    char play_fixture[TORIRSMAPED_LANE_PATH_MAX];
    /** The play account; "" = derived from the game port
     *  (ToriRSMapEd_LanePlayAccount). */
    char play_user[16];
    char play_pass[24];
};

/**
 * Fill a lane from its roots with every derived path at its default: the
 * pack outputs under `live_dir`, the binaries under `<repo_root>/src/
 * build_opt/` (io_server under build/), ports 43620 (game), 43621 (js5),
 * 43622 (io), base cache `<repo_root>/cache.<rev>`; the play client
 * `<repo_root>/src/torirs` on `<repo_root>/manifests/manifest_<rev>.ini` with
 * the `test/quests/fixtures/fresh_lumbridge.ini` save.
 */
void
ToriRSMapEd_LaneDefaults(
    struct ToriRSMapEdLane* lane,
    const char* repo_root,
    const char* content_dir,
    const char* live_dir);

/** 1 when the lane may be bound: every root set, and live_dir is not the
 *  content tree or under it (compared after resolving both). 0 with the
 *  reason in `why`. A lane comes from a flag or an environment variable, so
 *  a bad one is an answer, not an assert. */
int
ToriRSMapEd_LaneValid(
    const struct ToriRSMapEdLane* lane,
    char* why,
    size_t why_size);

/** One build step. */
struct ToriRSMapEdBuildJob
{
    enum ToriRSMapEdBuildWhat what;
    /** CLIENT_CACHE: the squares saved since the last successful client
     *  build, as (map_x, map_z) pairs; their map archives are repacked. */
    const int32_t* squares;
    int square_count;
    /** The quest kinds: the quest (ToriRSMapEd_BuildQuestValid). */
    char quest[64];
};

/** Where a running build reports. */
struct ToriRSMapEdBuildSink
{
    void* user;
    /** One line the tool printed, without its newline. */
    void (*line)(void* user, const char* line);
    /** One unit the tool rebuilt ("compiled server/scripts/...", "repacked
     *  varp"): what FACT_BUILD_DONE lists. */
    void (*unit)(void* user, const char* unit);
    /** One archive the build wrote: what RUN's reload names. A client cache
     *  build names (index, archive) of the client cache; a packs build names
     *  (TORIRSMAPED_RELOAD_SERVPACK_INDEX, group) per server-pack group its
     *  manifest diff found changed (ToriRSMapEd_BuildServPackDiff). */
    void (*archive)(void* user, uint32_t index, uint32_t archive);
};

/**
 * The seam. `start` begins one job and returns at once; `poll` advances it
 * without blocking, reporting through `sink`, and answers 1 while it runs and
 * 0 once it finished (*out_status then set). One job at a time: the server
 * never starts a second before the first is done.
 */
struct ToriRSMapEdBuildRunner
{
    void* ctx;
    enum EditorHost_Status (*start)(void* ctx, const struct ToriRSMapEdBuildJob* job);
    int (*poll)(
        void* ctx,
        const struct ToriRSMapEdBuildSink* sink,
        enum EditorHost_Status* out_status);
    /** Stop a running job (server shutdown); no more reports. */
    void (*cancel)(void* ctx);
    /** Release ctx. May be NULL for a binding the host owns. */
    void (*free_)(void* ctx);
};

/**
 * Bind (or, with NULL, clear) the server's BuildRunner; sets
 * TORIRSMAPED_CAP_BUILD. The server takes ownership: it calls free_ at Close
 * or when another runner replaces it.
 */
void
ToriRSMapEd_SetBuildRunner(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdBuildRunner* runner);

/** Bind the lane the build steps and RUN use (pack paths, the reload's
 *  script manifest, launch). Copied. Returns 0 (and binds nothing) for an
 *  invalid lane. */
int
ToriRSMapEd_SetLane(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLane* lane);

/* ---- the commands ---------------------------------------------------------- */

enum
{
    TORIRSMAPED_BUILD_ARGV_MAX = 16,
};

/** One command: argv plus the files it reads that the runner writes first. */
struct ToriRSMapEdBuildCommand
{
    char argv[TORIRSMAPED_BUILD_ARGV_MAX][TORIRSMAPED_LANE_PATH_MAX + 64];
    int argc;
};

/**
 * The command a job runs on `lane` -- the ONE place the make line is spelled,
 * used by the native runner to spawn and by the test runner to record.
 * Returns 0 for a `what` no command exists for.
 */
int
ToriRSMapEd_BuildCommand(
    const struct ToriRSMapEdLane* lane,
    const struct ToriRSMapEdBuildJob* job,
    struct ToriRSMapEdBuildCommand* out);

/** The command as one shell-quoted line ("make -C /r/src torirsserver-packs
 *  TORIRSSERVER_CONTENT_DIR=..."): what a log and a test compare. */
int
ToriRSMapEd_BuildCommandFormat(
    const struct ToriRSMapEdBuildCommand* command,
    char* out,
    size_t out_size);

/** Write `<live_dir>/map_archives.txt` for `job`'s squares, one
 *  `maps=<(x << 8) | z>` row each (cachepack --archive-list). 1 on success. */
int
ToriRSMapEd_BuildWriteMapList(
    const struct ToriRSMapEdLane* lane,
    const struct ToriRSMapEdBuildJob* job);

/** Feed `<cache>/client.changed` text (cp_incremental.h) to sink->archive,
 *  one `archive <table> <archive> <type>` row each. Returns the row count, or
 *  -1 for text that is not that file. */
int
ToriRSMapEd_BuildParseChanged(
    const char* text,
    size_t length,
    const struct ToriRSMapEdBuildSink* sink);

/** The reload row index that names a SERVER-PACK group, as the game server
 *  reads it (TORIRSSERVER_RELOAD_SERVPACK_INDEX, torirs_server_control.h). */
#define TORIRSMAPED_RELOAD_SERVPACK_INDEX 255u

/** The server-pack group a reload row names for a changed manifest archive
 *  (index, archive): the index itself, except the per-kind defaults (192)
 *  and client-id (193) groups, whose archive is the config kind -- those name
 *  the kind (its band group), since the server replaces a kind whole. */
uint32_t
ToriRSMapEd_BuildServPackReloadGroup(
    uint32_t index,
    uint32_t archive);

/**
 * Diff two `server/pack/pack.manifest` texts (docs/serverpack.md "The
 * manifests"): every `archive <index> <archive> <hash> ...` line whose hash
 * changed, appeared or went is a changed archive, and each distinct group
 * among them (ToriRSMapEd_BuildServPackReloadGroup) goes to sink->archive once
 * as (TORIRSMAPED_RELOAD_SERVPACK_INDEX, group) -- the rows RUN's reload sends
 * so the game server re-reads those groups (hot-reload contract section 1).
 * `*out_groups` gets the row count. Returns the changed-archive count, or -1
 * when either text is not a server-pack manifest.
 */
int
ToriRSMapEd_BuildServPackDiff(
    const char* before,
    size_t before_length,
    const char* after,
    size_t after_length,
    const struct ToriRSMapEdBuildSink* sink,
    int* out_groups);

/**
 * The line of a failed build's output that says why, out of its last
 * `count` lines (oldest first): the newest line that reads as an error
 * ("error", "failed", "refus", "cannot", "missing", "not found"), skipping
 * make's own "make: *** [...] Error N" and "== step" banners; else the newest
 * line that is neither. 0 (and `out` "") when no line qualifies.
 */
int
ToriRSMapEd_BuildFailureLine(
    const char* const* lines,
    int count,
    char* out,
    size_t out_size);

/** A tool line that names a rebuilt unit ("compiled  <path> — why",
 *  "cachepack: repacked <type> — why"): copies the unit into `out` and
 *  returns 1, else 0. */
int
ToriRSMapEd_BuildLineUnit(
    const char* line,
    char* out,
    size_t out_size);

/* ---- bindings --------------------------------------------------------------- */

/**
 * The native runner: spawns ToriRSMapEd_BuildCommand's command with stdout
 * and stderr on one non-blocking pipe and reads it line by line. CLIENT_CACHE
 * writes the map list first and reports `<live_dir>/cache/client.changed`'s
 * archives plus the squares' map archives once make succeeds. Returns 0 on
 * a platform with no process spawning (web, Windows), binding nothing.
 */
int
ToriRSMapEd_BuildRunnerNative(
    struct ToriRSMapEdBuildRunner* out,
    const struct ToriRSMapEdLane* lane);

/** The test runner's script for one `what`: what it prints and answers. */
struct ToriRSMapEdBuildMockScript
{
    const char* const* lines;
    int line_count;
    enum EditorHost_Status status;
    const char* const* units;
    int unit_count;
    const uint32_t* archives; /* (index, archive) pairs */
    int archive_count;
    /** Polls that report nothing before the job finishes (a slow build). */
    int idle_polls;
};

enum
{
    TORIRSMAPED_BUILD_MOCK_COMMANDS_MAX = 32,
};

/**
 * The test runner. Never spawns anything: `start` formats the command the
 * native runner WOULD run (ToriRSMapEd_BuildCommand on `lane`) into
 * `commands`, and the job then answers from `scripts[what]`. Host-owned
 * (free_ NULL); it must outlive the server it is bound to.
 */
struct ToriRSMapEdBuildMock
{
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildMockScript scripts[TORIRSMAPED_BUILD_WHAT_LAST + 1];
    char commands[TORIRSMAPED_BUILD_MOCK_COMMANDS_MAX][4096];
    uint32_t command_whats[TORIRSMAPED_BUILD_MOCK_COMMANDS_MAX];
    /** The squares each recorded CLIENT_CACHE job named, as text "x_z x_z". */
    char command_squares[TORIRSMAPED_BUILD_MOCK_COMMANDS_MAX][256];
    int command_count;

    /* the job in flight */
    int running;
    uint32_t what;
    int polls;
    int cancelled;
};

void
ToriRSMapEd_BuildRunnerMock(
    struct ToriRSMapEdBuildRunner* out,
    struct ToriRSMapEdBuildMock* mock);

#endif
