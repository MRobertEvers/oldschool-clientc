/*
 * The launch service's session table (platform/launch_sessions.c), driven
 * without a client: `make -C src test-launch-sessions`.
 *
 * A FAKE spawner records each member's argv and env and starts `sleep 30` in
 * its place (into the process group the table asked for), and a FAKE clock
 * lets the heartbeat and the graces pass without waiting. The leader is a
 * `sleep 30` too, so "the leader is gone" is a real pid that really ends.
 * The last case is REAL end to end: the system spawner, the monotonic clock
 * and a client binary that is a script exec'ing `sleep 30`; the fake leader
 * is SIGKILLed and the member must be gone within 15 s.
 *
 * Proves: open/spawn/status; the env a member gets (the embedded options
 * stripped, the party join and the watchdog set); one process group per
 * session; a leader pid that exits ends the group; a heartbeat that stops does
 * the same; a member exit is recorded and the seat respawns; close quits, then
 * kills; a member that ignores SIGTERM is SIGKILLed after the grace; the host's
 * own exit (LaunchSessions_Free) kills every member; the token and the seat
 * token are required; a non-loopback peer is refused.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "platform/launch_sessions.h"

#include <errno.h>
#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_fails = 0;
static int g_checks = 0;

static void
check(char const* what, int ok)
{
    g_checks++;
    printf("%-70s %s\n", what, ok ? "ok" : "FAIL");
    if( !ok )
        g_fails++;
}

/* ---- the fake spawner ---------------------------------------------------- */

enum
{
    FAKE_RECORD_MAX = 16,
};

struct FakeSpawn
{
    int pid;
    int process_group;
    char argv[2048];
    char environment[16384];
};

struct Fake
{
    struct LaunchSessions_Spawner ops;
    struct LaunchSessions_Spawner const* system;
    int ignore_term_next;
    int count;
    struct FakeSpawn spawns[FAKE_RECORD_MAX];
    int64_t now_ms;
};

static struct Fake g_fake;

static void
join_into(char* out, size_t out_size, char* const* items)
{
    size_t used = 0;

    out[0] = '\0';
    for( int i = 0; items[i]; i++ )
        used += (size_t)snprintf(out + used, out_size > used ? out_size - used : 0, "%s\n",
                                 items[i]);
}

static int
fake_spawn(void* context, char* const* argv, char* const* environment, char const* log_path,
           int process_group, int* out_pid, int* out_process_group)
{
    struct Fake* fake = context;
    static char* const sleep_argv[] = { "/bin/sleep", "30", NULL };
    static char* const stubborn_argv[] = { "/bin/sh", "-c", "trap '' TERM; /bin/sleep 30", NULL };
    char* const minimal_environment[] = { "PATH=/bin:/usr/bin", NULL };
    struct FakeSpawn* record;
    int result;

    result = fake->system->spawn(fake->system->context,
                                 fake->ignore_term_next ? stubborn_argv : sleep_argv,
                                 minimal_environment, log_path, process_group, out_pid,
                                 out_process_group);
    fake->ignore_term_next = 0;
    if( result != 0 )
        return result;
    record = &fake->spawns[fake->count++ % FAKE_RECORD_MAX];
    record->pid = *out_pid;
    record->process_group = *out_process_group;
    join_into(record->argv, sizeof(record->argv), argv);
    join_into(record->environment, sizeof(record->environment), environment);
    return 0;
}

static int
fake_signal_process(void* context, int pid, int signal_number)
{
    struct Fake* fake = context;
    return fake->system->signal_process(fake->system->context, pid, signal_number);
}

static int
fake_signal_group(void* context, int process_group, int signal_number)
{
    struct Fake* fake = context;
    return fake->system->signal_group(fake->system->context, process_group, signal_number);
}

static int
fake_process_alive(void* context, int pid)
{
    struct Fake* fake = context;
    return fake->system->process_alive(fake->system->context, pid);
}

static int
fake_reap_child(void* context, int pid, int block, int* out_exit_code, int* out_signal)
{
    struct Fake* fake = context;
    return fake->system->reap_child(fake->system->context, pid, block, out_exit_code,
                                    out_signal);
}

static int64_t
fake_clock(void* context)
{
    return ((struct Fake*)context)->now_ms;
}

/* ---- helpers ------------------------------------------------------------ */

static char*
ask(struct LaunchSessions* table, char const* verb, char const* body)
{
    int size = -1;
    char* answer = LaunchSessions_Answer(table, verb, body, (int)strlen(body), "local", &size);
    if( !answer || size != (int)strlen(answer) )
    {
        printf("launch/%s: answer size %d does not match its text\n", verb, size);
        exit(2);
    }
    return answer;
}

static int
starts_with(char const* text, char const* prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int
contains(char const* text, char const* needle)
{
    return strstr(text, needle) != NULL;
}

/* The value after `key` up to the end of its line or the next space. */
static void
value_of(char const* text, char const* key, char* out, size_t out_size)
{
    char const* at = strstr(text, key);
    size_t length = 0;

    out[0] = '\0';
    if( !at )
        return;
    at += strlen(key);
    while( at[length] && at[length] != '\n' && at[length] != ' ' && length + 1 < out_size )
        length++;
    memcpy(out, at, length);
    out[length] = '\0';
}

/* One line of `text` that starts with `prefix`, into `out`. */
static void
line_of(char const* text, char const* prefix, char* out, size_t out_size)
{
    char const* at = text;
    out[0] = '\0';
    while( at && *at )
    {
        if( starts_with(at, prefix) )
        {
            size_t length = strcspn(at, "\n");
            if( length >= out_size )
                length = out_size - 1;
            memcpy(out, at, length);
            out[length] = '\0';
            return;
        }
        at = strchr(at, '\n');
        if( at )
            at++;
    }
}

static int
start_sleep(void)
{
    char* const argv[] = { "/bin/sleep", "30", NULL };
    char* const environment[] = { "PATH=/bin:/usr/bin", NULL };
    int pid = 0;
    int group = 0;
    int result = LaunchSessions_SystemSpawner()->spawn(NULL, argv, environment, "/dev/null", 0,
                                                       &pid, &group);
    if( result != 0 )
    {
        printf("cannot start the fake leader: %s\n", strerror(result));
        exit(2);
    }
    return pid;
}

static void
kill_leader(int pid)
{
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0); /* the test is its parent: no zombie answers kill(pid, 0) */
}

static void
sleep_ms(int milliseconds)
{
    struct timespec wait = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };
    nanosleep(&wait, NULL);
}

static int
gone(int pid)
{
    return kill(pid, 0) != 0 && errno == ESRCH;
}

/* Reap (no clock movement) until the session's status says `state`, up to
 * `limit_ms` of wall time. */
static int
reap_until(struct LaunchSessions* table, char const* status_body, char const* state, int limit_ms)
{
    char want[64];
    snprintf(want, sizeof(want), "state=%s ", state);
    for( int waited = 0; waited <= limit_ms; waited += 20 )
    {
        char* answer;
        int found;
        LaunchSessions_Reap(table);
        answer = ask(table, "status", status_body);
        found = contains(answer, want);
        free(answer);
        if( found )
            return 1;
        sleep_ms(20);
    }
    return 0;
}

static char g_root[512];
static char g_launch_directory[600];
static char g_saves[600];

static char* g_base_environment[] = {
    "PATH=/bin:/usr/bin",
    "TORIRS_EMBED_PARTY_LISTEN=43600",
    "TORIRS_EMBED_PARTY_SIZE=3",
    "TORIRS_EMBED_PARTY_WAIT_S=60",
    "TORIRS_EMBED_CLOCK_MS=20",
    "TORIRS_MAX_FRAMES=90000",
    "TORIRS_QUEST_SCRIPT=/leader/script/_play_bloat.lua",
    "TORIRS_CONTENT_TEST=/leader/p1",
    "TORIRS_IO_SERVER=http://127.0.0.1:8088",
    "TORIRS_LAUNCH_LEADER_PID=1",
    "TORIRS_PLUGIN_MANIFEST=plugins/quest_driver.ini",
    "SDL_VIDEODRIVER=dummy",
    NULL,
};

static struct LaunchSessions*
fake_table(void)
{
    struct LaunchSessions_Config config;

    memset(&g_fake.ops, 0, sizeof(g_fake.ops));
    g_fake.ops.context = &g_fake;
    g_fake.ops.spawn = fake_spawn;
    g_fake.ops.signal_process = fake_signal_process;
    g_fake.ops.signal_group = fake_signal_group;
    g_fake.ops.process_alive = fake_process_alive;
    g_fake.ops.reap_child = fake_reap_child;
    g_fake.system = LaunchSessions_SystemSpawner();
    memset(&config, 0, sizeof(config));
    config.binary_path = "/opt/torirs/build/torirs";
    config.manifest_path = "manifests/osrs239-scripts.ini";
    config.launch_directory = g_launch_directory;
    config.spawner = &g_fake.ops;
    config.clock_ms = fake_clock;
    config.clock_context = &g_fake;
    config.base_environment = g_base_environment;
    return LaunchSessions_New(&config);
}

/* Opens a party of `size` led by `leader`; the session/token pair goes into
 * `auth` as the two request lines every leader verb starts with. */
static void
open_session(struct LaunchSessions* table, int leader, int size, char* auth, size_t auth_size)
{
    char body[1024];
    char session[64];
    char token[64];
    char* answer;

    snprintf(body, sizeof(body), "pid=%d\nport=43600\nsize=%d\nsaves=%s\n", leader, size, g_saves);
    answer = ask(table, "open", body);
    value_of(answer, "session=", session, sizeof(session));
    value_of(answer, "token=", token, sizeof(token));
    if( !starts_with(answer, "ok\n") || !session[0] || strlen(token) != 32 )
    {
        printf("launch/open failed: %s\n", answer);
        exit(2);
    }
    free(answer);
    snprintf(auth, auth_size, "session=%s\ntoken=%s\n", session, token);
}

static int
seat_pid(struct LaunchSessions* table, char const* auth, int seat)
{
    char prefix[32];
    char line[1024];
    char pid[32];
    char* answer = ask(table, "status", auth);

    snprintf(prefix, sizeof(prefix), "seat=%d ", seat);
    line_of(answer, prefix, line, sizeof(line));
    value_of(line, "pid=", pid, sizeof(pid));
    free(answer);
    return atoi(pid);
}

static void
seat_line(struct LaunchSessions* table, char const* auth, int seat, char* out, size_t out_size)
{
    char prefix[32];
    char* answer = ask(table, "status", auth);

    snprintf(prefix, sizeof(prefix), "seat=%d ", seat);
    line_of(answer, prefix, out, out_size);
    free(answer);
}

static char*
spawn_two(struct LaunchSessions* table, char const* auth)
{
    char body[2048];

    snprintf(body, sizeof(body),
             "%sseat=2\naccount=bloat_p2\npassword=quest\nautoplay={\"id\":\"_play_bloat\","
             "\"role\":2,\"size\":3}\nseat=3\naccount=bloat_p3\npassword=quest\n"
             "env=TORIRS_EMBED_PARTY_TRACE=1\n",
             auth);
    return ask(table, "spawn", body);
}

static void
case_open_spawn_status(void)
{
    struct LaunchSessions* table = fake_table();
    int leader = start_sleep();
    char auth[256];
    char body[1024];
    char line[1024];
    char* answer;
    struct FakeSpawn const* seat2;
    struct FakeSpawn const* seat3;
    int size = 0;

    printf("-- open, spawn, status, env, tokens, peers\n");
    answer = LaunchSessions_Answer(table, "open", "pid=1\n", 6, "10.0.0.5", &size);
    check("a non-loopback peer is refused", starts_with(answer, "error: ") &&
                                                contains(answer, "this machine only"));
    free(answer);
    answer = ask(table, "open", "pid=1\nport=43600\nsize=3\nbinary=/bin/sh\n");
    check("open refuses binary= (only the host's own binary runs)",
          starts_with(answer, "error: ") && contains(answer, "binary="));
    free(answer);
    answer = ask(table, "open", "pid=999999\nport=43600\nsize=3\nsaves=/tmp\n");
    check("open refuses a leader pid that is not running", starts_with(answer, "error: "));
    free(answer);
    snprintf(body, sizeof(body), "pid=%d\nport=43600\nsize=5\nsaves=%s\n", leader, g_saves);
    answer = ask(table, "open", body);
    check("open refuses a party larger than the embed server's 4", starts_with(answer, "error: "));
    free(answer);

    open_session(table, leader, 3, auth, sizeof(auth));
    check("open answers a session and a 32-hex token", 1);
    snprintf(body, sizeof(body), "%.*s", (int)(strchr(auth, '\n') - auth + 1), auth);
    answer = ask(table, "status", body);
    check("status without a token is refused",
          starts_with(answer, "error: the session token is wrong or missing"));
    free(answer);
    snprintf(body, sizeof(body), "%.*stoken=00000000000000000000000000000000\n",
             (int)(strchr(auth, '\n') - auth + 1), auth);
    answer = ask(table, "status", body);
    check("status with a wrong token is refused",
          starts_with(answer, "error: the session token is wrong"));
    free(answer);
    answer = ask(table, "status", auth);
    check("status with the token: ok, state=open, seats 2 and 3 never spawned",
          starts_with(answer, "ok\n") && contains(answer, "state=open ") &&
              contains(answer, "seat=2 pid=0 alive=0") && contains(answer, "seat=3 pid=0 alive=0"));
    free(answer);

    snprintf(body, sizeof(body), "%sseat=2\naccount=a\npassword=b\nenv=DYLD_INSERT_LIBRARIES=x\n",
             auth);
    answer = ask(table, "spawn", body);
    check("spawn refuses a loader env key", starts_with(answer, "error: ") && g_fake.count == 0);
    free(answer);
    snprintf(body, sizeof(body), "%sseat=2\naccount=a\npassword=b\nenv=TORIRS_LAUNCH_SEAT=3\n", auth);
    answer = ask(table, "spawn", body);
    check("spawn refuses a launch-owned env key", starts_with(answer, "error: ") && g_fake.count == 0);
    free(answer);
    snprintf(body, sizeof(body), "%sseat=4\naccount=a\npassword=b\n", auth);
    answer = ask(table, "spawn", body);
    check("spawn refuses seat 4 of a party of 3", starts_with(answer, "error: ") && g_fake.count == 0);
    free(answer);
    snprintf(body, sizeof(body), "%sseat=2\naccount=a\npassword=b\nbinary=/bin/sh\n", auth);
    answer = ask(table, "spawn", body);
    check("spawn refuses binary=", starts_with(answer, "error: ") && g_fake.count == 0);
    free(answer);

    answer = spawn_two(table, auth);
    check("spawn seats 2 and 3: ok, two starts", starts_with(answer, "ok\n") && g_fake.count == 2 &&
                                                     contains(answer, "seat=2 pid=") &&
                                                     contains(answer, "seat=3 pid="));
    free(answer);
    seat2 = &g_fake.spawns[0];
    seat3 = &g_fake.spawns[1];
    check("argv: the host's binary and manifest, the account, headless",
          strcmp(seat2->argv, "/opt/torirs/build/torirs\n--manifest\nmanifests/osrs239-scripts.ini\n"
                              "--user\nbloat_p2\n--pass\nquest\n--soft3d\n--window\n765x503\n") == 0);
    check("env: joins the leader's port as seat 2",
          contains(seat2->environment, "\nTORIRS_EMBED_PARTY_JOIN=43600\n") &&
              contains(seat2->environment, "\nTORIRS_EMBED_PARTY_SEAT=2\n") &&
              contains(seat3->environment, "\nTORIRS_EMBED_PARTY_SEAT=3\n"));
    check("env: no embedded options (no LISTEN, SIZE, IO_SERVER)",
          !contains(seat2->environment, "TORIRS_EMBED_PARTY_LISTEN") &&
              !contains(seat2->environment, "TORIRS_EMBED_PARTY_SIZE") &&
              !contains(seat2->environment, "TORIRS_IO_SERVER"));
    check("env: none of the leader's run (MAX_FRAMES, QUEST_SCRIPT, its CONTENT_TEST)",
          !contains(seat2->environment, "TORIRS_MAX_FRAMES") &&
              !contains(seat2->environment, "TORIRS_QUEST_SCRIPT") &&
              !contains(seat2->environment, "/leader/p1"));
    check("env: the virtual clock and the party wait pass through",
          contains(seat2->environment, "\nTORIRS_EMBED_CLOCK_MS=20\n") &&
              contains(seat2->environment, "\nTORIRS_EMBED_PARTY_WAIT_S=60\n") &&
              contains(seat2->environment, "TORIRS_PLUGIN_MANIFEST=plugins/quest_driver.ini\n"));
    snprintf(line, sizeof(line), "\nTORIRS_LAUNCH_LEADER_PID=%d\n", leader);
    check("env: the watchdog's leader pid (the inherited one replaced)",
          contains(seat2->environment, line) &&
              !contains(seat2->environment, "TORIRS_LAUNCH_LEADER_PID=1\n"));
    snprintf(line, sizeof(line), "\nTORIRSSERVER_SAVES=%s\n", g_saves);
    check("env: the leader's saves, on-demand drive, autoplay, dummy SDL",
          contains(seat2->environment, line) &&
              contains(seat2->environment, "\nTORIRS_DRIVE_ON_DEMAND=1\n") &&
              contains(seat2->environment,
                       "\nTORIRS_DRIVE_AUTOPLAY={\"id\":\"_play_bloat\",\"role\":2,\"size\":3}\n") &&
              contains(seat2->environment, "\nSDL_VIDEODRIVER=dummy\n") &&
              contains(seat2->environment, "\nTORIRS_PREFS=\n") &&
              !contains(seat3->environment, "TORIRS_DRIVE_AUTOPLAY"));
    check("env: a seat's own env= line", contains(seat3->environment, "\nTORIRS_EMBED_PARTY_TRACE=1\n"));
    check("one process group: seat 2 leads it, seat 3 joins it",
          seat2->process_group == seat2->pid && seat3->process_group == seat2->pid &&
              getpgid(seat3->pid) == seat2->pid && getpgid(seat2->pid) == seat2->pid);
    {
        char id[64];
        char path[1024];
        char pids[2048] = { 0 };
        char expect[128];
        FILE* file;
        value_of(auth, "session=", id, sizeof(id));
        snprintf(path, sizeof(path), "%s/%s.pids", g_launch_directory, id);
        file = fopen(path, "r");
        if( file )
        {
            size_t got = fread(pids, 1, sizeof(pids) - 1, file);
            pids[got] = '\0';
            fclose(file);
        }
        snprintf(expect, sizeof(expect), "seat=3 pid=%d\n", seat3->pid);
        check("the pids file under build/launch lists both members",
              file && contains(pids, expect) && contains(pids, "process_group="));
        snprintf(path, sizeof(path), "%s/%s/p2", g_launch_directory, id);
        struct stat info;
        check("seat 2's default run directory exists (its client.log)",
              stat(path, &info) == 0 && S_ISDIR(info.st_mode));
    }
    answer = spawn_two(table, auth);
    check("spawn refuses a seat that is already running", starts_with(answer, "error: ") &&
                                                            contains(answer, "already running"));
    free(answer);
    seat_line(table, auth, 2, line, sizeof(line));
    check("status: seat 2 alive, its account, never mailed",
          contains(line, " alive=1 exit=- signal=- account=bloat_p2 mail_age_ms=- unread=0 status=-"));

    /* command -> mailbox -> mail */
    snprintf(body, sizeof(body), "%sseat=all\ncommand=rm -rf /\n", auth);
    answer = ask(table, "command", body);
    check("command refuses a word that is not play/stop/cheat/quit", starts_with(answer, "error: "));
    free(answer);
    snprintf(body, sizeof(body), "%sseat=all\ncommand=cheat ::tele 3222 3218\n", auth);
    answer = ask(table, "command", body);
    check("command seat=all queues for both seats", starts_with(answer, "ok\nqueued=2\n"));
    free(answer);
    snprintf(body, sizeof(body), "%sseat=2\ncommand=play {\"id\":\"_play_bloat\"}\n", auth);
    answer = ask(table, "command", body);
    check("command seat=2 queues for one", starts_with(answer, "ok\nqueued=1\n"));
    free(answer);
    {
        char id[64];
        char environment_token[64] = { 0 };
        char const* at = strstr(seat2->environment, "TORIRS_LAUNCH_SEAT_TOKEN=");
        value_of(auth, "session=", id, sizeof(id));
        if( at )
            value_of(at, "TORIRS_LAUNCH_SEAT_TOKEN=", environment_token, sizeof(environment_token));
        snprintf(body, sizeof(body), "session=%s\nseat=2\nseat_token=%s\nstatus=x\n", id,
                 "ffffffffffffffffffffffffffffffff");
        answer = ask(table, "mail", body);
        check("mail with a wrong seat token is refused",
              starts_with(answer, "error: the seat token is wrong"));
        free(answer);
        snprintf(body, sizeof(body), "session=%s\ntoken=%s\nseat=2\nstatus=x\n", id,
                 strstr(auth, "token=") + 6);
        answer = ask(table, "mail", body);
        check("mail with the LEADER's token is refused (it does not take token=)",
              starts_with(answer, "error: "));
        free(answer);
        snprintf(body, sizeof(body),
                 "session=%s\nseat=2\nseat_token=%s\nstatus=state=playing step=3 hitpoints=99\n",
                 id, environment_token);
        answer = ask(table, "mail", body);
        check("mail with the seat token (from the member's env): its two commands, in order",
              strcmp(answer, "ok\ncommand=cheat ::tele 3222 3218\n"
                             "command=play {\"id\":\"_play_bloat\"}\n") == 0);
        free(answer);
        answer = ask(table, "mail", body);
        check("a second mail: the mailbox is empty", strcmp(answer, "ok\n") == 0);
        free(answer);
    }
    seat_line(table, auth, 2, line, sizeof(line));
    check("status: seat 2's posted status and mail age",
          contains(line, "mail_age_ms=0 unread=0 status=state=playing step=3 hitpoints=99"));
    seat_line(table, auth, 3, line, sizeof(line));
    check("status: seat 3 still holds its unread command", contains(line, " unread=1 "));

    /* a member that exits is recorded; the seat can be spawned again */
    {
        int old_pid = seat3->pid;
        kill(old_pid, SIGKILL);
        sleep_ms(100);
        LaunchSessions_Reap(table);
        seat_line(table, auth, 3, line, sizeof(line));
        check("a member exit is recorded: seat 3 alive=0 signal=9",
              contains(line, " alive=0 exit=- signal=9 "));
        answer = ask(table, "status", auth);
        check("the session stays open for the leader to respawn", contains(answer, "state=open "));
        free(answer);
        snprintf(body, sizeof(body), "%sseat=3\naccount=bloat_p3\npassword=quest\n", auth);
        answer = ask(table, "spawn", body);
        check("spawn seat 3 again: ok, a new pid in the same group",
              starts_with(answer, "ok\n") && seat_pid(table, auth, 3) != old_pid &&
                  getpgid(seat_pid(table, auth, 3)) == seat2->pid);
        free(answer);
    }

    /* the leader pid exits: the group is ended within the grace */
    {
        int pid2 = seat_pid(table, auth, 2);
        int pid3 = seat_pid(table, auth, 3);
        int64_t clock_before = g_fake.now_ms;
        kill_leader(leader);
        LaunchSessions_Reap(table);
        answer = ask(table, "status", auth);
        check("the leader gone: terminating at once, with the reason",
              contains(answer, "state=terminating ") && contains(answer, "reason=the leader (pid"));
        free(answer);
        check("... and closed (both members reaped) before the clock moved",
              reap_until(table, auth, "closed", 3000) && g_fake.now_ms == clock_before);
        check("both members are gone (kill(pid, 0) fails)", gone(pid2) && gone(pid3));
        seat_line(table, auth, 2, line, sizeof(line));
        check("seat 2 ended by SIGTERM (15)", contains(line, " alive=0 exit=- signal=15 "));
        snprintf(body, sizeof(body), "%sseat=2\naccount=a\npassword=b\n", auth);
        answer = ask(table, "spawn", body);
        check("a closed session spawns nothing", starts_with(answer, "error: ") &&
                                                     contains(answer, "is closed"));
        free(answer);
        {
            char id[64];
            char path[1024];
            value_of(auth, "session=", id, sizeof(id));
            snprintf(path, sizeof(path), "%s/%s.pids", g_launch_directory, id);
            check("the closed session's pids file is removed", access(path, F_OK) != 0);
        }
    }
    LaunchSessions_Free(table);
}

static void
case_heartbeat(void)
{
    struct LaunchSessions* table = fake_table();
    int leader = start_sleep();
    char auth[256];
    char body[512];
    char* answer;
    int pid2;

    printf("-- a heartbeat that stops\n");
    open_session(table, leader, 2, auth, sizeof(auth));
    snprintf(body, sizeof(body), "%sseat=2\naccount=hb_p2\npassword=quest\n", auth);
    free(ask(table, "spawn", body));
    pid2 = seat_pid(table, auth, 2);
    g_fake.now_ms += 9000;
    LaunchSessions_Reap(table);
    answer = ask(table, "heartbeat", auth);
    check("9 s without a beat: still open; heartbeat answers ok",
          strcmp(answer, "ok\nstate=open\n") == 0);
    free(answer);
    g_fake.now_ms += 9000;
    LaunchSessions_Reap(table);
    check("9 s after that beat: still alive", !gone(pid2));
    g_fake.now_ms += 2000;
    LaunchSessions_Reap(table);
    check("11 s without a beat (leader pid still alive): the member is told to end",
          reap_until(table, auth, "closed", 3000));
    answer = ask(table, "status", auth);
    check("reason: no heartbeat", contains(answer, "reason=no heartbeat from the leader for 11000 ms"));
    free(answer);
    check("the member is gone", gone(pid2));
    LaunchSessions_Free(table);
    kill_leader(leader);
}

static void
case_close_and_kill(void)
{
    struct LaunchSessions* table = fake_table();
    int leader = start_sleep();
    char auth[256];
    char body[512];
    char line[1024];
    char* answer;
    int pid2;
    int pid3;
    int group;

    printf("-- close: quit, the grace, then the kill (and SIGKILL for a stubborn one)\n");
    open_session(table, leader, 3, auth, sizeof(auth));
    snprintf(body, sizeof(body), "%sseat=2\naccount=c_p2\npassword=quest\n", auth);
    free(ask(table, "spawn", body));
    g_fake.ignore_term_next = 1; /* seat 3 ignores SIGTERM */
    snprintf(body, sizeof(body), "%sseat=3\naccount=c_p3\npassword=quest\n", auth);
    free(ask(table, "spawn", body));
    pid2 = seat_pid(table, auth, 2);
    pid3 = seat_pid(table, auth, 3);
    group = getpgid(pid2);
    sleep_ms(300); /* let seat 3's shell reach its `trap` before any SIGTERM */
    answer = ask(table, "close", auth);
    check("close: ok, closing", strcmp(answer, "ok\nstate=closing\n") == 0);
    free(answer);
    seat_line(table, auth, 2, line, sizeof(line));
    check("close queued quit in each mailbox", contains(line, " unread=1 "));
    snprintf(body, sizeof(body), "%sseat=all\ncommand=stop\n", auth);
    answer = ask(table, "command", body);
    check("a closing session takes no new command", starts_with(answer, "error: "));
    free(answer);
    g_fake.now_ms += 4000;
    LaunchSessions_Reap(table);
    check("inside the quit grace nothing is killed", !gone(pid2) && !gone(pid3));
    g_fake.now_ms += 1500;
    LaunchSessions_Reap(table);
    answer = ask(table, "status", auth);
    check("after the grace: terminating, reason closed by the leader",
          contains(answer, "state=terminating ") && contains(answer, "reason=closed by the leader"));
    free(answer);
    sleep_ms(200);
    LaunchSessions_Reap(table);
    check("seat 2 ended on SIGTERM; seat 3 ignored it", gone(pid2) && !gone(pid3));
    g_fake.now_ms += 5001;
    LaunchSessions_Reap(table);
    check("after the kill grace: SIGKILL, closed", reap_until(table, auth, "closed", 3000));
    seat_line(table, auth, 3, line, sizeof(line));
    check("seat 3 ended by SIGKILL (9)", contains(line, " signal=9 "));
    sleep_ms(200);
    check("the whole process group is gone (sh's sleep child too)",
          killpg(group, 0) != 0 && errno == ESRCH);
    LaunchSessions_Free(table);
    kill_leader(leader);
}

static void
case_host_exit(void)
{
    struct LaunchSessions* table = fake_table();
    int leader = start_sleep();
    char auth[256];
    int pid2;
    int pid3;
    struct timespec start;
    struct timespec end;

    printf("-- the host's own exit kills every member\n");
    open_session(table, leader, 3, auth, sizeof(auth));
    free(spawn_two(table, auth));
    pid2 = seat_pid(table, auth, 2);
    pid3 = seat_pid(table, auth, 3);
    clock_gettime(CLOCK_MONOTONIC, &start);
    LaunchSessions_Free(table);
    clock_gettime(CLOCK_MONOTONIC, &end);
    check("LaunchSessions_Free: both members gone", gone(pid2) && gone(pid3));
    check("... within 1.5 s",
          (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000 < 1500);
    kill_leader(leader);
}

/* REAL: the system spawner, the monotonic clock, the host's reaper once a
 * second, a "client" that is `sleep 30`. The fake leader is SIGKILLed. */
static void
case_real_spawn(void)
{
    struct LaunchSessions_Config config;
    struct LaunchSessions* table;
    char client[700];
    char auth[256];
    char body[512];
    char log_path[1024];
    char id[64];
    char* answer;
    FILE* script;
    int leader = start_sleep();
    int member;
    int64_t killed_at;
    int64_t gone_at = -1;
    struct stat info;

    printf("-- REAL spawn: sleep 30 as the client, the leader SIGKILLed\n");
    snprintf(client, sizeof(client), "%s/fake_client.sh", g_root);
    script = fopen(client, "w");
    if( !script )
    {
        printf("cannot write %s\n", client);
        exit(2);
    }
    /* Prints its argv, then becomes `sleep 30` (same pid, same group). */
    fprintf(script, "#!/bin/sh\necho \"fake client: $*\"\nexec /bin/sleep 30\n");
    fclose(script);
    chmod(client, 0755);
    memset(&config, 0, sizeof(config));
    config.binary_path = client;
    config.manifest_path = "manifests/osrs239-scripts.ini";
    config.launch_directory = g_launch_directory;
    config.spawner = LaunchSessions_SystemSpawner();
    config.clock_ms = LaunchSessions_MonotonicMs;
    config.base_environment = g_base_environment;
    table = LaunchSessions_New(&config);
    open_session(table, leader, 2, auth, sizeof(auth));
    snprintf(body, sizeof(body), "%sseat=2\naccount=real_p2\npassword=quest\n", auth);
    answer = ask(table, "spawn", body);
    check("real spawn: ok", starts_with(answer, "ok\n"));
    free(answer);
    member = seat_pid(table, auth, 2);
    sleep_ms(300);
    value_of(auth, "session=", id, sizeof(id));
    snprintf(log_path, sizeof(log_path), "%s/%s/p2/client.log", g_launch_directory, id);
    {
        char text[512] = { 0 };
        FILE* log = fopen(log_path, "r");
        if( log )
        {
            size_t got = fread(text, 1, sizeof(text) - 1, log);
            text[got] = '\0';
            fclose(log);
        }
        check("the member ran the host's binary with the leader's manifest (its client.log)",
              stat(log_path, &info) == 0 &&
                  contains(text, "fake client: --manifest manifests/osrs239-scripts.ini "
                                 "--user real_p2 --pass quest --soft3d --window 765x503"));
    }
    check("the member is alive in its own process group",
          !gone(member) && getpgid(member) == member && getpgid(member) != getpgid(0));
    kill_leader(leader);
    killed_at = LaunchSessions_MonotonicMs(NULL);
    /* The host's frame loop: the reaper once a second. */
    while( LaunchSessions_MonotonicMs(NULL) - killed_at < 15000 )
    {
        LaunchSessions_Reap(table);
        if( gone(member) )
        {
            gone_at = LaunchSessions_MonotonicMs(NULL);
            break;
        }
        sleep_ms(1000);
    }
    printf("   leader pid %d SIGKILLed; member pid %d gone after %lld ms\n", leader, member,
           gone_at < 0 ? -1LL : (long long)(gone_at - killed_at));
    check("REAL: kill(member, 0) fails within 15 s of the leader's SIGKILL", gone_at >= 0);
    LaunchSessions_Free(table);
}

static int
remove_entry(char const* path, struct stat const* info, int flag, struct FTW* walk)
{
    (void)info;
    (void)flag;
    (void)walk;
    return remove(path);
}

int
main(void)
{
    char const* temporary = getenv("TMPDIR");
    char own[1024];
    int leader;

    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(g_root, sizeof(g_root), "%s/launch_sessions_test.XXXXXX",
             temporary && temporary[0] ? temporary : "/tmp");
    if( !mkdtemp(g_root) )
    {
        printf("mkdtemp %s: %s\n", g_root, strerror(errno));
        return 2;
    }
    snprintf(g_launch_directory, sizeof(g_launch_directory), "%s/build/launch", g_root);
    snprintf(g_saves, sizeof(g_saves), "%s/saves", g_root);
    printf("launch_sessions_test: scratch %s\n", g_root);

    check("the service runs on this platform", LaunchSessions_UnsupportedReason() == NULL);
    check("the own binary path is absolute",
          LaunchSessions_OwnBinaryPath("launch_sessions_test", own, sizeof(own)) == 0 &&
              own[0] == '/');

    case_open_spawn_status();
    case_heartbeat();
    case_close_and_kill();
    case_host_exit();
    case_real_spawn();

    /* Nothing this test started may be left behind. */
    leader = waitpid(-1, NULL, WNOHANG);
    check("no child of the test is left, running or unreaped", leader == -1 && errno == ECHILD);

    /* A failed run keeps its scratch (the members' client.log) to read. */
    if( !g_fails )
        nftw(g_root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
    printf("launch_sessions_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
