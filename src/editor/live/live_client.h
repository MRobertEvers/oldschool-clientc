#ifndef SRC_EDITOR_LIVE_LIVE_CLIENT_H
#define SRC_EDITOR_LIVE_LIVE_CLIENT_H

/*
 * The client half of BUILD, RUN and the live link (torirsmaped/
 * torirs_maped_build.h and torirs_maped_live.h define the frames).
 *
 * A window keeps one Editor_LiveModel: what the editor server last said about
 * the live lane (FACT_LIVE_STATUS), the followed player's vars
 * (FACT_LIVE_VARS), the last RUN's outcome (FACT_RUN_DONE), and a log of
 * every build and live line. Every connection hears every one of those facts,
 * so every window's Live panel shows the same run whoever pressed RUN.
 *
 * The model is fed frames and never reads a socket: an Editor_LiveTransport
 * carries the requests up and hands the live-range facts down. In the client
 * it is bound to the editor host (live_host.c); a test binds it to a raw
 * session on an embedded server.
 */

#include <stddef.h>
#include <stdint.h>

#include "torirsmaped/torirs_maped_buf.h"

enum
{
    EDITOR_LIVE_LOG_LINES = 256,
    EDITOR_LIVE_LOG_LINE_MAX = 192,
    EDITOR_LIVE_STALE_MAX = 32,
    EDITOR_LIVE_VARS_MAX = 64,
    EDITOR_LIVE_RESTART_MAX = 16,
    EDITOR_LIVE_PLAYERS_MAX = 16,
};

struct Editor_LivePlayer
{
    char name[64];
    int32_t x;
    int32_t z;
    int32_t level;
};

struct Editor_LiveVar
{
    uint32_t varp;
    int32_t value;
};

struct Editor_LiveModel
{
    /* ---- FACT_LIVE_STATUS ---- */
    /** enum ToriRSMapEdLiveState. */
    uint32_t state;
    int launched;
    char address[256];
    char server_name[128];
    uint32_t players;
    uint32_t tick;
    char script_stamp[128];
    char servpack_stamp[128];
    char stale[EDITOR_LIVE_STALE_MAX][192];
    int stale_count;
    char message[1024];
    /** enum ToriRSMapEdRunStep; IDLE when no RUN is in flight. */
    uint32_t run_step;
    uint32_t pending_squares;
    uint32_t pending_spawns;
    uint32_t pending_archives;
    /** Bumps on every status. */
    int status_serial;

    /* ---- builds ---- */
    /** A FACT_BUILD_LINE arrived and its FACT_BUILD_DONE has not. */
    int building;
    uint32_t build_what;
    uint32_t last_build_what;
    uint32_t last_build_status;
    uint32_t last_build_units;
    uint32_t last_build_archives;
    int build_serial;

    /* ---- FACT_RUN_DONE ---- */
    int run_serial;
    uint32_t run_status;
    uint32_t run_last_step;
    char run_message[1024];
    uint32_t run_counts[4];
    char restart[EDITOR_LIVE_RESTART_MAX][192];
    int restart_count;

    /* ---- FACT_RUN_PROGRESS: where the RUN in flight is (torirs_maped_build.h) ---- */
    uint32_t progress_step;
    /** 1-based step index and the step count (save, build, reload, play). */
    uint32_t progress_index;
    uint32_t progress_count;
    /** Of the whole RUN, and of the current step, 0..100. */
    uint32_t progress_percent;
    uint32_t progress_step_percent;
    uint32_t progress_elapsed_ms;
    /** 0xFFFFFFFF when no earlier RUN gives an estimate. */
    uint32_t progress_remaining_ms;
    char progress_detail[160];
    /** Bumps on every FACT_RUN_PROGRESS. */
    int progress_serial;

    /* ---- FACT_LIVE_PLAY: the lane's play client and the server's players ---- */
    /** enum ToriRSMapEdPlayState. */
    uint32_t play_state;
    int play_browser;
    char play_account[32];
    int32_t play_pid;
    int32_t play_exit;
    char play_message[256];
    /** Absolute tile, -1 until the server reported it. */
    int32_t play_x;
    int32_t play_z;
    int32_t play_level;
    uint32_t play_starts;
    /** Where a browser plays the lane ("" when it serves no web page). */
    char play_web_url[1024];
    /** The server reports its players (CTL_PLAYERS). */
    int players_known;
    /** Every login the server took (a relog adds one). */
    uint32_t logins;
    struct Editor_LivePlayer player_rows[EDITOR_LIVE_PLAYERS_MAX];
    int player_row_count;
    /** Bumps on every FACT_LIVE_PLAY. */
    int play_serial;

    /* ---- FACT_LIVE_VARS ---- */
    char vars_player[64];
    struct Editor_LiveVar vars[EDITOR_LIVE_VARS_MAX];
    int var_count;
    int vars_serial;

    /* ---- the log: build lines, live lines, outcomes; newest at head-1 ---- */
    char log[EDITOR_LIVE_LOG_LINES][EDITOR_LIVE_LOG_LINE_MAX];
    /** Per row: the build `what` a FACT_BUILD_LINE row came from, that `what`
     *  | EDITOR_LIVE_LOG_DONE for its FACT_BUILD_DONE row, 0 for every other
     *  row (Editor_LiveLogWhat; the Problems panel's line stream). */
    uint32_t log_what[EDITOR_LIVE_LOG_LINES];
    int log_head;
    int log_count;
    int log_serial;
};

void
Editor_LiveModelInit(struct Editor_LiveModel* model);

/** 1 when `type` is one of the live-editor facts (173..190). */
int
Editor_LiveIsFact(uint32_t type);

/**
 * Fold one fact into the model. Returns 1 when it was a live-editor fact and
 * applied, 0 when `type` is not one (the caller's frame), -1 when it is one
 * but malformed (dropped; the model is unchanged except the log says so).
 */
int
Editor_LiveModelApply(
    struct Editor_LiveModel* model,
    uint32_t type,
    const uint8_t* body,
    uint32_t length);

/** Editor_LiveModel.log_what's mark on a FACT_BUILD_DONE row. */
#define EDITOR_LIVE_LOG_DONE 0x80000000u

/** The `index`th newest log line's build mark (Editor_LiveModel.log_what);
 *  0 past the end. */
uint32_t
Editor_LiveLogWhat(
    const struct Editor_LiveModel* model,
    int index);

/** The `index`th newest log line (0 newest), or NULL past the end. */
const char*
Editor_LiveLogLine(
    const struct Editor_LiveModel* model,
    int index);

/** The value the model last heard for `varp`, 1 when known. */
int
Editor_LiveVarValue(
    const struct Editor_LiveModel* model,
    uint32_t varp,
    int32_t* out_value);

/* ---- requests ----------------------------------------------------------------
 *
 * Each appends one request's PAYLOAD to `out` (the frame header is the
 * transport's) and returns its frame type. */

uint32_t
Editor_LiveEncodeBuild(
    struct ToriRSMapEdBuf* out,
    uint32_t what);

/** BUILD of a quest kind (TORIRSMAPED_BUILD_QUEST_SCAFFOLD / _GATE) for
 *  `quest`, which must be ToriRSMapEd_BuildQuestValid. */
uint32_t
Editor_LiveEncodeBuildQuest(
    struct ToriRSMapEdBuf* out,
    uint32_t what,
    const char* quest);

/** BUILD of a kind that carries an argument (ToriRSMapEd_BuildWhatTakesArg:
 *  the quest kinds), which must be
 *  ToriRSMapEd_BuildArgValid. */
uint32_t
Editor_LiveEncodeBuildArg(
    struct ToriRSMapEdBuf* out,
    uint32_t what,
    const char* arg);

uint32_t
Editor_LiveEncodeRun(struct ToriRSMapEdBuf* out);

/** `launch` 1 starts the lane's servers (address ignored); 0 dials
 *  "host:port". `ticket` is the game server's gate ticket, "" for none. */
uint32_t
Editor_LiveEncodeAttach(
    struct ToriRSMapEdBuf* out,
    int launch,
    const char* address,
    const char* ticket);

uint32_t
Editor_LiveEncodeDetach(struct ToriRSMapEdBuf* out);

/** A :: debugproc for `player` on the live server. A leading "::" on `line`
 *  is dropped: the control link takes the command itself. */
uint32_t
Editor_LiveEncodeCheat(
    struct ToriRSMapEdBuf* out,
    const char* player,
    const char* line);

/** LIVE_PLAY: `action` is enum ToriRSMapEdPlayAction. */
uint32_t
Editor_LiveEncodePlay(
    struct ToriRSMapEdBuf* out,
    uint32_t action);

/** The RUN in flight for a bar's caption: "build 2/4: repacked npc (12 s,
 *  ~9 s left)". "" when no RUN is in flight. */
void
Editor_LiveProgressLine(
    const struct Editor_LiveModel* model,
    char* out,
    size_t out_size);

/** How the last RUN ended, for the one sentence that says so
 *  (Editor_LiveRunOutcomeLine). */
enum Editor_LiveRunOutcome
{
    /** No FACT_RUN_DONE has arrived. */
    EDITOR_LIVE_RUN_NONE = 0,
    /** Every step succeeded and the reload took everything. */
    EDITOR_LIVE_RUN_DONE,
    /** Every step succeeded, but the reload listed what it cannot swap into
     *  a running server (FACT_RUN_DONE's restart rows): the new content does
     *  not run until the lane's server starts again. */
    EDITOR_LIVE_RUN_RESTART,
    /** A step failed; the steps after it did not run. */
    EDITOR_LIVE_RUN_STOPPED,
};

/** Bytes Editor_LiveRunOutcomeLine fills at most: one console line. */
#define EDITOR_LIVE_OUTCOME_MAX 160

/**
 * The last RUN's outcome as ONE sentence for the Output console and the
 * status bar (2026-11-09: only the Live panel said it, and a RUN that needed
 * a restart read "RUN done" while the new content did not run):
 *
 *   "RUN done in 31 s: 3 scripts, 2 config groups, 1 square reloaded; started
 *    the play client (it logs in as maped1)"
 *   "RUN done, but the lane's server must restart for: config varp records.
 *    Press Restart lane (Live panel, or File menu)." (a lane this editor
 *    launched; for a server it only dialled: "... the game server must
 *    restart for: ... Restart it where it runs, then Attach again.")
 *   "RUN stopped: the packs build failed: fenwick.rs2:7: unknown variable '%'"
 *   "RUN refused: a build or a RUN is already in flight" (STOPPED with
 *    run_last_step IDLE: no step of it ran)
 *
 * `seconds` is how long the RUN took, negative when nobody timed it.
 * `reason` is the failed build's first diagnostic, NULL or "" when there is
 * none: it replaces the server's own pick of a line (its message after the
 * first ':'), which is the build's LAST line that reads as an error and is
 * often a summary ("1 error"). `out` holds at least EDITOR_LIVE_OUTCOME_MAX
 * bytes; "" for EDITOR_LIVE_RUN_NONE.
 */
enum Editor_LiveRunOutcome
Editor_LiveRunOutcomeLine(
    const struct Editor_LiveModel* model,
    int seconds,
    const char* reason,
    char* out,
    size_t out_size);

/** The play client's line for the Live panel: "play: not started",
 *  "play: logged in as maped43620 at m50_50 (3222,3218)", ... */
void
Editor_LivePlayLine(
    const struct Editor_LiveModel* model,
    char* out,
    size_t out_size);

/** Follow `player`'s varps (n = 0 unsubscribes). */
uint32_t
Editor_LiveEncodeSubscribe(
    struct ToriRSMapEdBuf* out,
    const char* player,
    const uint32_t* varps,
    int n);

/** Parse "1, 2,281" into varp ids. Returns the count (at most `max`), or -1
 *  for text that is not a list of ids. */
int
Editor_LiveParseVarps(
    const char* text,
    uint32_t* out,
    int max);

/** The ::tele argument for an absolute tile: "level_mx_mz_lx_lz". */
void
Editor_LiveTeleCoord(
    int abs_x,
    int abs_z,
    int level,
    char* out,
    size_t out_size);

/* ---- the transport -------------------------------------------------------------- */

struct Editor_LiveTransport
{
    void* user;
    /** Send one request frame. 1 sent, 0 the connection is gone. */
    int (*send)(void* user, uint32_t type, const uint8_t* payload, uint32_t length);
    /** Take the next live-editor fact that arrived, or return 0. The body is
     *  the transport's until `release`. */
    int (*take)(void* user, uint32_t* out_type, uint8_t** out_body, uint32_t* out_length);
    void (*release)(void* user, uint8_t* body);
    /** Publish a shared-state field in this connection's Client
     *  (STATE_LIVE_FOCUS). NULL: the panel uses Editor_UiPublish. */
    int (*publish)(void* user, uint32_t key, const int32_t* values, int count);
};

/** Encode-and-send helper: `out` is cleared, filled by `type`'s encoder by the
 *  caller, then sent. Returns the transport's answer. */
int
Editor_LiveSend(
    const struct Editor_LiveTransport* transport,
    uint32_t type,
    struct ToriRSMapEdBuf* payload);

/** Drain every pending fact into `model`. Returns how many applied. */
int
Editor_LiveDrain(
    const struct Editor_LiveTransport* transport,
    struct Editor_LiveModel* model);

#endif
