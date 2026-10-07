#ifndef PLATFORM_LAUNCH_SESSIONS_H
#define PLATFORM_LAUNCH_SESSIONS_H

/*
 * The launch service of the client's embedded IO server (raid seam37,
 * docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-06j.md, DESIGN (1) and (2)).
 *
 * A leader client that runs a multi-account flow (a raid party) asks its own
 * embedded IO server to start the other clients. The service keeps a table of
 * LAUNCH SESSIONS: one per party, its seats, their pids, a secret token, the
 * leader's heartbeat, a mailbox per seat and the last status each seat posted.
 * It answers seven verbs as plain request/answer calls (the caller is the
 * plugin channel's `launch/<verb>` IO item; this module knows nothing of IO
 * items, HTTP or sockets):
 *
 *   open       leader registers (pid, saves, port, size) -> session + token
 *   spawn      start seats 2..size with the LEADER'S binary and manifest
 *   command    queue `play {json}` / `stop` / `cheat <line>` / `quit` for a
 *              seat or all seats
 *   mail       a member drains its mailbox and posts its status line
 *   status     every seat's pid, alive, exit code and last status
 *   heartbeat  the leader is still here (status/spawn/command count too)
 *   close      clean end: `quit` to every seat, the grace, then the kill
 *
 * Bodies are `key=value` lines. An answer's first line is `ok`,
 * `error: <reason>` or `unsupported: <reason>`; `ok` is followed by more
 * `key=value` lines (a seat's status is one line of space-separated pairs
 * whose LAST pair, `status=`, runs to the end of the line).
 *
 * SPAWNED CLIENTS are always the host's own binary (the config's
 * binary_path, argv[0] resolved absolute by LaunchSessions_OwnBinaryPath)
 * with the host's manifest, WITHOUT the embedded options: no
 * TORIRS_EMBED_PARTY_LISTEN/_SIZE, no TORIRS_IO_SERVER, no TORIRS_MAX_FRAMES;
 * each joins the leader's embedded game server with TORIRS_EMBED_PARTY_JOIN.
 * Nothing in a request is ever executed: a request names accounts, scripts and
 * env, never a program.
 *
 * SMART MANAGEMENT: LaunchSessions_Reap, called by the host once a second,
 * records every member that exited, and ends a session whose leader pid is
 * gone or whose heartbeat is older than heartbeat_timeout_ms (SIGTERM to its
 * process group and every seat, SIGKILL after kill_grace_ms). Every session's
 * pids are in <launch_directory>/<session>.pids while it lives so a later
 * `./launch stop` can reap what a crashed host left. LaunchSessions_Free
 * (the host's own exit) kills every session's members.
 *
 * PLATFORMS: macOS and Linux spawn with posix_spawn into a per-session process
 * group. Windows compiles the table and a CreateProcess spawner but answers
 * `unsupported: the party link is not available on this platform` until the
 * embedded party link runs there (ToriRSServer_EmbedPartyListen aborts on
 * Windows today). Web, Android and iOS compile to the constant
 * `unsupported: desktop only` with no spawning code at all.
 */

#include <stdint.h>

enum
{
    /* torirs_server_embed.h TORIRSSERVER_EMBED_CLIENT_MAX: a party is the
     * leader (seat 1) and up to three members. */
    LAUNCH_SESSIONS_PARTY_MAX = 4,
    LAUNCH_SESSIONS_SESSION_MAX = 8,
    LAUNCH_SESSIONS_MAILBOX_MAX = 16,
    LAUNCH_SESSIONS_SEAT_ENV_MAX = 16,
    LAUNCH_SESSIONS_LINE_MAX = 2048,
    LAUNCH_SESSIONS_PATH_MAX = 1024,
    LAUNCH_SESSIONS_BODY_MAX = 64 * 1024,
};

/* What a spawn needs from the operating system. The host passes the real one
 * (LaunchSessions_SystemSpawner); the unit test wraps it to record argv/env and
 * to run `sleep` in place of a client. */
struct LaunchSessions_Spawner
{
    void* context;
    /* Start `argv[0]` with exactly `argv` and `environment` (NULL-terminated),
     * stdin from the null device, stdout and stderr to `log_path`, in process
     * group `process_group` (0: a new group led by the child). Returns 0 and
     * fills *out_pid and *out_process_group, or an errno value. */
    int (*spawn)(
        void* context,
        char* const* argv,
        char* const* environment,
        char const* log_path,
        int process_group,
        int* out_pid,
        int* out_process_group);
    /* kill(pid, signal_number); 0 or an errno value (ESRCH: gone). */
    int (*signal_process)(void* context, int pid, int signal_number);
    /* killpg(process_group, signal_number); 0 or an errno value. */
    int (*signal_group)(void* context, int process_group, int signal_number);
    /* 1 while `pid` exists (kill(pid, 0) succeeds or answers EPERM). */
    int (*process_alive)(void* context, int pid);
    /* Reap a child: 1 when it has ended (*out_exit_code its code, or -1 and
     * *out_signal the signal that ended it), 0 while it runs. `block` waits
     * for the end instead of answering 0. */
    int (*reap_child)(void* context, int pid, int block, int* out_exit_code, int* out_signal);
};

struct LaunchSessions_Config
{
    /* Absolute path of the host's own binary: every member runs it. */
    char const* binary_path;
    /* The host's manifest (`--manifest`), or NULL when it was started without
     * one. */
    char const* manifest_path;
    /* Where the pids files and the members' default run directories go
     * (build/launch). Created when the first session opens. */
    char const* launch_directory;
    struct LaunchSessions_Spawner const* spawner;
    /* A monotonic clock in milliseconds (LaunchSessions_MonotonicMs). */
    int64_t (*clock_ms)(void* context);
    void* clock_context;
    /* The env a member starts from (NULL-terminated), or NULL for the host's
     * own environment, read at each spawn. */
    char* const* base_environment;
    /* 0 takes the default: 10000, 5000, 5000. */
    int heartbeat_timeout_ms;
    int quit_grace_ms;
    int kill_grace_ms;
};

struct LaunchSessions;

/* Every platform gets a table. On web, Android and iOS the config is not read
 * (pass NULL) and every answer is `unsupported: desktop only`; on Windows,
 * for now, `unsupported: the party link is not available on this platform`.
 * Where the service runs, binary_path, launch_directory, spawner and clock_ms
 * are required. */
struct LaunchSessions* LaunchSessions_New(struct LaunchSessions_Config const* config);

/* Kills every live session's members (SIGTERM, up to a second, SIGKILL),
 * reaps them, removes the pids files and frees the table. Accepts NULL. */
void LaunchSessions_Free(struct LaunchSessions* table);

/* The platform's refusal, or NULL where the service runs. */
char const* LaunchSessions_UnsupportedReason(void);

/*
 * Answers one request. `verb` is the part after `launch/`; `body` holds
 * `body_size` bytes of `key=value` lines (not NUL-terminated; NULL when
 * body_size is 0); `peer` is "local" for the in-process plugin channel, or
 * the caller's numeric address (only loopback is served). Returns a malloc'd
 * NUL-terminated answer and its length in *out_size; the caller frees it.
 */
char* LaunchSessions_Answer(
    struct LaunchSessions* table,
    char const* verb,
    char const* body,
    int body_size,
    char const* peer,
    int* out_size);

/* The host's once-a-second pass (see the banner). Returns the number of
 * sessions that still hold a live member. */
int LaunchSessions_Reap(struct LaunchSessions* table);

/* The real spawner for this platform, or NULL where there is none. */
struct LaunchSessions_Spawner const* LaunchSessions_SystemSpawner(void);

/* A monotonic clock in milliseconds (context unused). */
int64_t LaunchSessions_MonotonicMs(void* context);

/* The absolute path of the running binary (from the OS, else argv0 resolved
 * against the working directory) into `out`. 0 on success, -1 when it cannot
 * be found. */
int LaunchSessions_OwnBinaryPath(char const* argv0, char* out, int out_size);

#endif
