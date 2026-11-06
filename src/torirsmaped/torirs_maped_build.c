/*
 * BUILD, the BuildRunner bindings and the server's job state
 * (torirs_maped_build.h has the contract).
 *
 * BUILD NOTE: compiled INTO torirs_maped.c (`#include "torirs_maped_build.c"`
 * at its end, after the catalog and the views), never linked on its own. It
 * uses that unit's static send_frame / send_buf / save_doc, and every
 * file-scope name here starts with `build_` / `jobs_` (statics) or
 * `ToriRSMapEd` (the API) so nothing collides with it. torirs_maped_live.c
 * follows it in the same unit and shares struct ToriRSMapEdJobs.
 */

#include "torirs_maped_build.h"
#include "torirs_maped_live.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Processes and sockets: everywhere but the web and Windows. The bindings
 * that need them compile to "not available" there. */
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define TORIRSMAPED_JOBS_NATIVE 1
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#else
#define TORIRSMAPED_JOBS_NATIVE 0
#endif

enum
{
    /** Spawn files a RUN's reload can name at once. */
    JOBS_SPAWNS_MAX = 128,
    JOBS_SPAWN_PATH_MAX = 192,
    /** Client archives a reload can name at once. */
    JOBS_ARCHIVES_MAX = 4096,
    JOBS_STALE_MAX = 32,
    JOBS_SUBSCRIBE_MAX = 64,
    /** Players of CTL_FACT_PLAYERS kept (and passed on in FACT_LIVE_PLAY). */
    JOBS_PLAYER_ROWS = 16,
    /** A build's last lines, kept to say why it failed. */
    JOBS_BUILD_TAIL = 24,
    JOBS_BUILD_TAIL_LINE = 512,
    /** Lines of a failed build or a dead server's log repeated to the panel. */
    JOBS_FAILURE_LINES = 6,
};

/** One player the game server reported (CTL_FACT_PLAYERS). */
struct ToriRSMapEdJobsPlayer
{
    char account[64];
    char display[64];
    uint32_t slot;
    uint32_t generation;
    int32_t x;
    int32_t z;
    int32_t level;
};

/**
 * Everything the server keeps about builds, RUN and the live link. One per
 * server (maped->jobs), so two embeds never share any of it.
 */
struct ToriRSMapEdJobs
{
    int have_runner;
    struct ToriRSMapEdBuildRunner runner;
    int have_lane;
    struct ToriRSMapEdLane lane;
    int have_link;
    struct ToriRSMapEdLiveLink link;
    int have_play;
    struct ToriRSMapEdPlayLauncher play;

    /* ---- the build in flight ---- */
    int building;
    uint32_t build_what;
    /** Started by RUN: its end advances RUN instead of only reporting. */
    int build_for_run;
    struct ToriRSMapEdBuf build_units;
    uint32_t build_unit_count;
    uint32_t build_archives[2 * JOBS_ARCHIVES_MAX];
    int build_archive_count;
    /** The build's last JOBS_BUILD_TAIL lines, a ring (oldest at
     *  build_tail_head when full). */
    char build_tail[JOBS_BUILD_TAIL][JOBS_BUILD_TAIL_LINE];
    int build_tail_head;
    int build_tail_count;
    /** The packs or client-cache build whose last run failed (0 none): the
     *  lane is not launched on what it left until that build passes. */
    uint32_t build_failed_what;

    /* ---- RUN ---- */
    uint32_t run_step;

    /* ---- what the next reload names: everything since the last good one ---- */
    int32_t pending_squares[2 * TORIRSMAPED_BUILD_SQUARES_MAX];
    int pending_square_count;
    char pending_spawns[JOBS_SPAWNS_MAX][JOBS_SPAWN_PATH_MAX];
    int pending_spawn_count;
    uint32_t pending_archives[2 * JOBS_ARCHIVES_MAX];
    int pending_archive_count;

    /* ---- the live link ---- */
    uint32_t live_state;
    int launched;
    char address[256];
    char ticket[256];
    char server_name[128];
    uint32_t players;
    uint32_t tick;
    char script_stamp[128];
    char servpack_stamp[128];
    char stale[JOBS_STALE_MAX][256];
    int stale_count;
    char message[1024];
    struct ToriRSMapEdBuf ctl_in;
    int hello_sent;
    time_t last_status;
    /** A CTL_RELOAD is out; its RELOAD_DONE advances RUN. */
    int reload_waiting;
    time_t reload_deadline;
    /** The last LIVE_SUBSCRIBE, re-sent when the link comes back. */
    char sub_player[64];
    uint32_t sub_varps[JOBS_SUBSCRIBE_MAX];
    int sub_count;
    int sub_active;

    /* ---- the server's players (CTL_FACT_PLAYERS) ---- */
    /** The server sent CTL_FACT_PLAYERS since this link attached: it answers
     *  CTL_PLAYERS (an older server would close the link on it). */
    int players_known;
    uint32_t logins;
    uint32_t players_active;
    struct ToriRSMapEdJobsPlayer player_rows[JOBS_PLAYER_ROWS];
    int player_row_count;
    time_t last_players;

    /* ---- the play client (torirs_maped_live.h) ---- */
    /** enum ToriRSMapEdPlayState. */
    uint32_t play_state;
    /** A window plays in a browser tab (LIVE_PLAY browser): never spawn. */
    int play_browser;
    int play_pid;
    /** The exit status of the last one that exited (-signal for a signal). */
    int play_exit;
    int play_starts;
    time_t play_started;
    /** Started while the lane's ports were not up yet: spawn once they are
     *  (the client's boot needs JS5 at once), or give up at the deadline. */
    int play_wait_lane;
    time_t play_wait_deadline;
    /** The login it holds once seen: (slot, generation) changes on a relog. */
    uint32_t play_slot;
    uint32_t play_generation;
    int32_t play_x;
    int32_t play_z;
    int32_t play_level;
    char play_message[256];

    /* ---- RUN's play step: the reload's outcome, finished after it ---- */
    int run_play_waiting;
    time_t run_play_deadline;
    char run_message[256];
    uint32_t run_counts[4];
    struct ToriRSMapEdBuf run_restart;
    uint32_t run_restart_count;

    /* ---- FACT_RUN_PROGRESS: this RUN's clock and the last RUN's durations ---- */
    /** When the step in `run_step` began (monotonic ms); 0 outside a RUN. */
    int64_t step_started_ms;
    /** This RUN's measured steps so far, by jobs_run_step_index. */
    uint32_t step_run_ms[4];
    /** The last RUN that finished on this lane: per step, and the units its
     *  build reported (<live>/run_times.ini); 0 = unknown. */
    uint32_t step_last_ms[4];
    uint32_t build_last_units;
    int run_times_loaded;
    int64_t progress_sent_ms;
    char progress_detail[160];
};

static void jobs_run_step_enter(struct ToriRSMapEd* maped, uint32_t step);
static void jobs_progress_broadcast(struct ToriRSMapEd* maped, int force);

static void jobs_live_broadcast_status(struct ToriRSMapEd* maped);
static void jobs_run_finish(
    struct ToriRSMapEd* maped,
    enum EditorHost_Status status,
    uint32_t step,
    const char* message,
    const uint32_t* counts,
    const struct ToriRSMapEdBuf* restart_rows,
    uint32_t restart_count);
static void jobs_run_reload(struct ToriRSMapEd* maped);

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

const char*
ToriRSMapEd_RunStepName(uint32_t step)
{
    if( step == TORIRSMAPED_RUN_STEP_IDLE )
        return "idle";
    if( step == TORIRSMAPED_RUN_STEP_SAVE )
        return "save";
    if( step == TORIRSMAPED_RUN_STEP_PACKS )
        return "packs";
    if( step == TORIRSMAPED_RUN_STEP_CLIENT_CACHE )
        return "client cache";
    if( step == TORIRSMAPED_RUN_STEP_RELOAD )
        return "reload";
    if( step == TORIRSMAPED_RUN_STEP_PLAY )
        return "play client";
    if( step == TORIRSMAPED_RUN_STEP_BUILD )
        return "build";
    if( step == TORIRSMAPED_RUN_STEP_DONE )
        return "done";
    return "?";
}

const char*
ToriRSMapEd_BuildWhatName(uint32_t what)
{
    if( what == TORIRSMAPED_BUILD_PACKS )
        return "packs";
    if( what == TORIRSMAPED_BUILD_CLIENT_CACHE )
        return "client cache";
    if( what == TORIRSMAPED_BUILD_SCRIPTS_LINT )
        return "scripts lint";
    if( what == TORIRSMAPED_BUILD_CS2 )
        return "cs2 compile";
    if( what == TORIRSMAPED_BUILD_RUN )
        return "packs and client cache";
    if( what == TORIRSMAPED_BUILD_QUEST_SCAFFOLD )
        return "quest scaffold";
    if( what == TORIRSMAPED_BUILD_QUEST_GATE )
        return "quest gate";
    return "?";
}


/* ------------------------------------------------------------------ */
/* The lane                                                            */
/* ------------------------------------------------------------------ */

void
ToriRSMapEd_LaneDefaults(
    struct ToriRSMapEdLane* lane,
    const char* repo_root,
    const char* content_dir,
    const char* live_dir)
{
    assert(lane);
    assert(repo_root);
    assert(content_dir);
    assert(live_dir);

    memset(lane, 0, sizeof(*lane));
    snprintf(lane->repo_root, sizeof(lane->repo_root), "%s", repo_root);
    snprintf(lane->content_dir, sizeof(lane->content_dir), "%s", content_dir);
    snprintf(lane->live_dir, sizeof(lane->live_dir), "%s", live_dir);
    snprintf(lane->rev, sizeof(lane->rev), "osrs241");
    snprintf(lane->base_cache, sizeof(lane->base_cache), "%s/cache.%s", repo_root, lane->rev);
    snprintf(lane->make, sizeof(lane->make), "make");
    snprintf(lane->server_bin, sizeof(lane->server_bin), "%s/src/build_opt/torirsserver", repo_root);
    snprintf(lane->js5_bin, sizeof(lane->js5_bin), "%s/src/build_opt/js5_server", repo_root);
    snprintf(lane->io_bin, sizeof(lane->io_bin), "%s/src/build/io_server", repo_root);
    snprintf(lane->io_root, sizeof(lane->io_root), "%s/build-web", repo_root);
    lane->game_port = 43620;
    lane->js5_port = 43621;
    lane->io_port = 43622;
    snprintf(lane->play_bin, sizeof(lane->play_bin), "%s/src/torirs", repo_root);
    snprintf(lane->play_manifest, sizeof(lane->play_manifest), "%s/manifests/manifest_%s.ini", repo_root,
        lane->rev);
    snprintf(lane->play_fixture, sizeof(lane->play_fixture), "%s/test/quests/fixtures/fresh_lumbridge.ini",
        repo_root);
}

/** `path` resolved when it exists, else its parent resolved plus the last
 *  component (a live dir not made yet still compares truthfully). */
static void
build_resolve(
    const char* path,
    char* out,
    size_t out_size)
{
#if TORIRSMAPED_JOBS_NATIVE
    char resolved[PATH_MAX];
    if( realpath(path, resolved) )
    {
        snprintf(out, out_size, "%s", resolved);
        return;
    }
    {
        char parent[PATH_MAX];
        const char* slash = strrchr(path, '/');
        if( slash && slash != path )
        {
            snprintf(parent, sizeof(parent), "%.*s", (int)(slash - path), path);
            if( realpath(parent, resolved) )
            {
                snprintf(out, out_size, "%s%s", resolved, slash);
                return;
            }
        }
    }
#endif
    snprintf(out, out_size, "%s", path);
}

int
ToriRSMapEd_LaneValid(
    const struct ToriRSMapEdLane* lane,
    char* why,
    size_t why_size)
{
    char live[TORIRSMAPED_LANE_PATH_MAX + 256];
    char content[TORIRSMAPED_LANE_PATH_MAX + 256];
    size_t content_length;

    assert(lane);
    assert(why);

    why[0] = '\0';
    if( !lane->repo_root[0] || !lane->content_dir[0] || !lane->live_dir[0] )
    {
        snprintf(why, why_size, "the lane needs a repo root, a content dir and a live dir");
        return 0;
    }
    build_resolve(lane->live_dir, live, sizeof(live));
    build_resolve(lane->content_dir, content, sizeof(content));
    content_length = strlen(content);
    while( content_length > 1 && content[content_length - 1] == '/' )
        content[--content_length] = '\0';
    if( strncmp(live, content, content_length) == 0
        && (live[content_length] == '\0' || live[content_length] == '/') )
    {
        snprintf(why, why_size, "the live dir %s is inside the content tree %s: a build would write "
                                "into the tree it reads",
            live, content);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static void
build_arg(
    struct ToriRSMapEdBuildCommand* command,
    const char* format,
    const char* a,
    const char* b)
{
    assert(command->argc < TORIRSMAPED_BUILD_ARGV_MAX);
    snprintf(command->argv[command->argc], sizeof(command->argv[0]), format, a, b);
    command->argc++;
}

int
ToriRSMapEd_BuildCommand(
    const struct ToriRSMapEdLane* lane,
    const struct ToriRSMapEdBuildJob* job,
    struct ToriRSMapEdBuildCommand* out)
{
    assert(lane);
    assert(job);
    assert(out);

    int const packs = job->what == TORIRSMAPED_BUILD_PACKS || job->what == TORIRSMAPED_BUILD_RUN;
    int const client = job->what == TORIRSMAPED_BUILD_CLIENT_CACHE || job->what == TORIRSMAPED_BUILD_RUN;

    memset(out, 0, sizeof(*out));
    if( ToriRSMapEd_BuildWhatTakesQuest(job->what) )
    {
        /* src/maped/shop-quest.mk: the quest board's targets run the quest
         * gate's own tools from the repository root. The quest lands on
         * make's command line, so only a valid one gets this far. */
        assert(ToriRSMapEd_BuildQuestValid(job->quest));
        build_arg(out, "%s%s", lane->make[0] ? lane->make : "make", "");
        build_arg(out, "-C%s%s", "", "");
        build_arg(out, "%s/src%s", lane->repo_root, "");
        build_arg(out, "--no-print-directory%s%s", "", "");
        build_arg(out, "%s%s",
            job->what == TORIRSMAPED_BUILD_QUEST_SCAFFOLD ? "maped-quest-scaffold" : "maped-quest-gate", "");
        build_arg(out, "MAPED_QUEST=%s%s", job->quest, "");
        build_arg(out, "TORIRSSERVER_CONTENT_DIR=%s%s", lane->content_dir, "");
        build_arg(out, "OBJVERIFY_SKIP=1%s%s", "", "");
        return 1;
    }
    if( !packs && !client )
        return 0;

    build_arg(out, "%s%s", lane->make[0] ? lane->make : "make", "");
    build_arg(out, "-C%s%s", "", "");
    build_arg(out, "%s/src%s", lane->repo_root, "");
    build_arg(out, "--no-print-directory%s%s", "", "");
    /* src/maped/live-editor.mk: maped-lane-packs seeds a brand-new lane from
     * the tree's own pack outputs, then tools/build_packs.py under the
     * project's own target (allocation, the base and lane script packs and
     * the server pack, incremental); maped-client-cache is cachepack pack
     * --incremental into the live cache, then the named map squares;
     * maped-run-build is both at once under -j2. */
    if( job->what == TORIRSMAPED_BUILD_PACKS )
        build_arg(out, "maped-lane-packs%s%s", "", "");
    else if( job->what == TORIRSMAPED_BUILD_CLIENT_CACHE )
        build_arg(out, "maped-client-cache%s%s", "", "");
    else
        build_arg(out, "maped-run-build%s%s", "", "");
    build_arg(out, "TORIRSSERVER_CONTENT_DIR=%s%s", lane->content_dir, "");
    if( packs )
    {
        build_arg(out, "TORIRSSERVER_SCRIPT_OUT=%s/scripts%s", lane->live_dir, "");
        build_arg(out, "TORIRSSERVER_PACKS_LANE_OUT=%s/scripts_lanes%s", lane->live_dir, "");
        build_arg(out, "SERVPACK_OUT=%s/servpack%s", lane->live_dir, "");
    }
    if( client )
    {
        build_arg(out, "MAPED_LIVE_CACHE=%s/cache%s", lane->live_dir, "");
        build_arg(out, "TORIRSSERVER_CACHE_BASE=%s%s", lane->base_cache, "");
        build_arg(out, "MAPED_CACHE_REV=%s%s", lane->rev, "");
        if( job->square_count > 0 )
            build_arg(out, "MAPED_MAP_ARCHIVES=%s/map_archives.txt%s", lane->live_dir, "");
        else
            build_arg(out, "MAPED_MAP_ARCHIVES=%s%s", "", "");
    }
    /* Nothing of the object directory is compiled by these targets: the
     * verifier's half second per make is not owed (src/makefile). */
    build_arg(out, "OBJVERIFY_SKIP=1%s%s", "", "");
    return 1;
}

int
ToriRSMapEd_BuildCommandFormat(
    const struct ToriRSMapEdBuildCommand* command,
    char* out,
    size_t out_size)
{
    size_t at = 0;

    assert(command);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    for( int i = 0; i < command->argc; i++ )
    {
        const char* arg = command->argv[i];
        int plain = arg[0] != '\0';
        for( const char* p = arg; *p; p++ )
        {
            if( !(*p == '/' || *p == '.' || *p == '-' || *p == '_' || *p == '=' || *p == ':'
                    || *p == ',' || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z')
                    || (*p >= 'A' && *p <= 'Z')) )
                plain = 0;
        }
        if( at + 1 < out_size && i > 0 )
            out[at++] = ' ';
        if( plain )
            at += (size_t)snprintf(out + at, at < out_size ? out_size - at : 0, "%s", arg);
        else
        {
            /* single-quoted, with ' as '\'' */
            if( at + 1 < out_size )
                out[at++] = '\'';
            for( const char* p = arg; *p; p++ )
            {
                if( *p == '\'' )
                    at += (size_t)snprintf(out + at, at < out_size ? out_size - at : 0, "'\\''");
                else if( at + 1 < out_size )
                    out[at++] = *p;
            }
            if( at + 1 < out_size )
                out[at++] = '\'';
        }
        if( at >= out_size )
        {
            out[out_size - 1] = '\0';
            return (int)(out_size - 1);
        }
        out[at] = '\0';
    }
    return (int)at;
}

int
ToriRSMapEd_BuildWriteMapList(
    const struct ToriRSMapEdLane* lane,
    const struct ToriRSMapEdBuildJob* job)
{
    char path[TORIRSMAPED_LANE_PATH_MAX + 32];
    char temporary[TORIRSMAPED_LANE_PATH_MAX + 40];
    FILE* file;
    int ok = 1;

    assert(lane);
    assert(job);
    assert(job->squares || job->square_count == 0);

    snprintf(path, sizeof(path), "%s/map_archives.txt", lane->live_dir);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if( !file )
        return 0;
    fprintf(file, "# The squares the editor saved since the last client build "
                  "(torirsmaped/torirs_maped_build.h).\n");
    for( int i = 0; i < job->square_count; i++ )
    {
        int const x = job->squares[2 * i];
        int const z = job->squares[2 * i + 1];
        if( x < 0 || x > 255 || z < 0 || z > 255 )
            continue;
        if( fprintf(file, "maps=%d\n", (x << 8) | z) < 0 )
            ok = 0;
    }
    if( fclose(file) != 0 )
        ok = 0;
    return ok && rename(temporary, path) == 0;
}

int
ToriRSMapEd_BuildParseChanged(
    const char* text,
    size_t length,
    const struct ToriRSMapEdBuildSink* sink)
{
    size_t at = 0;
    int rows = 0;

    assert(text || length == 0);
    assert(sink);

    while( at < length )
    {
        char line[256];
        size_t end = at;
        unsigned table;
        unsigned archive;
        char type[64];

        while( end < length && text[end] != '\n' )
            end++;
        if( end - at >= sizeof(line) )
            return -1;
        memcpy(line, text + at, end - at);
        line[end - at] = '\0';
        at = end + 1;
        if( !line[0] )
            continue;
        if( sscanf(line, "archive %u %u %63s", &table, &archive, type) != 3 )
            return -1;
        if( sink->archive )
            sink->archive(sink->user, (uint32_t)table, (uint32_t)archive);
        rows++;
    }
    return rows;
}

/* ---- the server pack's manifest diff -------------------------------------- */

/* One `archive <index> <archive> <hash> <rev> <type>` line of a server-pack
 * manifest (docs/serverpack.md "The manifests"). */
struct build_servpack_row
{
    uint32_t index;
    uint32_t archive;
    unsigned long long hash;
};

static int
build_servpack_row_compare(
    const void* a,
    const void* b)
{
    const struct build_servpack_row* left = a;
    const struct build_servpack_row* right = b;

    if( left->index != right->index )
        return left->index < right->index ? -1 : 1;
    if( left->archive != right->archive )
        return left->archive < right->archive ? -1 : 1;
    return 0;
}

/* Every archive line of `text`, sorted. Returns the row count (*out owned by
 * the caller, NULL for none), or -1 when the text is not a server-pack
 * manifest. */
static int
build_servpack_rows(
    const char* text,
    size_t length,
    struct build_servpack_row** out)
{
    struct build_servpack_row* rows = NULL;
    int count = 0;
    int capacity = 0;
    size_t at = 0;
    int header = 0;

    assert(text || length == 0);
    assert(out);
    *out = NULL;
    while( at < length )
    {
        char line[512];
        size_t end = at;
        unsigned index;
        unsigned archive;
        unsigned long long hash;

        while( end < length && text[end] != '\n' )
            end++;
        if( end - at < sizeof(line) )
        {
            memcpy(line, text + at, end - at);
            line[end - at] = '\0';
        }
        else
            line[0] = '\0'; /* an input path that long is no archive line */
        at = end + 1;
        if( strncmp(line, "torirs-server-pack ", 19) == 0 )
            header = 1;
        if( strncmp(line, "archive ", 8) != 0 )
            continue;
        if( sscanf(line, "archive %u %u %llx", &index, &archive, &hash) != 3 )
        {
            free(rows);
            return -1;
        }
        if( count == capacity )
        {
            capacity = capacity ? capacity * 2 : 4096;
            rows = realloc(rows, (size_t)capacity * sizeof(*rows));
            assert(rows);
        }
        rows[count].index = index;
        rows[count].archive = archive;
        rows[count].hash = hash;
        count++;
    }
    if( !header )
    {
        free(rows);
        return -1;
    }
    if( count > 1 )
        qsort(rows, (size_t)count, sizeof(*rows), build_servpack_row_compare);
    *out = rows;
    return count;
}

uint32_t
ToriRSMapEd_BuildServPackReloadGroup(
    uint32_t index,
    uint32_t archive)
{
    /* The defaults (192) and client-id (193) groups hold one archive per
     * config kind, at archive = kind: the server reloads a kind whole, so the
     * row names the kind's band group, which it reads as that kind. */
    if( (index == 192u || index == 193u) && archive < 64u )
        return archive;
    return index;
}

int
ToriRSMapEd_BuildServPackDiff(
    const char* before,
    size_t before_length,
    const char* after,
    size_t after_length,
    const struct ToriRSMapEdBuildSink* sink,
    int* out_groups)
{
    struct build_servpack_row* old_rows;
    struct build_servpack_row* new_rows;
    int old_count;
    int new_count;
    int i = 0;
    int j = 0;
    int changed = 0;
    /* One reload row per group: a kind is replaced whole, so 400 changed
     * archives of loc are still one row. */
    uint8_t named[256];

    assert(sink);
    assert(out_groups);
    *out_groups = 0;
    old_count = build_servpack_rows(before, before_length, &old_rows);
    if( old_count < 0 )
        return -1;
    new_count = build_servpack_rows(after, after_length, &new_rows);
    if( new_count < 0 )
    {
        free(old_rows);
        return -1;
    }
    memset(named, 0, sizeof(named));
    while( i < old_count || j < new_count )
    {
        int order;
        uint32_t index;
        uint32_t archive;
        uint32_t group;

        if( i >= old_count )
            order = 1;
        else if( j >= new_count )
            order = -1;
        else
            order = build_servpack_row_compare(&old_rows[i], &new_rows[j]);
        if( order == 0 )
        {
            int same = old_rows[i].hash == new_rows[j].hash;
            index = new_rows[j].index;
            archive = new_rows[j].archive;
            i++;
            j++;
            if( same )
                continue;
        }
        else if( order < 0 )
        {
            /* gone from the new pack */
            index = old_rows[i].index;
            archive = old_rows[i].archive;
            i++;
        }
        else
        {
            /* new in the new pack */
            index = new_rows[j].index;
            archive = new_rows[j].archive;
            j++;
        }
        changed++;
        group = ToriRSMapEd_BuildServPackReloadGroup(index, archive);
        if( group > 254u || named[group] )
            continue;
        named[group] = 1;
        (*out_groups)++;
        if( sink->archive )
            sink->archive(sink->user, TORIRSMAPED_RELOAD_SERVPACK_INDEX, group);
    }
    free(old_rows);
    free(new_rows);
    return changed;
}

#if TORIRSMAPED_JOBS_NATIVE
/* `path` read whole into a malloc'd buffer (NUL-terminated), or NULL. */
static char*
build_read_whole(
    const char* path,
    size_t* out_length)
{
    FILE* file = fopen(path, "rb");
    char* text;
    long size;

    *out_length = 0;
    if( !file )
        return NULL;
    if( fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0 )
    {
        fclose(file);
        return NULL;
    }
    text = malloc((size_t)size + 1);
    assert(text);
    if( fread(text, 1, (size_t)size, file) != (size_t)size )
    {
        free(text);
        fclose(file);
        return NULL;
    }
    fclose(file);
    text[size] = '\0';
    *out_length = (size_t)size;
    return text;
}
#endif /* TORIRSMAPED_JOBS_NATIVE */

/** `needle` in `text`, ignoring ASCII case. */
static int
build_has_word(
    const char* text,
    const char* needle)
{
    size_t const length = strlen(needle);
    for( const char* p = text; *p; p++ )
    {
        size_t i = 0;
        while( i < length && p[i]
               && (p[i] | 0x20) == (needle[i] | 0x20) )
            i++;
        if( i == length )
            return 1;
    }
    return 0;
}

/** make's own words about a failure ("make: *** [x] Error 2", "make[1]:
 *  Leaving directory") and step banners say nothing a person needs. */
static int
build_line_is_noise(const char* line)
{
    while( *line == ' ' || *line == '\t' )
        line++;
    if( !line[0] )
        return 1;
    if( strncmp(line, "make", 4) == 0 && (line[4] == ':' || line[4] == '[') )
        return 1;
    if( strncmp(line, "== ", 3) == 0 )
        return 1;
    return 0;
}

int
ToriRSMapEd_BuildFailureLine(
    const char* const* lines,
    int count,
    char* out,
    size_t out_size)
{
    static const char* const words[] = { "error", "failed", "refus", "cannot", "missing", "not found",
        "lacks", "undefined", "unknown" };
    int fallback = -1;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( count <= 0 )
        return 0;
    assert(lines);
    for( int i = count - 1; i >= 0; i-- )
    {
        if( build_line_is_noise(lines[i]) )
            continue;
        if( fallback < 0 )
            fallback = i;
        for( size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++ )
        {
            if( build_has_word(lines[i], words[w]) )
            {
                snprintf(out, out_size, "%s", lines[i]);
                return 1;
            }
        }
    }
    if( fallback < 0 )
        return 0;
    snprintf(out, out_size, "%s", lines[fallback]);
    return 1;
}

int
ToriRSMapEd_BuildLineUnit(
    const char* line,
    char* out,
    size_t out_size)
{
    static const char* const prefixes[] = { "compiled ", "cachepack: repacked " };

    assert(line);
    assert(out);

    for( size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++ )
    {
        size_t const n = strlen(prefixes[p]);
        const char* start;
        const char* end;
        if( strncmp(line, prefixes[p], n) != 0 )
            continue;
        start = line + n;
        while( *start == ' ' )
            start++;
        /* the unit ends at the first space (its "— why" follows) */
        end = start;
        while( *end && *end != ' ' )
            end++;
        if( end == start )
            return 0;
        snprintf(out, out_size, "%.*s", (int)(end - start), start);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Processes (native only)                                             */
/* ------------------------------------------------------------------ */

#if TORIRSMAPED_JOBS_NATIVE

/** 1 when the environment entry `entry` ("NAME=VALUE") is named by `drop`:
 *  "NAME" drops exactly NAME, "PREFIX*" every name starting with PREFIX. */
static int
jobs_env_dropped(
    const char* entry,
    const char* const* drop)
{
    const char* eq = strchr(entry, '=');
    size_t const name = eq ? (size_t)(eq - entry) : strlen(entry);

    for( int d = 0; drop && drop[d]; d++ )
    {
        size_t const length = strlen(drop[d]);
        if( length > 0 && drop[d][length - 1] == '*' )
        {
            if( name >= length - 1 && strncmp(entry, drop[d], length - 1) == 0 )
                return 1;
        }
        else if( length == name && strncmp(entry, drop[d], name) == 0 )
            return 1;
    }
    return 0;
}

/** A child's environment: this process's minus what `drop` names
 *  (jobs_env_dropped), with `extra_env` ("K=V", NULL-terminated, may be
 *  NULL) layered over it. The array is owned (free it); the strings are
 *  borrowed from environ and `extra_env`. */
static char**
jobs_env_build(
    const char* const* extra_env,
    const char* const* drop)
{
    int base = 0;
    int extra = 0;
    int n = 0;
    char** env;

    while( environ[base] )
        base++;
    while( extra_env && extra_env[extra] )
        extra++;
    env = malloc((size_t)(base + extra + 1) * sizeof(char*));
    assert(env);
    for( int i = 0; i < base; i++ )
    {
        const char* eq = strchr(environ[i], '=');
        size_t const name = eq ? (size_t)(eq - environ[i]) : strlen(environ[i]);
        int replaced = jobs_env_dropped(environ[i], drop);
        for( int e = 0; e < extra && !replaced; e++ )
        {
            if( strncmp(extra_env[e], environ[i], name) == 0 && extra_env[e][name] == '=' )
                replaced = 1;
        }
        if( !replaced )
            env[n++] = environ[i];
    }
    for( int e = 0; e < extra; e++ )
        env[n++] = (char*)extra_env[e];
    env[n] = NULL;
    return env;
}

/** Spawn `argv` (PATH-searched when argv[0] has no '/') with environment
 *  `envp`. stdout and stderr go: with `out_fd`, to one non-blocking pipe
 *  whose read end is returned there; else with `log_path`, to that file
 *  (truncated); else to /dev/null. Returns the pid, or -1 with errno. */
static int
jobs_spawn_envp(
    char* const* argv,
    char* const* envp,
    const char* log_path,
    int* out_fd)
{
    posix_spawn_file_actions_t actions;
    int pipe_fds[2] = { -1, -1 };
    pid_t pid = -1;
    int rc;

    assert(argv);
    assert(argv[0]);
    assert(envp);

    posix_spawn_file_actions_init(&actions);
    if( out_fd )
    {
        if( pipe(pipe_fds) != 0 )
        {
            posix_spawn_file_actions_destroy(&actions);
            return -1;
        }
        posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], 1);
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], 2);
        posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
    }
    else if( log_path && log_path[0] )
    {
        posix_spawn_file_actions_addopen(&actions, 1, log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, 1, 2);
    }
    else
    {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    }
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);

    if( strchr(argv[0], '/') )
        rc = posix_spawn(&pid, argv[0], &actions, NULL, argv, envp);
    else
        rc = posix_spawnp(&pid, argv[0], &actions, NULL, argv, envp);
    posix_spawn_file_actions_destroy(&actions);

    if( out_fd )
        close(pipe_fds[1]);
    if( rc != 0 )
    {
        if( out_fd )
            close(pipe_fds[0]);
        errno = rc;
        return -1;
    }
    if( out_fd )
    {
        fcntl(pipe_fds[0], F_SETFL, fcntl(pipe_fds[0], F_GETFL, 0) | O_NONBLOCK);
        fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
        *out_fd = pipe_fds[0];
    }
    return (int)pid;
}

/** Spawn `argv` (PATH-searched when argv[0] has no '/'), `extra_env` (NULL
 *  or "K=V" strings, NULL-terminated) layered over this process's
 *  environment. Output as jobs_spawn_envp (`out_fd`, else `log_path`, else
 *  /dev/null). Returns the pid, or -1 with errno. */
static int
jobs_spawn_logged(
    char* const* argv,
    const char* const* extra_env,
    const char* log_path,
    int* out_fd)
{
    char** env = jobs_env_build(extra_env, NULL);
    int const pid = jobs_spawn_envp(argv, env, log_path, out_fd);
    int const saved = errno;

    free(env);
    errno = saved;
    return pid;
}

static int
jobs_spawn(
    char* const* argv,
    const char* const* extra_env,
    int* out_fd)
{
    return jobs_spawn_logged(argv, extra_env, NULL, out_fd);
}

/** SIGTERM, up to `grace_ms` for it to go, then SIGKILL; always reaped. */
static void
jobs_stop_process(
    int pid,
    int grace_ms)
{
    int status;

    if( pid <= 0 )
        return;
    kill((pid_t)pid, SIGTERM);
    for( int waited = 0; waited < grace_ms; waited += 20 )
    {
        if( waitpid((pid_t)pid, &status, WNOHANG) == (pid_t)pid )
            return;
        usleep(20 * 1000);
    }
    kill((pid_t)pid, SIGKILL);
    waitpid((pid_t)pid, &status, 0);
}

/* ---- the native runner ---------------------------------------------------- */

struct build_native
{
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildJob job;
    int32_t squares[2 * TORIRSMAPED_BUILD_SQUARES_MAX];
    int pid;
    int fd;
    int eof;
    int exited;
    int exit_ok;
    char partial[TORIRSMAPED_BUILD_LINE_MAX];
    size_t partial_length;
    /** A packs build: `<live>/servpack/pack.manifest` as it was before the
     *  build (NULL when there was none), diffed against the one it wrote. */
    char* servpack_before;
    size_t servpack_before_length;
};

static enum EditorHost_Status
build_native_start(
    void* ctx,
    const struct ToriRSMapEdBuildJob* job)
{
    struct build_native* native = ctx;
    struct ToriRSMapEdBuildCommand command;
    char* argv[TORIRSMAPED_BUILD_ARGV_MAX + 1];
    int fd = -1;

    assert(native);
    assert(job);
    assert(native->pid <= 0);

    if( !ToriRSMapEd_BuildCommand(&native->lane, job, &command) )
        return EDITOR_HOST_UNSUPPORTED;
    native->job = *job;
    native->job.square_count =
        job->square_count < TORIRSMAPED_BUILD_SQUARES_MAX ? job->square_count : TORIRSMAPED_BUILD_SQUARES_MAX;
    if( native->job.square_count > 0 )
        memcpy(native->squares, job->squares, (size_t)native->job.square_count * 2 * sizeof(int32_t));
    native->job.squares = native->squares;

    mkdir(native->lane.live_dir, 0755);
    free(native->servpack_before);
    native->servpack_before = NULL;
    native->servpack_before_length = 0;
    if( job->what == TORIRSMAPED_BUILD_PACKS || job->what == TORIRSMAPED_BUILD_RUN )
    {
        char path[TORIRSMAPED_LANE_PATH_MAX + 32];
        snprintf(path, sizeof(path), "%s/servpack/pack.manifest", native->lane.live_dir);
        native->servpack_before = build_read_whole(path, &native->servpack_before_length);
    }
    if( (job->what == TORIRSMAPED_BUILD_CLIENT_CACHE || job->what == TORIRSMAPED_BUILD_RUN)
        && native->job.square_count > 0 && !ToriRSMapEd_BuildWriteMapList(&native->lane, &native->job) )
    {
        TORIRS_ERR("torirsmaped: cannot write %s/map_archives.txt\n", native->lane.live_dir);
        return EDITOR_HOST_IO_ERROR;
    }
    for( int i = 0; i < command.argc; i++ )
        argv[i] = command.argv[i];
    argv[command.argc] = NULL;
    native->pid = jobs_spawn(argv, NULL, &fd);
    if( native->pid < 0 )
    {
        TORIRS_ERR("torirsmaped: cannot start %s: %s\n", argv[0], strerror(errno));
        native->pid = 0;
        return EDITOR_HOST_IO_ERROR;
    }
    native->fd = fd;
    native->eof = 0;
    native->exited = 0;
    native->exit_ok = 0;
    native->partial_length = 0;
    return EDITOR_HOST_OK;
}

static void
build_native_line(
    struct build_native* native,
    const struct ToriRSMapEdBuildSink* sink,
    const char* line)
{
    char unit[512];
    if( sink->line )
        sink->line(sink->user, line);
    if( sink->unit && ToriRSMapEd_BuildLineUnit(line, unit, sizeof(unit)) )
        sink->unit(sink->user, unit);
    (void)native;
}

/* After a packs build: every server-pack archive whose manifest line moved
 * becomes a reload row (TORIRSMAPED_RELOAD_SERVPACK_INDEX, group), and one
 * line says how many. */
static void
build_native_servpack_diff(
    struct build_native* native,
    const struct ToriRSMapEdBuildSink* sink)
{
    char path[TORIRSMAPED_LANE_PATH_MAX + 32];
    char line[256];
    char* after;
    size_t after_length;
    int groups = 0;
    int changed;

    snprintf(path, sizeof(path), "%s/servpack/pack.manifest", native->lane.live_dir);
    after = build_read_whole(path, &after_length);
    if( !native->servpack_before || !after )
    {
        snprintf(line, sizeof(line), "servpack: no manifest %s the build; no server-pack group "
                                     "is reloaded",
            native->servpack_before ? "after" : "before");
        changed = -2;
    }
    else
    {
        changed = ToriRSMapEd_BuildServPackDiff(native->servpack_before, native->servpack_before_length,
            after, after_length, sink, &groups);
        if( changed < 0 )
            snprintf(line, sizeof(line), "servpack: %s is not a server-pack manifest", path);
        else
            snprintf(line, sizeof(line), "servpack: %d archive(s) changed in %d group(s)", changed,
                groups);
    }
    if( sink->line )
        sink->line(sink->user, line);
    free(after);
    free(native->servpack_before);
    native->servpack_before = NULL;
    native->servpack_before_length = 0;
}

static int
build_native_poll(
    void* ctx,
    const struct ToriRSMapEdBuildSink* sink,
    enum EditorHost_Status* out_status)
{
    struct build_native* native = ctx;
    char chunk[16384];

    assert(native);
    assert(sink);
    assert(out_status);

    /* Polled only between a start and the poll that reported it done. */
    assert(native->pid > 0);
    while( !native->eof )
    {
        ssize_t got = read(native->fd, chunk, sizeof(chunk));
        if( got > 0 )
        {
            for( ssize_t i = 0; i < got; i++ )
            {
                char c = chunk[i];
                if( c == '\n' )
                {
                    native->partial[native->partial_length] = '\0';
                    if( native->partial_length > 0 && native->partial[native->partial_length - 1] == '\r' )
                        native->partial[native->partial_length - 1] = '\0';
                    build_native_line(native, sink, native->partial);
                    native->partial_length = 0;
                }
                else if( native->partial_length + 1 < sizeof(native->partial) )
                    native->partial[native->partial_length++] = c;
            }
            continue;
        }
        if( got == 0 )
        {
            native->eof = 1;
            close(native->fd);
            native->fd = -1;
            if( native->partial_length > 0 )
            {
                native->partial[native->partial_length] = '\0';
                build_native_line(native, sink, native->partial);
                native->partial_length = 0;
            }
            break;
        }
        if( errno == EINTR )
            continue;
        break; /* EAGAIN: nothing more this pass */
    }
    if( !native->exited )
    {
        int status = 0;
        pid_t done = waitpid((pid_t)native->pid, &status, WNOHANG);
        if( done == (pid_t)native->pid )
        {
            native->exited = 1;
            native->exit_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
    }
    if( !native->eof || !native->exited )
        return 1;

    native->pid = 0;
    if( !native->exit_ok )
    {
        *out_status = EDITOR_HOST_BAKE_FAILED;
        return 0;
    }
    if( native->job.what == TORIRSMAPED_BUILD_PACKS || native->job.what == TORIRSMAPED_BUILD_RUN )
        build_native_servpack_diff(native, sink);
    if( native->job.what == TORIRSMAPED_BUILD_CLIENT_CACHE || native->job.what == TORIRSMAPED_BUILD_RUN )
    {
        char path[TORIRSMAPED_LANE_PATH_MAX + 32];
        FILE* file;
        snprintf(path, sizeof(path), "%s/cache/client.changed", native->lane.live_dir);
        file = fopen(path, "rb");
        if( file )
        {
            char text[65536];
            size_t length = fread(text, 1, sizeof(text), file);
            fclose(file);
            if( ToriRSMapEd_BuildParseChanged(text, length, sink) < 0 )
                TORIRS_ERR("torirsmaped: %s is not a client.changed file\n", path);
        }
        for( int i = 0; i < native->job.square_count && sink->archive; i++ )
        {
            int const x = native->squares[2 * i];
            int const z = native->squares[2 * i + 1];
            if( x >= 0 && x <= 255 && z >= 0 && z <= 255 )
                sink->archive(sink->user, 5u, (uint32_t)((x << 8) | z));
        }
    }
    *out_status = EDITOR_HOST_OK;
    return 0;
}

static void
build_native_cancel(void* ctx)
{
    struct build_native* native = ctx;

    assert(native);
    if( native->fd >= 0 )
        close(native->fd);
    native->fd = -1;
    if( native->pid > 0 && !native->exited )
        jobs_stop_process(native->pid, 2000);
    native->pid = 0;
}

static void
build_native_free(void* ctx)
{
    struct build_native* native = ctx;
    if( !native )
        return;
    build_native_cancel(native);
    free(native->servpack_before);
    free(native);
}

#endif /* TORIRSMAPED_JOBS_NATIVE */

int
ToriRSMapEd_BuildRunnerNative(
    struct ToriRSMapEdBuildRunner* out,
    const struct ToriRSMapEdLane* lane)
{
    assert(out);
    assert(lane);

    memset(out, 0, sizeof(*out));
#if TORIRSMAPED_JOBS_NATIVE
    {
        struct build_native* native = calloc(1, sizeof(*native));
        assert(native);
        native->lane = *lane;
        native->fd = -1;
        out->ctx = native;
        out->start = build_native_start;
        out->poll = build_native_poll;
        out->cancel = build_native_cancel;
        out->free_ = build_native_free;
        return 1;
    }
#else
    (void)lane;
    return 0;
#endif
}

/* ---- the test runner -------------------------------------------------------- */

static enum EditorHost_Status
build_mock_start(
    void* ctx,
    const struct ToriRSMapEdBuildJob* job)
{
    struct ToriRSMapEdBuildMock* mock = ctx;
    struct ToriRSMapEdBuildCommand command;
    int slot;

    assert(mock);
    assert(job);
    assert(!mock->running);

    if( !ToriRSMapEd_BuildCommand(&mock->lane, job, &command) )
        return EDITOR_HOST_UNSUPPORTED;
    slot = mock->command_count < TORIRSMAPED_BUILD_MOCK_COMMANDS_MAX
               ? mock->command_count
               : TORIRSMAPED_BUILD_MOCK_COMMANDS_MAX - 1;
    ToriRSMapEd_BuildCommandFormat(&command, mock->commands[slot], sizeof(mock->commands[slot]));
    mock->command_whats[slot] = (uint32_t)job->what;
    mock->command_squares[slot][0] = '\0';
    for( int i = 0; i < job->square_count; i++ )
    {
        size_t const at = strlen(mock->command_squares[slot]);
        snprintf(mock->command_squares[slot] + at, sizeof(mock->command_squares[slot]) - at, "%s%d_%d",
            i ? " " : "", (int)job->squares[2 * i], (int)job->squares[2 * i + 1]);
    }
    if( mock->command_count < TORIRSMAPED_BUILD_MOCK_COMMANDS_MAX )
        mock->command_count++;
    mock->running = 1;
    mock->what = (uint32_t)job->what;
    mock->polls = 0;
    return EDITOR_HOST_OK;
}

static int
build_mock_poll(
    void* ctx,
    const struct ToriRSMapEdBuildSink* sink,
    enum EditorHost_Status* out_status)
{
    struct ToriRSMapEdBuildMock* mock = ctx;
    const struct ToriRSMapEdBuildMockScript* script;

    assert(mock);
    assert(sink);
    assert(out_status);
    assert(mock->running);
    assert(mock->what <= TORIRSMAPED_BUILD_WHAT_LAST);

    script = &mock->scripts[mock->what];
    if( mock->polls++ < script->idle_polls )
        return 1;
    for( int i = 0; i < script->line_count; i++ )
    {
        char unit[512];
        if( sink->line )
            sink->line(sink->user, script->lines[i]);
        if( sink->unit && ToriRSMapEd_BuildLineUnit(script->lines[i], unit, sizeof(unit)) )
            sink->unit(sink->user, unit);
    }
    for( int i = 0; i < script->unit_count && sink->unit; i++ )
        sink->unit(sink->user, script->units[i]);
    for( int i = 0; i < script->archive_count && sink->archive; i++ )
        sink->archive(sink->user, script->archives[2 * i], script->archives[2 * i + 1]);
    mock->running = 0;
    *out_status = script->status;
    return 0;
}

static void
build_mock_cancel(void* ctx)
{
    struct ToriRSMapEdBuildMock* mock = ctx;
    assert(mock);
    if( mock->running )
        mock->cancelled++;
    mock->running = 0;
}

void
ToriRSMapEd_BuildRunnerMock(
    struct ToriRSMapEdBuildRunner* out,
    struct ToriRSMapEdBuildMock* mock)
{
    assert(out);
    assert(mock);

    memset(out, 0, sizeof(*out));
    out->ctx = mock;
    out->start = build_mock_start;
    out->poll = build_mock_poll;
    out->cancel = build_mock_cancel;
    out->free_ = NULL;
}

/* ------------------------------------------------------------------ */
/* The server's job state                                              */
/* ------------------------------------------------------------------ */

static struct ToriRSMapEdJobs*
jobs_create(void)
{
    struct ToriRSMapEdJobs* jobs = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*jobs));
    assert(jobs);
    jobs->run_step = TORIRSMAPED_RUN_STEP_IDLE;
    jobs->live_state = TORIRSMAPED_LIVE_DETACHED;
    jobs->play_state = TORIRSMAPED_PLAY_NONE;
    jobs->play_x = jobs->play_z = jobs->play_level = -1;
    return jobs;
}

static void jobs_live_drop(struct ToriRSMapEd* maped, uint32_t state, const char* message);

static void
jobs_free(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    if( !jobs )
        return;
    if( jobs->have_link )
    {
        jobs_live_drop(maped, TORIRSMAPED_LIVE_DETACHED, "the editor server closed");
        if( jobs->link.free_ )
            jobs->link.free_(jobs->link.ctx);
    }
    if( jobs->have_play )
    {
        /* The play client is the lane's, like its servers: it goes too. */
        jobs->play.stop(jobs->play.ctx);
        if( jobs->play.free_ )
            jobs->play.free_(jobs->play.ctx);
    }
    ToriRSMapEd_BufFree(&jobs->run_restart);
    if( jobs->have_runner )
    {
        if( jobs->building && jobs->runner.cancel )
            jobs->runner.cancel(jobs->runner.ctx);
        if( jobs->runner.free_ )
            jobs->runner.free_(jobs->runner.ctx);
    }
    ToriRSMapEd_BufFree(&jobs->build_units);
    ToriRSMapEd_BufFree(&jobs->ctl_in);
    ToriMem_Free(ToriMem_Editor(), jobs);
    maped->jobs = NULL;
}

void
ToriRSMapEd_SetBuildRunner(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdBuildRunner* runner)
{
    struct ToriRSMapEdJobs* jobs;

    assert(maped);
    assert(maped->jobs);
    jobs = maped->jobs;
    /* Replacing the runner mid-build would orphan the job's process. */
    assert(!jobs->building);

    if( jobs->have_runner && jobs->runner.free_ )
        jobs->runner.free_(jobs->runner.ctx);
    memset(&jobs->runner, 0, sizeof(jobs->runner));
    jobs->have_runner = 0;
    maped->capabilities &= ~(uint32_t)TORIRSMAPED_CAP_BUILD;
    if( runner )
    {
        assert(runner->start);
        assert(runner->poll);
        jobs->runner = *runner;
        jobs->have_runner = 1;
        maped->capabilities |= TORIRSMAPED_CAP_BUILD;
    }
}

int
ToriRSMapEd_SetLane(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLane* lane)
{
    char why[2 * TORIRSMAPED_LANE_PATH_MAX + 256];

    assert(maped);
    assert(maped->jobs);
    assert(lane);

    if( !ToriRSMapEd_LaneValid(lane, why, sizeof(why)) )
    {
        TORIRS_ERR("torirsmaped: lane refused: %s\n", why);
        return 0;
    }
    maped->jobs->lane = *lane;
    maped->jobs->have_lane = 1;
    return 1;
}

/** One live-editor fact to every v2 connection. */
static void
jobs_broadcast(
    struct ToriRSMapEd* maped,
    uint32_t type,
    const struct ToriRSMapEdBuf* payload)
{
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct ToriRSMapEdSession* session = maped->sessions[i];
        if( session && session->alive && session->version >= TORIRSMAPED_PROTO_VERSION )
            send_buf(session, type, payload);
    }
}

/** A square saved, or a spawn file: what the next reload names. Called with
 *  every FACT_SAVED's rows (broadcast_saved). */
static void
jobs_note_saved(
    struct ToriRSMapEd* maped,
    const struct Editor_DocRef* rows,
    int count)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    /* Created at Open, freed at Close: a save outside that is a host bug. */
    assert(jobs);
    for( int i = 0; i < count; i++ )
    {
        if( rows[i].kind == EDITOR_DOC_SQUARE )
        {
            int map_x;
            int map_z;
            int known = 0;
            if( !Editor_SquareKeyParse(rows[i].key, &map_x, &map_z) )
                continue;
            for( int s = 0; s < jobs->pending_square_count; s++ )
            {
                if( jobs->pending_squares[2 * s] == map_x && jobs->pending_squares[2 * s + 1] == map_z )
                    known = 1;
            }
            if( known )
                continue;
            if( jobs->pending_square_count >= TORIRSMAPED_BUILD_SQUARES_MAX )
            {
                TORIRS_LOG("torirsmaped: more than %d squares pending a reload; m%d_%d not named\n",
                    TORIRSMAPED_BUILD_SQUARES_MAX, map_x, map_z);
                continue;
            }
            jobs->pending_squares[2 * jobs->pending_square_count] = map_x;
            jobs->pending_squares[2 * jobs->pending_square_count + 1] = map_z;
            jobs->pending_square_count++;
        }
        else if( rows[i].kind == EDITOR_DOC_SPAWN )
        {
            char path[JOBS_SPAWN_PATH_MAX];
            int known = 0;
            if( !Editor_HostDocPath(EDITOR_DOC_SPAWN, rows[i].key, 0, path, sizeof(path)) )
                continue;
            for( int s = 0; s < jobs->pending_spawn_count; s++ )
            {
                if( strcmp(jobs->pending_spawns[s], path) == 0 )
                    known = 1;
            }
            if( known )
                continue;
            if( jobs->pending_spawn_count >= JOBS_SPAWNS_MAX )
            {
                TORIRS_LOG("torirsmaped: more than %d spawn files pending a reload; %s not named\n",
                    JOBS_SPAWNS_MAX, path);
                continue;
            }
            snprintf(jobs->pending_spawns[jobs->pending_spawn_count++], JOBS_SPAWN_PATH_MAX, "%s", path);
        }
    }
}

/* ---- the build in flight ---------------------------------------------------- */

static void
jobs_build_sink_line(
    void* user,
    const char* line)
{
    struct ToriRSMapEd* maped = user;
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;

    {
        int const slot = (jobs->build_tail_head + jobs->build_tail_count) % JOBS_BUILD_TAIL;
        snprintf(jobs->build_tail[slot], JOBS_BUILD_TAIL_LINE, "%s", line);
        if( jobs->build_tail_count < JOBS_BUILD_TAIL )
            jobs->build_tail_count++;
        else
            jobs->build_tail_head = (jobs->build_tail_head + 1) % JOBS_BUILD_TAIL;
    }

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, maped->jobs->build_what);
    ToriRSMapEd_PutBytes(&payload, line,
        (uint32_t)(strlen(line) < TORIRSMAPED_BUILD_LINE_MAX ? strlen(line) : TORIRSMAPED_BUILD_LINE_MAX));
    jobs_broadcast(maped, TORIRSMAPED_FACT_BUILD_LINE, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
jobs_build_sink_unit(
    void* user,
    const char* unit)
{
    struct ToriRSMapEd* maped = user;
    ToriRSMapEd_PutString(&maped->jobs->build_units, unit);
    maped->jobs->build_unit_count++;
    snprintf(maped->jobs->progress_detail, sizeof(maped->jobs->progress_detail), "%s", unit);
}

static void
jobs_build_sink_archive(
    void* user,
    uint32_t index,
    uint32_t archive)
{
    struct ToriRSMapEdJobs* jobs = ((struct ToriRSMapEd*)user)->jobs;
    for( int i = 0; i < jobs->build_archive_count; i++ )
    {
        if( jobs->build_archives[2 * i] == index && jobs->build_archives[2 * i + 1] == archive )
            return;
    }
    if( jobs->build_archive_count >= JOBS_ARCHIVES_MAX )
        return;
    jobs->build_archives[2 * jobs->build_archive_count] = index;
    jobs->build_archives[2 * jobs->build_archive_count + 1] = archive;
    jobs->build_archive_count++;
}

/** Start `what`. On a refusal, FACT_BUILD_DONE says so at once. */
static enum EditorHost_Status
jobs_build_start(
    struct ToriRSMapEd* maped,
    uint32_t what,
    const char* quest,
    int for_run)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuildJob job;
    enum EditorHost_Status status;

    assert(!jobs->building);
    assert(quest);
    memset(&job, 0, sizeof(job));
    job.what = (enum ToriRSMapEdBuildWhat)what;
    snprintf(job.quest, sizeof(job.quest), "%s", quest);
    if( what == TORIRSMAPED_BUILD_CLIENT_CACHE || what == TORIRSMAPED_BUILD_RUN )
    {
        job.squares = jobs->pending_squares;
        job.square_count = jobs->pending_square_count;
    }
    if( !jobs->have_runner )
        status = EDITOR_HOST_UNSUPPORTED;
    else
        status = jobs->runner.start(jobs->runner.ctx, &job);
    if( status != EDITOR_HOST_OK )
        return status;

    jobs->building = 1;
    jobs->build_what = what;
    jobs->build_for_run = for_run;
    ToriRSMapEd_BufFree(&jobs->build_units);
    memset(&jobs->build_units, 0, sizeof(jobs->build_units));
    jobs->build_unit_count = 0;
    jobs->build_archive_count = 0;
    jobs->build_tail_head = 0;
    jobs->build_tail_count = 0;
    return EDITOR_HOST_OK;
}

static void
jobs_build_send_done(
    struct ToriRSMapEd* maped,
    uint32_t what,
    enum EditorHost_Status status,
    int with_results)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, what);
    ToriRSMapEd_PutU32(&payload, (uint32_t)status);
    if( with_results )
    {
        ToriRSMapEd_PutU32(&payload, jobs->build_unit_count);
        if( ToriRSMapEd_BufAvailable(&jobs->build_units) > 0 )
            ToriRSMapEd_BufWrite(&payload, ToriRSMapEd_BufPeek(&jobs->build_units),
                ToriRSMapEd_BufAvailable(&jobs->build_units));
        ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->build_archive_count);
        for( int i = 0; i < jobs->build_archive_count; i++ )
        {
            ToriRSMapEd_PutU32(&payload, jobs->build_archives[2 * i]);
            ToriRSMapEd_PutU32(&payload, jobs->build_archives[2 * i + 1]);
        }
    }
    else
    {
        ToriRSMapEd_PutU32(&payload, 0);
        ToriRSMapEd_PutU32(&payload, 0);
    }
    /* What the job wrote (a scaffolded test, a RUN's save) may be in this
     * server's tree: the lane counts' cache walks again on its next ask. */
    maped->lane_writes_own++;
    jobs_broadcast(maped, TORIRSMAPED_FACT_BUILD_DONE, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/** Why the build in `jobs->build_tail` failed, one line ("" when it printed
 *  nothing useful). */
static void
jobs_build_failure_line(
    const struct ToriRSMapEdJobs* jobs,
    char* out,
    size_t out_size)
{
    const char* lines[JOBS_BUILD_TAIL];
    for( int i = 0; i < jobs->build_tail_count; i++ )
        lines[i] = jobs->build_tail[(jobs->build_tail_head + i) % JOBS_BUILD_TAIL];
    (void)ToriRSMapEd_BuildFailureLine(lines, jobs->build_tail_count, out, out_size);
}

static void live_line(struct ToriRSMapEd* maped, const char* line, uint32_t length);

/** A failed build's last lines again, after its BUILD_DONE, so the panel's
 *  log ends on the reason and not on a hundred lines of progress. */
static void
jobs_build_report_failure(
    struct ToriRSMapEd* maped,
    uint32_t what)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    int shown = 0;
    int first;
    char line[JOBS_BUILD_TAIL_LINE + 64];

    /* The last JOBS_FAILURE_LINES lines that are not make's own. */
    first = jobs->build_tail_count;
    while( first > 0 && shown < JOBS_FAILURE_LINES )
    {
        first--;
        if( !build_line_is_noise(jobs->build_tail[(jobs->build_tail_head + first) % JOBS_BUILD_TAIL]) )
            shown++;
    }
    snprintf(line, sizeof(line), "the %s build failed; its last lines:", ToriRSMapEd_BuildWhatName(what));
    live_line(maped, line, (uint32_t)strlen(line));
    for( int i = first; i < jobs->build_tail_count; i++ )
    {
        const char* text = jobs->build_tail[(jobs->build_tail_head + i) % JOBS_BUILD_TAIL];
        if( build_line_is_noise(text) )
            continue;
        snprintf(line, sizeof(line), "  %s", text);
        live_line(maped, line, (uint32_t)strlen(line));
    }
    TORIRS_LOG("torirsmaped: the %s build failed\n", ToriRSMapEd_BuildWhatName(what));
}

/** The build finished: report it, keep its archives for the reload, and move
 *  RUN on (or stop it). */
static void
jobs_build_finished(
    struct ToriRSMapEd* maped,
    enum EditorHost_Status status)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    uint32_t const what = jobs->build_what;
    int const for_run = jobs->build_for_run;

    jobs->building = 0;
    jobs_build_send_done(maped, what, status, 1);
    /* Both builds name archives: the client cache's (index, archive) rows and
     * the packs build's server-pack groups (index 255, from the manifest
     * diff). Every one is owed to the next reload. */
    if( status == EDITOR_HOST_OK
        && (what == TORIRSMAPED_BUILD_CLIENT_CACHE || what == TORIRSMAPED_BUILD_PACKS
            || what == TORIRSMAPED_BUILD_RUN) )
    {
        for( int i = 0; i < jobs->build_archive_count; i++ )
        {
            int known = 0;
            for( int p = 0; p < jobs->pending_archive_count; p++ )
            {
                if( jobs->pending_archives[2 * p] == jobs->build_archives[2 * i]
                    && jobs->pending_archives[2 * p + 1] == jobs->build_archives[2 * i + 1] )
                    known = 1;
            }
            if( known || jobs->pending_archive_count >= JOBS_ARCHIVES_MAX )
                continue;
            jobs->pending_archives[2 * jobs->pending_archive_count] = jobs->build_archives[2 * i];
            jobs->pending_archives[2 * jobs->pending_archive_count + 1] = jobs->build_archives[2 * i + 1];
            jobs->pending_archive_count++;
        }
    }
    if( what == TORIRSMAPED_BUILD_PACKS || what == TORIRSMAPED_BUILD_CLIENT_CACHE || what == TORIRSMAPED_BUILD_RUN )
    {
        if( status != EDITOR_HOST_OK )
            jobs->build_failed_what = what;
        else if( jobs->build_failed_what == what )
            jobs->build_failed_what = 0;
    }
    if( status != EDITOR_HOST_OK )
        jobs_build_report_failure(maped, what);
    if( !for_run )
        return;

    if( status != EDITOR_HOST_OK )
    {
        char message[JOBS_BUILD_TAIL_LINE + 160];
        char why[JOBS_BUILD_TAIL_LINE];
        const char* which = ToriRSMapEd_BuildWhatName(what);
        uint32_t step = TORIRSMAPED_RUN_STEP_BUILD;

        jobs_build_failure_line(jobs, why, sizeof(why));
        if( what == TORIRSMAPED_BUILD_RUN )
        {
            /* The two halves ran at once: make's error lines name the half
             * that failed. */
            int packs_failed = 0;
            int client_failed = 0;
            for( int i = 0; i < jobs->build_tail_count; i++ )
            {
                const char* text = jobs->build_tail[(jobs->build_tail_head + i) % JOBS_BUILD_TAIL];
                if( strstr(text, "[torirsserver-packs]") || strstr(text, "== packs: FAILED") )
                    packs_failed = 1;
                if( strstr(text, "[maped-client-cache]") )
                    client_failed = 1;
            }
            which = packs_failed && client_failed ? "packs and client cache"
                    : packs_failed              ? "packs"
                    : client_failed             ? "client cache"
                                                : "packs and client cache";
        }
        else if( what == TORIRSMAPED_BUILD_PACKS )
            step = TORIRSMAPED_RUN_STEP_PACKS;
        else if( what == TORIRSMAPED_BUILD_CLIENT_CACHE )
            step = TORIRSMAPED_RUN_STEP_CLIENT_CACHE;
        /* RUN stops here: nothing is launched or reloaded on a failed build. */
        snprintf(message, sizeof(message), "the %s build failed%s%s; nothing was launched or reloaded", which,
            why[0] ? ": " : "", why);
        jobs_run_finish(maped, status, step, message, NULL, NULL, 0);
        return;
    }
    jobs_run_step_enter(maped, TORIRSMAPED_RUN_STEP_RELOAD);
    jobs_run_reload(maped);
}

static void
jobs_build_service(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuildSink sink;
    enum EditorHost_Status status = EDITOR_HOST_OK;

    if( !jobs->building )
        return;
    sink.user = maped;
    sink.line = jobs_build_sink_line;
    sink.unit = jobs_build_sink_unit;
    sink.archive = jobs_build_sink_archive;
    if( jobs->runner.poll(jobs->runner.ctx, &sink, &status) )
        return;
    jobs_build_finished(maped, status);
}

/* ---- BUILD ------------------------------------------------------------------ */

static void
handle_build(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    uint32_t what;
    enum EditorHost_Status status;
    char quest[128] = "";
    int has_quest = 0;

    if( length < 4 )
    {
        session->alive = 0;
        return;
    }
    what = ToriRSMapEd_ReadU32(body);
    if( length > 4 )
    {
        /* u32 what, string quest: what the quest kinds carry. */
        struct ToriRSMapEdReader reader;
        ToriRSMapEd_ReaderInit(&reader, body + 4, length - 4);
        ToriRSMapEd_GetString(&reader, quest, sizeof(quest));
        if( reader.bad || reader.at != reader.length )
        {
            session->alive = 0;
            return;
        }
        has_quest = 1;
    }
    if( jobs->building || jobs->run_step != TORIRSMAPED_RUN_STEP_IDLE )
        status = EDITOR_HOST_REFUSED;
    /* A quest kind without its quest, or another kind with one, or a quest
     * no make command line should carry, is answered, never run. */
    else if( ToriRSMapEd_BuildWhatTakesQuest(what) != has_quest
             || (has_quest && !ToriRSMapEd_BuildQuestValid(quest)) )
        status = EDITOR_HOST_REFUSED;
    else
        status = jobs_build_start(maped, what, quest, 0);
    if( status != EDITOR_HOST_OK )
    {
        jobs_build_send_done(maped, what, status, 0);
        return;
    }
    /* A runner that is done at once still answers through the service pass,
     * so every build reports the same way. */
    jobs_build_service(maped);
}

/* ---- RUN ---------------------------------------------------------------------- */

/* RUN's progress (FACT_RUN_PROGRESS, torirs_maped_build.h).
 *
 * A RUN is four steps -- save, build, reload, play client -- and the bar's
 * only honest source of "how far" is how long each took the last time it ran
 * on this lane: a build's tools print a line per unit, not a count, and a
 * reload answers when the server has swapped. So the lane remembers the last
 * finished RUN's step durations and its build's unit count
 * (<live>/run_times.ini), the whole-RUN percent weighs the steps by them, and
 * within the build the units reported so far are set against that count.
 * Without a history the step's own clock still moves the bar, asymptotically,
 * and the remaining time reads unknown. */

static const uint32_t k_run_steps[4] = { TORIRSMAPED_RUN_STEP_SAVE, TORIRSMAPED_RUN_STEP_BUILD,
    TORIRSMAPED_RUN_STEP_RELOAD, TORIRSMAPED_RUN_STEP_PLAY };
/** The weights without a history, i.e. a lane's FIRST RUN, which is the
 *  cold one: its build is a 20 s client pack on this Mac (2026-11-02). */
static const uint32_t k_run_default_ms[4] = { 300, 20000, 800, 3000 };
static const char* const k_run_times_keys[4] = { "save_ms", "build_ms", "reload_ms", "play_ms" };

static int64_t
jobs_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int
jobs_run_step_index(uint32_t step)
{
    for( int i = 0; i < 4; i++ )
    {
        if( k_run_steps[i] == step )
            return i;
    }
    return -1;
}

static void
jobs_run_times_path(
    const struct ToriRSMapEdJobs* jobs,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/run_times.ini", jobs->lane.live_dir);
}

static void
jobs_run_times_load(struct ToriRSMapEdJobs* jobs)
{
    char path[TORIRSMAPED_LANE_PATH_MAX + 32];
    char line[256];
    FILE* file;

    if( jobs->run_times_loaded || !jobs->have_lane )
        return;
    jobs->run_times_loaded = 1;
    jobs_run_times_path(jobs, path, sizeof(path));
    file = fopen(path, "rb");
    if( !file )
        return;
    while( fgets(line, sizeof(line), file) )
    {
        char key[64];
        unsigned value;
        if( sscanf(line, " %63[a-z_] = %u", key, &value) != 2 )
            continue;
        for( int i = 0; i < 4; i++ )
        {
            if( strcmp(key, k_run_times_keys[i]) == 0 )
                jobs->step_last_ms[i] = value;
        }
        if( strcmp(key, "build_units") == 0 )
            jobs->build_last_units = value;
    }
    fclose(file);
}

/** A RUN finished every step: its measured steps become the next RUN's
 *  estimate, on disk so a restarted editor server keeps them. */
static void
jobs_run_times_record(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    char path[TORIRSMAPED_LANE_PATH_MAX + 32];
    int const last = jobs_run_step_index(jobs->run_step);
    FILE* file;

    if( last >= 0 && jobs->step_started_ms > 0 )
    {
        int64_t const took = jobs_now_ms() - jobs->step_started_ms;
        jobs->step_run_ms[last] = took > 0 ? (uint32_t)took : 1;
    }
    for( int i = 0; i < 4; i++ )
    {
        if( jobs->step_run_ms[i] )
            jobs->step_last_ms[i] = jobs->step_run_ms[i];
    }
    if( jobs->build_unit_count )
        jobs->build_last_units = jobs->build_unit_count;
    if( !jobs->have_lane )
        return;
    jobs_run_times_path(jobs, path, sizeof(path));
    file = fopen(path, "wb");
    if( !file )
        return;
    fprintf(file, "; The last RUN that finished on this lane, step by step: what the RUN\n"
                  "; progress bar's estimate weighs by (torirsmaped/torirs_maped_build.h).\n");
    for( int i = 0; i < 4; i++ )
        fprintf(file, "%s = %u\n", k_run_times_keys[i], jobs->step_last_ms[i]);
    fprintf(file, "build_units = %u\n", jobs->build_last_units);
    fclose(file);
}

/** RUN moves to `step`: the step it leaves is timed, the new one's clock
 *  starts, and every window hears the status and a progress row. A step
 *  re-entered (PLAY decides twice) is not re-timed. */
static void
jobs_run_step_enter(
    struct ToriRSMapEd* maped,
    uint32_t step)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    int64_t const now = jobs_now_ms();
    int const leaving = jobs_run_step_index(jobs->run_step);

    if( jobs->run_step == step )
        return;
    if( leaving >= 0 && jobs->step_started_ms > 0 )
    {
        int64_t const took = now - jobs->step_started_ms;
        jobs->step_run_ms[leaving] = took > 0 ? (uint32_t)took : 1;
    }
    if( step == TORIRSMAPED_RUN_STEP_SAVE )
    {
        memset(jobs->step_run_ms, 0, sizeof(jobs->step_run_ms));
        jobs_run_times_load(jobs);
    }
    jobs->run_step = step;
    jobs->step_started_ms = now;
    jobs->progress_sent_ms = 0;
    /* The build's units are counted from this RUN's build, not the last
     * job's (its count stays until jobs_build_start resets it). */
    if( step == TORIRSMAPED_RUN_STEP_BUILD )
        jobs->build_unit_count = 0;
    if( step == TORIRSMAPED_RUN_STEP_SAVE )
        snprintf(jobs->progress_detail, sizeof(jobs->progress_detail), "saving the edited documents");
    else if( step == TORIRSMAPED_RUN_STEP_BUILD )
        snprintf(jobs->progress_detail, sizeof(jobs->progress_detail), "packs and client cache");
    else if( step == TORIRSMAPED_RUN_STEP_RELOAD )
        snprintf(jobs->progress_detail, sizeof(jobs->progress_detail), "reloading the live server");
    else
        jobs->progress_detail[0] = '\0';
    jobs_progress_broadcast(maped, 1);
}

static void
jobs_progress_broadcast(
    struct ToriRSMapEd* maped,
    int force)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;
    int const index = jobs_run_step_index(jobs->run_step);
    int64_t const now = jobs_now_ms();
    uint32_t weights[4];
    uint64_t total = 0;
    uint64_t done = 0;
    uint64_t later = 0;
    int64_t elapsed;
    double fraction;
    double time_fraction;
    uint32_t percent;
    uint32_t remaining;
    const char* detail;

    if( index < 0 || jobs->step_started_ms <= 0 )
        return;
    if( !force && now - jobs->progress_sent_ms < 250 )
        return;
    jobs->progress_sent_ms = now;
    elapsed = now - jobs->step_started_ms;
    if( elapsed < 0 )
        elapsed = 0;
    for( int i = 0; i < 4; i++ )
    {
        weights[i] = jobs->step_last_ms[i] ? jobs->step_last_ms[i] : k_run_default_ms[i];
        total += weights[i];
        if( i < index )
            done += weights[i];
        if( i > index )
            later += weights[i];
    }
    /* The step's clock against its last duration, never reaching the end
     * on its own: a step that runs long holds at 95 %. Without a history it
     * approaches 90 % asymptotically. */
    if( jobs->step_last_ms[index] )
        time_fraction = (double)elapsed / (double)jobs->step_last_ms[index];
    else
        time_fraction = (double)elapsed / ((double)elapsed + (double)k_run_default_ms[index]);
    if( time_fraction > 0.95 )
        time_fraction = 0.95;
    fraction = time_fraction;
    if( jobs->run_step == TORIRSMAPED_RUN_STEP_BUILD && jobs->build_last_units > 0 && jobs->building )
    {
        double const units = (double)jobs->build_unit_count / (double)jobs->build_last_units;
        if( units > fraction )
            fraction = units > 0.97 ? 0.97 : units;
    }
    percent = (uint32_t)(((double)done + fraction * (double)weights[index]) * 100.0 / (double)total);
    if( percent > 99 )
        percent = 99;
    if( jobs->step_last_ms[index] )
    {
        int64_t left = (int64_t)weights[index] - elapsed;
        remaining = (uint32_t)((left > 0 ? left : 0) + (int64_t)later);
    }
    else
        remaining = 0xFFFFFFFFu;
    detail = jobs->progress_detail;
    if( jobs->run_step == TORIRSMAPED_RUN_STEP_PLAY && jobs->play_message[0] )
        detail = jobs->play_message;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, jobs->run_step);
    ToriRSMapEd_PutU32(&payload, (uint32_t)index + 1);
    ToriRSMapEd_PutU32(&payload, 4);
    ToriRSMapEd_PutU32(&payload, percent);
    ToriRSMapEd_PutU32(&payload, (uint32_t)(fraction * 100.0));
    ToriRSMapEd_PutU32(&payload, (uint32_t)elapsed);
    ToriRSMapEd_PutU32(&payload, remaining);
    ToriRSMapEd_PutString(&payload, detail);
    jobs_broadcast(maped, TORIRSMAPED_FACT_RUN_PROGRESS, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/** Once per service pass: the bar keeps moving between the tools' lines. */
static void
jobs_progress_service(struct ToriRSMapEd* maped)
{
    if( maped->jobs->run_step != TORIRSMAPED_RUN_STEP_IDLE )
        jobs_progress_broadcast(maped, 0);
}


static void
jobs_run_finish(
    struct ToriRSMapEd* maped,
    enum EditorHost_Status status,
    uint32_t step,
    const char* message,
    const uint32_t* counts,
    const struct ToriRSMapEdBuf* restart_rows,
    uint32_t restart_count)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;

    if( status == EDITOR_HOST_OK )
        jobs_run_times_record(maped);
    jobs->run_step = TORIRSMAPED_RUN_STEP_IDLE;
    jobs->step_started_ms = 0;
    jobs->reload_waiting = 0;
    jobs->run_play_waiting = 0;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, (uint32_t)status);
    ToriRSMapEd_PutU32(&payload, step);
    ToriRSMapEd_PutString(&payload, message ? message : "");
    for( int i = 0; i < 4; i++ )
        ToriRSMapEd_PutU32(&payload, counts ? counts[i] : 0);
    ToriRSMapEd_PutU32(&payload, restart_count);
    if( restart_rows && ToriRSMapEd_BufAvailable(restart_rows) > 0 )
        ToriRSMapEd_BufWrite(
            &payload, ToriRSMapEd_BufPeek(restart_rows), ToriRSMapEd_BufAvailable(restart_rows));
    TORIRS_LOG("torirsmaped: RUN %s at %s%s%s\n",
        status == EDITOR_HOST_OK ? "done" : "stopped",
        ToriRSMapEd_RunStepName(step),
        message && message[0] ? ": " : "",
        message ? message : "");
    maped->lane_writes_own++;
    jobs_broadcast(maped, TORIRSMAPED_FACT_RUN_DONE, &payload);
    ToriRSMapEd_BufFree(&payload);
    jobs_live_broadcast_status(maped);
}

/** RUN's first step, inline: SAVE_ALL through the server's own save path. */
static enum EditorHost_Status
jobs_run_save(
    struct ToriRSMapEd* maped,
    int* out_saved,
    int* out_failed)
{
    struct Editor_DocRef* rows;
    int capacity;
    int saved = 0;
    int failed = 0;

    *out_saved = 0;
    *out_failed = 0;
    if( !maped->writable )
        return EDITOR_HOST_LOCKED;
    capacity = maped->doc.square_count + maped->doc.spawn_count + maped->doc.text_count + 1;
    rows = ToriMem_Alloc(ToriMem_Editor(), (size_t)capacity * sizeof(*rows));
    assert(rows);
    for( int i = 0; i < maped->doc.square_count; i++ )
    {
        struct Editor_DocHandle handle = { EDITOR_DOC_SQUARE, &maped->doc.squares[i], NULL, NULL };
        if( !handle.square->loaded
            || (!Editor_DocHandleDirty(&handle) && !maped->doc.square_created[i]) )
            continue;
        if( save_doc(maped, &handle, &rows[saved]) == EDITOR_HOST_OK )
            saved++;
        else
            failed++;
    }
    for( int i = 0; i < maped->doc.spawn_count; i++ )
    {
        struct Editor_DocHandle handle = { EDITOR_DOC_SPAWN, NULL, maped->doc.spawns[i], NULL };
        if( !handle.spawn->loaded || !handle.spawn->dirty )
            continue;
        if( save_doc(maped, &handle, &rows[saved]) == EDITOR_HOST_OK )
            saved++;
        else
            failed++;
    }
    for( int i = 0; i < maped->doc.text_count; i++ )
    {
        struct Editor_DocHandle handle = { maped->doc.texts[i]->kind, NULL, NULL, maped->doc.texts[i] };
        if( !handle.text->loaded || !handle.text->dirty )
            continue;
        if( save_doc(maped, &handle, &rows[saved]) == EDITOR_HOST_OK )
            saved++;
        else
            failed++;
    }
    /* What did land is broadcast either way: those mirrors are clean now. */
    broadcast_saved(maped, rows, saved);
    ToriMem_Free(ToriMem_Editor(), rows);
    *out_saved = saved;
    *out_failed = failed;
    return failed ? EDITOR_HOST_IO_ERROR : EDITOR_HOST_OK;
}

static void
handle_run(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t length)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    enum EditorHost_Status status;
    int saved;
    int failed;
    char message[1400];

    if( length != 0 )
    {
        session->alive = 0;
        return;
    }
    if( jobs->building || jobs->run_step != TORIRSMAPED_RUN_STEP_IDLE )
    {
        /* Not this run's to finish: the one in flight keeps its step. */
        struct ToriRSMapEdBuf payload;
        memset(&payload, 0, sizeof(payload));
        ToriRSMapEd_PutU32(&payload, (uint32_t)EDITOR_HOST_REFUSED);
        ToriRSMapEd_PutU32(&payload, TORIRSMAPED_RUN_STEP_IDLE);
        ToriRSMapEd_PutString(&payload, "a build or a RUN is already in flight");
        for( int i = 0; i < 5; i++ )
            ToriRSMapEd_PutU32(&payload, 0);
        send_buf(session, TORIRSMAPED_FACT_RUN_DONE, &payload);
        ToriRSMapEd_BufFree(&payload);
        return;
    }

    jobs_run_step_enter(maped, TORIRSMAPED_RUN_STEP_SAVE);
    status = jobs_run_save(maped, &saved, &failed);
    if( saved > 0 )
        catalog_atlas_changed(maped, 1);
    if( status != EDITOR_HOST_OK )
    {
        if( status == EDITOR_HOST_LOCKED )
            snprintf(message, sizeof(message),
                "read-only: %s -- close it, or run this window against the daemon (nothing was saved, "
                "built or launched)",
                maped->read_only_reason[0] ? maped->read_only_reason : "another server holds the tree's lock");
        else
            snprintf(message, sizeof(message), "%d document(s) did not save", failed);
        jobs_run_finish(maped, status, TORIRSMAPED_RUN_STEP_SAVE, message, NULL, NULL, 0);
        return;
    }
    jobs_run_step_enter(maped, TORIRSMAPED_RUN_STEP_BUILD);
    status = jobs_build_start(maped, TORIRSMAPED_BUILD_RUN, "", 1);
    if( status != EDITOR_HOST_OK )
    {
        jobs_build_send_done(maped, TORIRSMAPED_BUILD_RUN, status, 0);
        jobs_run_finish(maped, status, TORIRSMAPED_RUN_STEP_BUILD,
            jobs->have_runner ? "the build did not start" : "this server has no build runner", NULL, NULL, 0);
        return;
    }
    jobs_live_broadcast_status(maped);
    jobs_build_service(maped);
}
