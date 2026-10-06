/*
 * The launch service of the client's embedded IO server: the session table,
 * its seven verbs, the reaper and the spawners. The contract is in
 * launch_sessions.h; the design in
 * docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-06j.md (DESIGN (1), (2)).
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if defined(_WIN32) && !defined(_CRT_RAND_S)
#define _CRT_RAND_S
#endif

#include "platform/launch_sessions.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

/* Web, Android and iOS have no process to start and no party link: the whole
 * service is the constant answer below, with no spawning code compiled. */
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || \
    (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define LAUNCH_SESSIONS_DESKTOP 0
#else
#define LAUNCH_SESSIONS_DESKTOP 1
#endif

#if !LAUNCH_SESSIONS_DESKTOP

#define LAUNCH_SESSIONS_UNSUPPORTED_ANSWER "unsupported: desktop only\n"

struct LaunchSessions
{
    int unused;
};

struct LaunchSessions*
LaunchSessions_New(struct LaunchSessions_Config const* config)
{
    struct LaunchSessions* table = calloc(1, sizeof(*table));

    (void)config;
    assert(table);
    return table;
}

void
LaunchSessions_Free(struct LaunchSessions* table)
{
    free(table);
}

char const*
LaunchSessions_UnsupportedReason(void)
{
    return "desktop only";
}

char*
LaunchSessions_Answer(
    struct LaunchSessions* table,
    char const* verb,
    char const* body,
    int body_size,
    char const* peer,
    int* out_size)
{
    size_t length = strlen(LAUNCH_SESSIONS_UNSUPPORTED_ANSWER);
    char* answer = malloc(length + 1);

    assert(table);
    assert(verb);
    assert(peer);
    assert(out_size);
    (void)body;
    (void)body_size;
    assert(answer);
    memcpy(answer, LAUNCH_SESSIONS_UNSUPPORTED_ANSWER, length + 1);
    *out_size = (int)length;
    return answer;
}

int
LaunchSessions_Reap(struct LaunchSessions* table)
{
    assert(table);
    return 0;
}

struct LaunchSessions_Spawner const*
LaunchSessions_SystemSpawner(void)
{
    return NULL;
}

int64_t
LaunchSessions_MonotonicMs(void* context)
{
    (void)context;
    return 0;
}

int
LaunchSessions_OwnBinaryPath(char const* argv0, char* out, int out_size)
{
    assert(argv0);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    return -1;
}

#else /* LAUNCH_SESSIONS_DESKTOP */

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#include <windows.h>
#define LAUNCH_SIGNAL_TERMINATE 15
#define LAUNCH_SIGNAL_KILL 9
#define launch_make_directory(path) _mkdir(path)
#define launch_host_pid() ((int)GetCurrentProcessId())
#else
#include <limits.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#define LAUNCH_SIGNAL_TERMINATE SIGTERM
#define LAUNCH_SIGNAL_KILL SIGKILL
#define launch_make_directory(path) mkdir((path), 0755)
#define launch_host_pid() ((int)getpid())
#endif

enum
{
    LAUNCH_HEARTBEAT_TIMEOUT_MS_DEFAULT = 10000,
    LAUNCH_QUIT_GRACE_MS_DEFAULT = 5000,
    LAUNCH_KILL_GRACE_MS_DEFAULT = 5000,
    LAUNCH_TOKEN_BYTES = 16,
    LAUNCH_TOKEN_SIZE = LAUNCH_TOKEN_BYTES * 2 + 1,
    LAUNCH_REQUEST_PAIR_MAX = 256,
    LAUNCH_ACCOUNT_MAX = 64,
    LAUNCH_STATUS_MAX = 512,
    LAUNCH_REASON_MAX = 160,
    LAUNCH_ENVIRONMENT_EXTRA = 24,
};

enum LaunchSessionState
{
    LAUNCH_SESSION_FREE = 0,
    LAUNCH_SESSION_OPEN,
    /* `close`: every seat was told `quit`; the grace runs. */
    LAUNCH_SESSION_CLOSING,
    /* SIGTERM sent; SIGKILL at the deadline. */
    LAUNCH_SESSION_TERMINATING,
    /* Every member reaped; kept so the leader can read why. */
    LAUNCH_SESSION_CLOSED,
};

struct LaunchSeat
{
    int spawned;
    int pid;
    /* The group it was started in. Signalled only while the seat is alive:
     * an unreaped child keeps its pid and its group id from being reused, so
     * a killpg can never reach a stranger's group. */
    int process_group;
    int alive;
    int exit_code;
    int exit_signal;
    char account[LAUNCH_ACCOUNT_MAX];
    char seat_token[LAUNCH_TOKEN_SIZE];
    char directory[LAUNCH_SESSIONS_PATH_MAX];
    char* mailbox[LAUNCH_SESSIONS_MAILBOX_MAX];
    int mailbox_count;
    char status[LAUNCH_STATUS_MAX];
    int64_t mail_ms;
};

struct LaunchSession
{
    enum LaunchSessionState state;
    int number;
    char id[48];
    char token[LAUNCH_TOKEN_SIZE];
    int leader_pid;
    int port;
    int size;
    char saves[LAUNCH_SESSIONS_PATH_MAX];
    int process_group;
    int killed;
    int64_t heartbeat_ms;
    int64_t deadline_ms;
    int64_t closed_ms;
    char reason[LAUNCH_REASON_MAX];
    /* Indexed by seat number: 0 and 1 (the leader) are never used. */
    struct LaunchSeat seats[LAUNCH_SESSIONS_PARTY_MAX + 1];
};

struct LaunchSessions
{
    char binary_path[LAUNCH_SESSIONS_PATH_MAX];
    char manifest_path[LAUNCH_SESSIONS_PATH_MAX];
    int has_manifest;
    char launch_directory[LAUNCH_SESSIONS_PATH_MAX];
    struct LaunchSessions_Spawner const* spawner;
    int64_t (*clock_ms)(void* context);
    void* clock_context;
    char* const* base_environment;
    int heartbeat_timeout_ms;
    int quit_grace_ms;
    int kill_grace_ms;
    int next_number;
    struct LaunchSession sessions[LAUNCH_SESSIONS_SESSION_MAX];
};

/* ---- the answer text ---------------------------------------------------- */

struct LaunchText
{
    char* data;
    int size;
    int capacity;
};

static void
text_append(struct LaunchText* text, char const* format, ...)
{
    va_list arguments;
    int needed;

    assert(text);
    assert(format);
    va_start(arguments, format);
    needed = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);
    assert(needed >= 0);
    if( text->size + needed + 1 > text->capacity )
    {
        int capacity = text->capacity ? text->capacity : 256;
        while( capacity < text->size + needed + 1 )
            capacity *= 2;
        text->data = realloc(text->data, (size_t)capacity);
        assert(text->data);
        text->capacity = capacity;
    }
    va_start(arguments, format);
    vsnprintf(text->data + text->size, (size_t)(needed + 1), format, arguments);
    va_end(arguments);
    text->size += needed;
}

/* ---- the request -------------------------------------------------------- */

struct LaunchPair
{
    char const* key;
    char const* value;
};

struct LaunchRequest
{
    char* copy;
    int count;
    struct LaunchPair pairs[LAUNCH_REQUEST_PAIR_MAX];
};

/* Splits `body` into key=value lines. NULL, or the refusal text. */
static char const*
request_parse(struct LaunchRequest* request, char const* body, int body_size)
{
    char* line;

    memset(request, 0, sizeof(*request));
    request->copy = malloc((size_t)body_size + 1);
    assert(request->copy);
    if( body_size > 0 )
        memcpy(request->copy, body, (size_t)body_size);
    request->copy[body_size] = '\0';
    line = request->copy;
    while( *line )
    {
        char* end = strchr(line, '\n');
        char* equals;

        if( end )
            *end = '\0';
        if( end && end > line && end[-1] == '\r' )
            end[-1] = '\0';
        if( line[0] != '\0' )
        {
            equals = strchr(line, '=');
            if( !equals || equals == line )
                return "a request line is not key=value";
            if( request->count == LAUNCH_REQUEST_PAIR_MAX )
                return "the request has too many lines";
            *equals = '\0';
            request->pairs[request->count].key = line;
            request->pairs[request->count].value = equals + 1;
            request->count++;
        }
        if( !end )
            break;
        line = end + 1;
    }
    return NULL;
}

static char const*
request_value(struct LaunchRequest const* request, char const* key)
{
    for( int i = 0; i < request->count; i++ )
        if( strcmp(request->pairs[i].key, key) == 0 )
            return request->pairs[i].value;
    return NULL;
}

/* The first key not in the NULL-terminated `allowed`, or NULL. */
static char const*
request_unknown_key(struct LaunchRequest const* request, char const* const* allowed)
{
    for( int i = 0; i < request->count; i++ )
    {
        int known = 0;
        for( int k = 0; allowed[k]; k++ )
            if( strcmp(request->pairs[i].key, allowed[k]) == 0 )
                known = 1;
        if( !known )
            return request->pairs[i].key;
    }
    return NULL;
}

/* A whole decimal integer in [low, high]; 0 and *out on success, -1 else. */
static int
parse_integer(char const* text, long low, long high, long* out)
{
    char* end = NULL;
    long value;

    if( !text || !text[0] )
        return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if( errno || !end || *end != '\0' || value < low || value > high )
        return -1;
    *out = value;
    return 0;
}

/* ---- helpers ------------------------------------------------------------ */

static int
peer_is_loopback(char const* peer)
{
    return strcmp(peer, "local") == 0 || strncmp(peer, "127.", 4) == 0 ||
           strcmp(peer, "::1") == 0 || strncmp(peer, "::ffff:127.", 11) == 0;
}

static int
make_token(char* out)
{
    unsigned char bytes[LAUNCH_TOKEN_BYTES];

#if defined(_WIN32)
    for( int i = 0; i < LAUNCH_TOKEN_BYTES; i++ )
    {
        unsigned int value = 0;
        if( rand_s(&value) != 0 )
            return -1;
        bytes[i] = (unsigned char)value;
    }
#elif defined(__APPLE__)
    arc4random_buf(bytes, sizeof(bytes));
#else
    FILE* random_source = fopen("/dev/urandom", "rb");
    size_t got;

    if( !random_source )
        return -1;
    got = fread(bytes, 1, sizeof(bytes), random_source);
    fclose(random_source);
    if( got != sizeof(bytes) )
        return -1;
#endif
    for( int i = 0; i < LAUNCH_TOKEN_BYTES; i++ )
        snprintf(out + i * 2, 3, "%02x", bytes[i]);
    return 0;
}

/* Constant time in the token's length: a token is a secret. */
static int
token_matches(char const* expected, char const* given)
{
    unsigned difference = 0;
    size_t length = strlen(expected);

    if( !given || strlen(given) != length )
        return 0;
    for( size_t i = 0; i < length; i++ )
        difference |= (unsigned)(expected[i] ^ given[i]);
    return difference == 0;
}

/* mkdir -p. 0, or the errno of the component that could not be made. */
static int
make_directories(char const* path)
{
    char partial[LAUNCH_SESSIONS_PATH_MAX];
    size_t length = strlen(path);

    if( length == 0 || length >= sizeof(partial) )
        return ENAMETOOLONG;
    memcpy(partial, path, length + 1);
    for( size_t i = 1; i <= length; i++ )
    {
        if( partial[i] != '/' && partial[i] != '\\' && partial[i] != '\0' )
            continue;
        char saved = partial[i];
        partial[i] = '\0';
        if( launch_make_directory(partial) != 0 && errno != EEXIST )
            return errno;
        partial[i] = saved;
    }
    return 0;
}

static int64_t
table_now(struct LaunchSessions const* table)
{
    return table->clock_ms(table->clock_context);
}

static char const*
state_name(enum LaunchSessionState state)
{
    if( state == LAUNCH_SESSION_OPEN )
        return "open";
    if( state == LAUNCH_SESSION_CLOSING )
        return "closing";
    if( state == LAUNCH_SESSION_TERMINATING )
        return "terminating";
    if( state == LAUNCH_SESSION_CLOSED )
        return "closed";
    return "free";
}

static void
pids_path(struct LaunchSessions const* table, struct LaunchSession const* session, char* out,
          size_t out_size)
{
    snprintf(out, out_size, "%s/%s.pids", table->launch_directory, session->id);
}

/* The orphan list: while a session lives, its pids are on disk so a later
 * `./launch stop` can reap what a crashed host left. 0, or an errno. */
static int
pids_write(struct LaunchSessions const* table, struct LaunchSession const* session)
{
    char path[LAUNCH_SESSIONS_PATH_MAX + 64];
    FILE* file;

    pids_path(table, session, path, sizeof(path));
    file = fopen(path, "w");
    if( !file )
        return errno ? errno : EIO;
    fprintf(file, "host=%d\nleader=%d\nprocess_group=%d\n", launch_host_pid(),
            session->leader_pid, session->process_group);
    for( int seat = 2; seat <= session->size; seat++ )
        if( session->seats[seat].spawned && session->seats[seat].alive )
            fprintf(file, "seat=%d pid=%d\n", seat, session->seats[seat].pid);
    fclose(file);
    return 0;
}

static void
pids_refresh(struct LaunchSessions const* table, struct LaunchSession const* session)
{
    char path[LAUNCH_SESSIONS_PATH_MAX + 64];
    int error;

    if( session->state == LAUNCH_SESSION_CLOSED )
    {
        pids_path(table, session, path, sizeof(path));
        remove(path);
        return;
    }
    error = pids_write(table, session);
    if( error )
        fprintf(stderr, "launch: session %s: cannot write its pids file: %s\n", session->id,
                strerror(error));
}

static void
mailbox_clear(struct LaunchSeat* seat)
{
    for( int i = 0; i < seat->mailbox_count; i++ )
        free(seat->mailbox[i]);
    seat->mailbox_count = 0;
}

static int
mailbox_push(struct LaunchSeat* seat, char const* command)
{
    if( seat->mailbox_count == LAUNCH_SESSIONS_MAILBOX_MAX )
        return -1;
    seat->mailbox[seat->mailbox_count] = strdup(command);
    assert(seat->mailbox[seat->mailbox_count]);
    seat->mailbox_count++;
    return 0;
}

static int
session_alive_seats(struct LaunchSession const* session)
{
    int alive = 0;
    for( int seat = 2; seat <= session->size; seat++ )
        if( session->seats[seat].spawned && session->seats[seat].alive )
            alive++;
    return alive;
}

static void
session_signal(struct LaunchSessions const* table, struct LaunchSession* session,
               int signal_number)
{
    struct LaunchSessions_Spawner const* spawner = table->spawner;

    /* The group reaches anything a member started; the pid covers a member
     * that left its group. A seat respawned after its group emptied leads a
     * group of its own. */
    for( int number = 2; number <= session->size; number++ )
    {
        struct LaunchSeat const* seat = &session->seats[number];
        if( !seat->spawned || !seat->alive )
            continue;
        spawner->signal_group(spawner->context, seat->process_group, signal_number);
        spawner->signal_process(spawner->context, seat->pid, signal_number);
    }
}

static void
session_mark_closed(struct LaunchSessions const* table, struct LaunchSession* session)
{
    session->state = LAUNCH_SESSION_CLOSED;
    session->closed_ms = table_now(table);
    for( int seat = 2; seat <= session->size; seat++ )
        mailbox_clear(&session->seats[seat]);
    fprintf(stderr, "launch: session %s closed: %s\n", session->id, session->reason);
    pids_refresh(table, session);
}

/* SIGTERM now, SIGKILL when kill_grace_ms has passed (the reaper). */
static void
session_terminate(struct LaunchSessions const* table, struct LaunchSession* session,
                  char const* reason)
{
    if( reason )
        snprintf(session->reason, sizeof(session->reason), "%s", reason);
    if( session_alive_seats(session) == 0 )
    {
        session_mark_closed(table, session);
        return;
    }
    fprintf(stderr, "launch: session %s: %s: terminating its members\n", session->id,
            session->reason);
    session_signal(table, session, LAUNCH_SIGNAL_TERMINATE);
    session->state = LAUNCH_SESSION_TERMINATING;
    session->killed = 0;
    session->deadline_ms = table_now(table) + table->kill_grace_ms;
}

/* Records every member that has ended. Returns how many changed. */
static int
session_reap_members(struct LaunchSessions const* table, struct LaunchSession* session, int block)
{
    struct LaunchSessions_Spawner const* spawner = table->spawner;
    int changed = 0;

    for( int number = 2; number <= session->size; number++ )
    {
        struct LaunchSeat* seat = &session->seats[number];
        int exit_code = -1;
        int exit_signal = 0;

        if( !seat->spawned || !seat->alive )
            continue;
        if( !spawner->reap_child(spawner->context, seat->pid, block, &exit_code, &exit_signal) )
            continue;
        seat->alive = 0;
        seat->exit_code = exit_code;
        seat->exit_signal = exit_signal;
        changed++;
        fprintf(stderr, "launch: session %s: seat %d (pid %d) exited: %s %d\n", session->id,
                number, seat->pid, exit_signal ? "signal" : "code",
                exit_signal ? exit_signal : exit_code);
    }
    return changed;
}

/* The session `id`, if its token matches. NULL and the refusal in *refusal
 * otherwise. */
static struct LaunchSession*
session_authorised(struct LaunchSessions* table, struct LaunchRequest const* request,
                   char const** refusal)
{
    char const* id = request_value(request, "session");
    char const* token = request_value(request, "token");

    if( !id )
    {
        *refusal = "session= is required";
        return NULL;
    }
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
    {
        struct LaunchSession* session = &table->sessions[i];
        if( session->state == LAUNCH_SESSION_FREE || strcmp(session->id, id) != 0 )
            continue;
        if( !token_matches(session->token, token) )
        {
            *refusal = "the session token is wrong or missing";
            return NULL;
        }
        return session;
    }
    *refusal = "no such session";
    return NULL;
}

/* ---- a member's environment and command line ----------------------------- */

/* The leader's env a member must NOT inherit: the embedded options (a member
 * hosts no world and no IO server), the leader's own run (its script, frame
 * cap, ledger directory, prefs) and everything the spawn sets itself. */
static char const* const g_stripped_keys[] = {
    "TORIRS_EMBED_PARTY_LISTEN",
    "TORIRS_EMBED_PARTY_SIZE",
    "TORIRS_EMBED_PARTY_JOIN",
    "TORIRS_EMBED_PARTY_SEAT",
    "TORIRS_IO_SERVER",
    "TORIRS_MAX_FRAMES",
    "TORIRS_QUEST_SCRIPT",
    "TORIRS_DRIVE_AUTOPLAY",
    "TORIRS_DRIVE_ON_DEMAND",
    "TORIRS_CONTENT_TEST",
    "TORIRS_PLUGIN_PREFS",
    "TORIRS_PREFS",
    "TORIRSSERVER_SAVES",
    "SDL_VIDEODRIVER",
    "SDL_AUDIODRIVER",
    NULL,
};

/* Keys a request's `env=` may not set: the ones that make a member something
 * other than a member of THIS session, and the loader's injection knobs. */
static int
seat_environment_key_refused(char const* key, size_t key_length)
{
    static char const* const refused_prefixes[] = {
        "TORIRS_LAUNCH_", "TORIRS_EMBED_PARTY_LISTEN", "TORIRS_EMBED_PARTY_SIZE",
        "TORIRS_EMBED_PARTY_JOIN", "TORIRS_EMBED_PARTY_SEAT", "TORIRS_IO_SERVER",
        "DYLD_", "LD_", NULL,
    };
    if( key_length == 0 )
        return 1;
    for( size_t i = 0; i < key_length; i++ )
    {
        char c = key[i];
        int ok = c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (i > 0 && c >= '0' && c <= '9');
        if( !ok )
            return 1;
    }
    for( int i = 0; refused_prefixes[i]; i++ )
    {
        size_t prefix_length = strlen(refused_prefixes[i]);
        if( key_length >= prefix_length && strncmp(key, refused_prefixes[i], prefix_length) == 0 )
            return 1;
    }
    return 0;
}

struct LaunchEnvironment
{
    char** entries;
    int count;
    int capacity;
};

static int
environment_find(struct LaunchEnvironment const* environment, char const* key, size_t key_length)
{
    for( int i = 0; i < environment->count; i++ )
        if( strncmp(environment->entries[i], key, key_length) == 0 &&
            environment->entries[i][key_length] == '=' )
            return i;
    return -1;
}

/* `entry` is KEY=VALUE; replaces an existing KEY. */
static void
environment_put(struct LaunchEnvironment* environment, char const* entry)
{
    char const* equals = strchr(entry, '=');
    int index;
    char* copy;

    assert(equals);
    copy = strdup(entry);
    assert(copy);
    index = environment_find(environment, entry, (size_t)(equals - entry));
    if( index >= 0 )
    {
        free(environment->entries[index]);
        environment->entries[index] = copy;
        return;
    }
    if( environment->count + 2 > environment->capacity )
    {
        environment->capacity = environment->capacity ? environment->capacity * 2 : 64;
        environment->entries =
            realloc(environment->entries, sizeof(char*) * (size_t)environment->capacity);
        assert(environment->entries);
    }
    environment->entries[environment->count++] = copy;
    environment->entries[environment->count] = NULL;
}

static void
environment_set(struct LaunchEnvironment* environment, char const* key, char const* value)
{
    struct LaunchText entry = { 0 };

    text_append(&entry, "%s=%s", key, value);
    environment_put(environment, entry.data);
    free(entry.data);
}

static void
environment_unset(struct LaunchEnvironment* environment, char const* key)
{
    int index = environment_find(environment, key, strlen(key));

    if( index < 0 )
        return;
    free(environment->entries[index]);
    environment->entries[index] = environment->entries[environment->count - 1];
    environment->count--;
    environment->entries[environment->count] = NULL;
}

static void
environment_free(struct LaunchEnvironment* environment)
{
    for( int i = 0; i < environment->count; i++ )
        free(environment->entries[i]);
    free(environment->entries);
    memset(environment, 0, sizeof(*environment));
}

static int
key_is_stripped(char const* entry)
{
    char const* equals = strchr(entry, '=');
    size_t key_length = equals ? (size_t)(equals - entry) : strlen(entry);

    if( strncmp(entry, "TORIRS_LAUNCH_", 14) == 0 )
        return 1;
    for( int i = 0; g_stripped_keys[i]; i++ )
        if( strlen(g_stripped_keys[i]) == key_length &&
            strncmp(entry, g_stripped_keys[i], key_length) == 0 )
            return 1;
    return 0;
}

struct LaunchSeatRequest
{
    int seat;
    char const* account;
    char const* password;
    int headless;
    char const* autoplay;
    char const* directory;
    char const* environment[LAUNCH_SESSIONS_SEAT_ENV_MAX];
    int environment_count;
    char const* unset[LAUNCH_SESSIONS_SEAT_ENV_MAX];
    int unset_count;
};

static void
member_environment(
    struct LaunchSessions const* table,
    struct LaunchSession const* session,
    struct LaunchSeatRequest const* request,
    struct LaunchSeat const* seat,
    struct LaunchEnvironment* environment)
{
    char* const* base = table->base_environment ? table->base_environment : environ;
    char number[32];
    struct LaunchText prefs = { 0 };

    memset(environment, 0, sizeof(*environment));
    for( int i = 0; base && base[i]; i++ )
        if( strchr(base[i], '=') && !key_is_stripped(base[i]) )
            environment_put(environment, base[i]);

    /* The member role of the embedded transport: no world of its own, the
     * leader's on 127.0.0.1:<port>, logging in as seat n. */
    snprintf(number, sizeof(number), "%d", session->port);
    environment_set(environment, "TORIRS_EMBED_PARTY_JOIN", number);
    snprintf(number, sizeof(number), "%d", request->seat);
    environment_set(environment, "TORIRS_EMBED_PARTY_SEAT", number);
    /* Its fresh account's fixture goes where the leader's world reads it. */
    environment_set(environment, "TORIRSSERVER_SAVES", session->saves);
    environment_set(environment, "TORIRS_CONTENT_TEST", seat->directory);
    text_append(&prefs, "%s/plugin_prefs.ini", seat->directory);
    environment_set(environment, "TORIRS_PLUGIN_PREFS", prefs.data);
    free(prefs.data);
    /* Never the owner's preferences.ini: empty is "do not touch the file". */
    environment_set(environment, "TORIRS_PREFS", "");
    environment_set(environment, "TORIRS_STDERR_UNBUFFERED", "1");
    environment_set(environment, "TORIRS_DRIVE_ON_DEMAND", "1");
    if( request->autoplay )
        environment_set(environment, "TORIRS_DRIVE_AUTOPLAY", request->autoplay);
    /* The member's own watchdog (exit when the leader pid is gone) and its
     * mailbox address. */
    snprintf(number, sizeof(number), "%d", session->leader_pid);
    environment_set(environment, "TORIRS_LAUNCH_LEADER_PID", number);
    environment_set(environment, "TORIRS_LAUNCH_SESSION", session->id);
    snprintf(number, sizeof(number), "%d", request->seat);
    environment_set(environment, "TORIRS_LAUNCH_SEAT", number);
    environment_set(environment, "TORIRS_LAUNCH_SEAT_TOKEN", seat->seat_token);
    if( request->headless )
    {
        environment_set(environment, "SDL_VIDEODRIVER", "dummy");
        environment_set(environment, "SDL_AUDIODRIVER", "dummy");
    }
    for( int i = 0; i < request->environment_count; i++ )
        environment_put(environment, request->environment[i]);
    for( int i = 0; i < request->unset_count; i++ )
        environment_unset(environment, request->unset[i]);
}

/* The leader's binary and manifest; nothing from the request but the
 * account. run.py's member line (tools/quest_gate/run.py run_party). */
static int
member_argv(struct LaunchSessions const* table, struct LaunchSeatRequest const* request,
            char const** argv)
{
    int argc = 0;

    argv[argc++] = table->binary_path;
    if( table->has_manifest )
    {
        argv[argc++] = "--manifest";
        argv[argc++] = table->manifest_path;
    }
    argv[argc++] = "--user";
    argv[argc++] = request->account;
    argv[argc++] = "--pass";
    argv[argc++] = request->password;
    if( request->headless )
        argv[argc++] = "--soft3d";
    argv[argc++] = "--window";
    argv[argc++] = "765x503";
    argv[argc] = NULL;
    return argc;
}

/* 0, or the errno of the failed start. */
static int
member_spawn(struct LaunchSessions* table, struct LaunchSession* session,
             struct LaunchSeatRequest const* request)
{
    struct LaunchSessions_Spawner const* spawner = table->spawner;
    struct LaunchSeat* seat = &session->seats[request->seat];
    struct LaunchEnvironment environment;
    struct LaunchText log_path = { 0 };
    char const* argv[16];
    int pid = 0;
    int process_group = 0;
    int error;

    mailbox_clear(seat);
    memset(seat, 0, sizeof(*seat));
    seat->mail_ms = -1;
    seat->exit_code = -1;
    snprintf(seat->account, sizeof(seat->account), "%s", request->account);
    if( make_token(seat->seat_token) != 0 )
        return EIO;
    if( request->directory )
        snprintf(seat->directory, sizeof(seat->directory), "%s", request->directory);
    else
        snprintf(seat->directory, sizeof(seat->directory), "%s/%s/p%d", table->launch_directory,
                 session->id, request->seat);
    error = make_directories(seat->directory);
    if( error )
        return error;
    text_append(&log_path, "%s/client.log", seat->directory);
    member_argv(table, request, argv);
    member_environment(table, session, request, seat, &environment);

    /* Every member of a session shares one process group, so one killpg
     * ends the party. A group whose members have all gone no longer exists:
     * the next member then leads a new one. */
    if( session->process_group > 0 && session_alive_seats(session) > 0 )
        process_group = session->process_group;
    error = spawner->spawn(spawner->context, (char* const*)argv, environment.entries,
                           log_path.data, process_group, &pid, &process_group);
    if( error && process_group > 0 )
    {
        process_group = 0;
        error = spawner->spawn(spawner->context, (char* const*)argv, environment.entries,
                               log_path.data, 0, &pid, &process_group);
    }
    environment_free(&environment);
    free(log_path.data);
    if( error )
        return error;
    seat->spawned = 1;
    seat->alive = 1;
    seat->pid = pid;
    seat->process_group = process_group;
    session->process_group = process_group;
    fprintf(stderr, "launch: session %s: seat %d is pid %d (%s, process group %d)\n", session->id,
            request->seat, pid, request->account, process_group);
    return 0;
}

/* ---- the verbs ---------------------------------------------------------- */

static void
answer_error(struct LaunchText* answer, char const* format, ...)
{
    char reason[LAUNCH_SESSIONS_LINE_MAX];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(reason, sizeof(reason), format, arguments);
    va_end(arguments);
    text_append(answer, "error: %s\n", reason);
}

static char const*
base_environment_value(struct LaunchSessions const* table, char const* key)
{
    char* const* base = table->base_environment ? table->base_environment : environ;
    size_t key_length = strlen(key);

    for( int i = 0; base && base[i]; i++ )
        if( strncmp(base[i], key, key_length) == 0 && base[i][key_length] == '=' )
            return base[i] + key_length + 1;
    return NULL;
}

static void
verb_open(struct LaunchSessions* table, struct LaunchRequest const* request,
          struct LaunchText* answer)
{
    static char const* const allowed[] = { "pid", "saves", "port", "size", NULL };
    char const* unknown = request_unknown_key(request, allowed);
    char const* saves = request_value(request, "saves");
    struct LaunchSession* session = NULL;
    long pid = 0;
    long port = 0;
    long size = 0;
    int error;

    if( unknown )
    {
        answer_error(answer, "launch/open does not take %s=", unknown);
        return;
    }
    if( parse_integer(request_value(request, "pid"), 1, 0x7fffffffL, &pid) != 0 ||
        parse_integer(request_value(request, "port"), 1, 65535, &port) != 0 ||
        parse_integer(request_value(request, "size"), 2, LAUNCH_SESSIONS_PARTY_MAX, &size) != 0 )
    {
        answer_error(answer, "launch/open needs pid=<leader pid> port=<1..65535> size=<2..%d>",
                     LAUNCH_SESSIONS_PARTY_MAX);
        return;
    }
    if( !table->spawner->process_alive(table->spawner->context, (int)pid) )
    {
        answer_error(answer, "the leader pid %ld is not running", pid);
        return;
    }
    if( !saves || !saves[0] )
        saves = base_environment_value(table, "TORIRSSERVER_SAVES");
    if( !saves || !saves[0] )
    {
        answer_error(answer, "launch/open needs saves= (the leader's TORIRSSERVER_SAVES): "
                             "a member's fresh account must be where the leader's world reads it");
        return;
    }
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX && !session; i++ )
        if( table->sessions[i].state == LAUNCH_SESSION_FREE )
            session = &table->sessions[i];
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
    {
        struct LaunchSession* candidate = &table->sessions[i];
        if( session || candidate->state != LAUNCH_SESSION_CLOSED )
            continue;
        session = candidate;
        for( int k = 0; k < LAUNCH_SESSIONS_SESSION_MAX; k++ )
            if( table->sessions[k].state == LAUNCH_SESSION_CLOSED &&
                table->sessions[k].closed_ms < session->closed_ms )
                session = &table->sessions[k];
    }
    if( !session )
    {
        answer_error(answer, "all %d launch sessions are live", LAUNCH_SESSIONS_SESSION_MAX);
        return;
    }
    error = make_directories(table->launch_directory);
    if( error )
    {
        answer_error(answer, "cannot make %s: %s", table->launch_directory, strerror(error));
        return;
    }
    memset(session, 0, sizeof(*session));
    if( make_token(session->token) != 0 )
    {
        answer_error(answer, "no source of randomness for the session token");
        return;
    }
    session->number = ++table->next_number;
    snprintf(session->id, sizeof(session->id), "%d-%d", launch_host_pid(), session->number);
    session->leader_pid = (int)pid;
    session->port = (int)port;
    session->size = (int)size;
    snprintf(session->saves, sizeof(session->saves), "%s", saves);
    session->heartbeat_ms = table_now(table);
    snprintf(session->reason, sizeof(session->reason), "-");
    error = pids_write(table, session);
    if( error )
    {
        answer_error(answer, "cannot write the pids file under %s: %s", table->launch_directory,
                     strerror(error));
        memset(session, 0, sizeof(*session));
        return;
    }
    session->state = LAUNCH_SESSION_OPEN;
    fprintf(stderr, "launch: session %s opened: leader pid %d, party of %d on port %d\n",
            session->id, session->leader_pid, session->size, session->port);
    text_append(answer, "ok\nsession=%s\ntoken=%s\n", session->id, session->token);
}

/* Fills `seats` from the request's seat blocks (each starts at `seat=`).
 * Returns the count, or -1 with the refusal in `answer`. */
static int
spawn_parse_seats(struct LaunchSession const* session, struct LaunchRequest const* request,
                  struct LaunchSeatRequest* seats, struct LaunchText* answer)
{
    int count = 0;
    struct LaunchSeatRequest* current = NULL;

    for( int i = 0; i < request->count; i++ )
    {
        char const* key = request->pairs[i].key;
        char const* value = request->pairs[i].value;
        long number = 0;

        if( strcmp(key, "session") == 0 || strcmp(key, "token") == 0 )
            continue;
        if( strcmp(key, "seat") == 0 )
        {
            if( parse_integer(value, 2, session->size, &number) != 0 )
            {
                answer_error(answer, "seat=%s is not a member seat of a party of %d (2..%d)",
                             value, session->size, session->size);
                return -1;
            }
            for( int k = 0; k < count; k++ )
                if( seats[k].seat == (int)number )
                {
                    answer_error(answer, "seat %ld is named twice", number);
                    return -1;
                }
            if( session->seats[number].spawned && session->seats[number].alive )
            {
                answer_error(answer, "seat %ld is already running (pid %d)", number,
                             session->seats[number].pid);
                return -1;
            }
            current = &seats[count++];
            memset(current, 0, sizeof(*current));
            current->seat = (int)number;
            current->headless = 1;
            continue;
        }
        if( !current )
        {
            answer_error(answer, "%s= comes before any seat=", key);
            return -1;
        }
        if( strcmp(key, "account") == 0 )
            current->account = value;
        else if( strcmp(key, "password") == 0 )
            current->password = value;
        else if( strcmp(key, "autoplay") == 0 )
            current->autoplay = value;
        else if( strcmp(key, "directory") == 0 )
            current->directory = value;
        else if( strcmp(key, "headless") == 0 )
        {
            if( parse_integer(value, 0, 1, &number) != 0 )
            {
                answer_error(answer, "headless= is 0 or 1, not %s", value);
                return -1;
            }
            current->headless = (int)number;
        }
        else if( strcmp(key, "env") == 0 || strcmp(key, "unset") == 0 )
        {
            int is_unset = key[0] == 'u';
            char const* equals = strchr(value, '=');
            size_t key_length = is_unset ? strlen(value) : (equals ? (size_t)(equals - value) : 0);
            int* slot_count = is_unset ? &current->unset_count : &current->environment_count;

            if( (!is_unset && !equals) || (is_unset && equals) ||
                seat_environment_key_refused(value, key_length) )
            {
                answer_error(answer, "%s=%s is refused (a launch-owned or loader key, or not "
                                     "KEY=VALUE)", key, value);
                return -1;
            }
            if( *slot_count == LAUNCH_SESSIONS_SEAT_ENV_MAX )
            {
                answer_error(answer, "seat %d has more than %d %s= lines", current->seat,
                             LAUNCH_SESSIONS_SEAT_ENV_MAX, key);
                return -1;
            }
            if( is_unset )
                current->unset[current->unset_count++] = value;
            else
                current->environment[current->environment_count++] = value;
        }
        else
        {
            answer_error(answer, "launch/spawn does not take %s=", key);
            return -1;
        }
    }
    for( int k = 0; k < count; k++ )
    {
        char const* account = seats[k].account;
        if( !account || !account[0] || strlen(account) >= LAUNCH_ACCOUNT_MAX ||
            strpbrk(account, " \t") || !seats[k].password || !seats[k].password[0] )
        {
            answer_error(answer, "seat %d needs account=<name without spaces> and password=",
                         seats[k].seat);
            return -1;
        }
    }
    if( count == 0 )
        answer_error(answer, "launch/spawn names no seat=");
    return count ? count : -1;
}

static void
verb_spawn(struct LaunchSessions* table, struct LaunchRequest const* request,
           struct LaunchText* answer)
{
    struct LaunchSeatRequest seats[LAUNCH_SESSIONS_PARTY_MAX];
    char const* refusal = NULL;
    struct LaunchSession* session = session_authorised(table, request, &refusal);
    struct LaunchText spawned = { 0 };
    int count;
    int failed_seat = 0;
    int failed_error = 0;

    if( !session )
    {
        answer_error(answer, "%s", refusal);
        return;
    }
    if( session->state != LAUNCH_SESSION_OPEN )
    {
        answer_error(answer, "session %s is %s", session->id, state_name(session->state));
        return;
    }
    session->heartbeat_ms = table_now(table);
    count = spawn_parse_seats(session, request, seats, answer);
    if( count < 0 )
        return;
    text_append(&spawned, "%s", "");
    for( int k = 0; k < count && !failed_seat; k++ )
    {
        int error = member_spawn(table, session, &seats[k]);
        if( error )
        {
            failed_seat = seats[k].seat;
            failed_error = error;
            continue;
        }
        text_append(&spawned, "seat=%d pid=%d directory=%s\n", seats[k].seat,
                    session->seats[seats[k].seat].pid, session->seats[seats[k].seat].directory);
    }
    pids_refresh(table, session);
    if( failed_seat )
        answer_error(answer, "seat %d did not start: %s", failed_seat, strerror(failed_error));
    else
        text_append(answer, "ok\n");
    text_append(answer, "%s", spawned.data);
    free(spawned.data);
}

static int
command_is_known(char const* command)
{
    static char const* const words[] = { "play", "stop", "cheat", "quit", NULL };

    for( int i = 0; words[i]; i++ )
    {
        size_t length = strlen(words[i]);
        if( strncmp(command, words[i], length) == 0 &&
            (command[length] == '\0' || command[length] == ' ') )
            return 1;
    }
    return 0;
}

static void
verb_command(struct LaunchSessions* table, struct LaunchRequest const* request,
             struct LaunchText* answer)
{
    static char const* const allowed[] = { "session", "token", "seat", "command", NULL };
    char const* unknown = request_unknown_key(request, allowed);
    char const* refusal = NULL;
    struct LaunchSession* session = unknown ? NULL : session_authorised(table, request, &refusal);
    char const* seat_text = request_value(request, "seat");
    char const* command = request_value(request, "command");
    long only = 0;
    int queued = 0;

    if( unknown )
    {
        answer_error(answer, "launch/command does not take %s=", unknown);
        return;
    }
    if( !session )
    {
        answer_error(answer, "%s", refusal);
        return;
    }
    if( session->state != LAUNCH_SESSION_OPEN )
    {
        answer_error(answer, "session %s is %s", session->id, state_name(session->state));
        return;
    }
    session->heartbeat_ms = table_now(table);
    if( !command || !command_is_known(command) )
    {
        answer_error(answer, "command= is play {json}, stop, cheat <line> or quit");
        return;
    }
    if( !seat_text ||
        (strcmp(seat_text, "all") != 0 && parse_integer(seat_text, 2, session->size, &only) != 0) )
    {
        answer_error(answer, "seat= is all or 2..%d", session->size);
        return;
    }
    for( int seat = 2; seat <= session->size; seat++ )
    {
        if( (only && seat != only) || !session->seats[seat].spawned )
            continue;
        if( session->seats[seat].mailbox_count == LAUNCH_SESSIONS_MAILBOX_MAX )
        {
            answer_error(answer, "seat %d's mailbox is full (%d unread)", seat,
                         LAUNCH_SESSIONS_MAILBOX_MAX);
            return;
        }
    }
    for( int seat = 2; seat <= session->size; seat++ )
        if( (!only || seat == only) && session->seats[seat].spawned &&
            mailbox_push(&session->seats[seat], command) == 0 )
            queued++;
    if( queued == 0 )
    {
        answer_error(answer, "seat %s has never been spawned", seat_text);
        return;
    }
    text_append(answer, "ok\nqueued=%d\n", queued);
}

/* A member's poll: its seat token, not the session's. */
static void
verb_mail(struct LaunchSessions* table, struct LaunchRequest const* request,
          struct LaunchText* answer)
{
    static char const* const allowed[] = { "session", "seat", "seat_token", "status", NULL };
    char const* unknown = request_unknown_key(request, allowed);
    char const* id = request_value(request, "session");
    char const* status = request_value(request, "status");
    struct LaunchSession* session = NULL;
    struct LaunchSeat* seat;
    long number = 0;

    if( unknown )
    {
        answer_error(answer, "launch/mail does not take %s=", unknown);
        return;
    }
    for( int i = 0; id && i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
        if( table->sessions[i].state != LAUNCH_SESSION_FREE &&
            strcmp(table->sessions[i].id, id) == 0 )
            session = &table->sessions[i];
    if( !session )
    {
        answer_error(answer, "no such session");
        return;
    }
    if( parse_integer(request_value(request, "seat"), 2, session->size, &number) != 0 ||
        !session->seats[number].spawned )
    {
        answer_error(answer, "seat= is not a spawned seat of session %s", session->id);
        return;
    }
    seat = &session->seats[number];
    if( !token_matches(seat->seat_token, request_value(request, "seat_token")) )
    {
        answer_error(answer, "the seat token is wrong or missing");
        return;
    }
    if( session->state == LAUNCH_SESSION_CLOSED )
    {
        answer_error(answer, "session %s is closed: %s", session->id, session->reason);
        return;
    }
    seat->mail_ms = table_now(table);
    if( status )
        snprintf(seat->status, sizeof(seat->status), "%s", status);
    text_append(answer, "ok\n");
    for( int i = 0; i < seat->mailbox_count; i++ )
        text_append(answer, "command=%s\n", seat->mailbox[i]);
    mailbox_clear(seat);
}

static void
verb_status(struct LaunchSessions* table, struct LaunchSession* session,
            struct LaunchText* answer)
{
    int64_t now = table_now(table);

    text_append(answer, "ok\nsession=%s state=%s size=%d port=%d leader=%d process_group=%d "
                        "reason=%s\n",
                session->id, state_name(session->state), session->size, session->port,
                session->leader_pid, session->process_group, session->reason);
    for( int number = 2; number <= session->size; number++ )
    {
        struct LaunchSeat const* seat = &session->seats[number];
        if( !seat->spawned )
        {
            text_append(answer, "seat=%d pid=0 alive=0 exit=- signal=- account=- "
                                "mail_age_ms=- unread=0 status=-\n", number);
            continue;
        }
        text_append(answer, "seat=%d pid=%d alive=%d ", number, seat->pid, seat->alive);
        if( seat->alive )
            text_append(answer, "exit=- signal=- ");
        else if( seat->exit_signal )
            text_append(answer, "exit=- signal=%d ", seat->exit_signal);
        else
            text_append(answer, "exit=%d signal=- ", seat->exit_code);
        text_append(answer, "account=%s ", seat->account);
        if( seat->mail_ms < 0 )
            text_append(answer, "mail_age_ms=- ");
        else
            text_append(answer, "mail_age_ms=%lld ", (long long)(now - seat->mail_ms));
        text_append(answer, "unread=%d status=%s\n", seat->mailbox_count,
                    seat->status[0] ? seat->status : "-");
    }
}

static void
verb_close(struct LaunchSessions* table, struct LaunchSession* session,
           struct LaunchText* answer)
{
    if( session->state == LAUNCH_SESSION_OPEN )
    {
        snprintf(session->reason, sizeof(session->reason), "closed by the leader");
        for( int seat = 2; seat <= session->size; seat++ )
            if( session->seats[seat].spawned && session->seats[seat].alive )
            {
                /* `quit` jumps the queue's limit: a full mailbox still ends. */
                struct LaunchSeat* member = &session->seats[seat];
                if( member->mailbox_count == LAUNCH_SESSIONS_MAILBOX_MAX )
                {
                    member->mailbox_count--;
                    free(member->mailbox[member->mailbox_count]);
                }
                mailbox_push(member, "quit");
            }
        if( session_alive_seats(session) == 0 )
            session_mark_closed(table, session);
        else
        {
            session->state = LAUNCH_SESSION_CLOSING;
            session->deadline_ms = table_now(table) + table->quit_grace_ms;
        }
    }
    text_append(answer, "ok\nstate=%s\n", state_name(session->state));
}

/* ---- the table ---------------------------------------------------------- */

char const*
LaunchSessions_UnsupportedReason(void)
{
#if defined(_WIN32)
    /* ToriRSServer_EmbedPartyListen aborts on Windows: a member spawned there
     * would abort at its first dial. The spawner below is ready for the day
     * the party link runs. */
    return "the party link is not available on this platform";
#else
    return NULL;
#endif
}

struct LaunchSessions*
LaunchSessions_New(struct LaunchSessions_Config const* config)
{
    struct LaunchSessions* table = calloc(1, sizeof(*table));

    assert(table);
    assert(config);
    assert(config->binary_path);
    assert(config->launch_directory);
    assert(config->spawner);
    assert(config->clock_ms);
    assert(strlen(config->binary_path) < sizeof(table->binary_path));
    assert(strlen(config->launch_directory) < sizeof(table->launch_directory));
    snprintf(table->binary_path, sizeof(table->binary_path), "%s", config->binary_path);
    if( config->manifest_path )
    {
        assert(strlen(config->manifest_path) < sizeof(table->manifest_path));
        snprintf(table->manifest_path, sizeof(table->manifest_path), "%s",
                 config->manifest_path);
        table->has_manifest = 1;
    }
    snprintf(table->launch_directory, sizeof(table->launch_directory), "%s",
             config->launch_directory);
    table->spawner = config->spawner;
    table->clock_ms = config->clock_ms;
    table->clock_context = config->clock_context;
    table->base_environment = config->base_environment;
    table->heartbeat_timeout_ms = config->heartbeat_timeout_ms > 0
                                      ? config->heartbeat_timeout_ms
                                      : LAUNCH_HEARTBEAT_TIMEOUT_MS_DEFAULT;
    table->quit_grace_ms =
        config->quit_grace_ms > 0 ? config->quit_grace_ms : LAUNCH_QUIT_GRACE_MS_DEFAULT;
    table->kill_grace_ms =
        config->kill_grace_ms > 0 ? config->kill_grace_ms : LAUNCH_KILL_GRACE_MS_DEFAULT;
    return table;
}

char*
LaunchSessions_Answer(
    struct LaunchSessions* table,
    char const* verb,
    char const* body,
    int body_size,
    char const* peer,
    int* out_size)
{
    static char const* const leader_keys[] = { "session", "token", NULL };
    struct LaunchText answer = { 0 };
    struct LaunchRequest request;
    char const* reason = LaunchSessions_UnsupportedReason();
    char const* refusal;

    assert(table);
    assert(verb);
    assert(peer);
    assert(out_size);
    assert(body_size >= 0);
    if( body_size > 0 )
        assert(body);
    memset(&request, 0, sizeof(request));
    if( reason )
        text_append(&answer, "unsupported: %s\n", reason);
    else if( !peer_is_loopback(peer) )
        answer_error(&answer, "launch requests are served to this machine only, not %s", peer);
    else if( body_size > LAUNCH_SESSIONS_BODY_MAX )
        answer_error(&answer, "the request is %d bytes; the limit is %d", body_size,
                     LAUNCH_SESSIONS_BODY_MAX);
    else if( (refusal = request_parse(&request, body, body_size)) != NULL )
        answer_error(&answer, "%s", refusal);
    else if( strcmp(verb, "open") == 0 )
        verb_open(table, &request, &answer);
    else if( strcmp(verb, "spawn") == 0 )
        verb_spawn(table, &request, &answer);
    else if( strcmp(verb, "command") == 0 )
        verb_command(table, &request, &answer);
    else if( strcmp(verb, "mail") == 0 )
        verb_mail(table, &request, &answer);
    else if( strcmp(verb, "status") == 0 || strcmp(verb, "heartbeat") == 0 ||
             strcmp(verb, "close") == 0 )
    {
        char const* unknown = request_unknown_key(&request, leader_keys);
        struct LaunchSession* session =
            unknown ? NULL : session_authorised(table, &request, &refusal);

        if( unknown )
            answer_error(&answer, "launch/%s does not take %s=", verb, unknown);
        else if( !session )
            answer_error(&answer, "%s", refusal);
        else
        {
            if( session->state == LAUNCH_SESSION_OPEN )
                session->heartbeat_ms = table_now(table);
            if( strcmp(verb, "status") == 0 )
                verb_status(table, session, &answer);
            else if( strcmp(verb, "close") == 0 )
                verb_close(table, session, &answer);
            else
                text_append(&answer, "ok\nstate=%s\n", state_name(session->state));
        }
    }
    else
        answer_error(&answer, "no launch verb %s (open, spawn, command, mail, status, "
                              "heartbeat, close)", verb);
    free(request.copy);
    *out_size = answer.size;
    return answer.data;
}

int
LaunchSessions_Reap(struct LaunchSessions* table)
{
    struct LaunchSessions_Spawner const* spawner;
    int live = 0;

    assert(table);
    spawner = table->spawner;
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
    {
        struct LaunchSession* session = &table->sessions[i];
        int64_t now = table_now(table);
        char reason[LAUNCH_REASON_MAX];

        if( session->state == LAUNCH_SESSION_FREE || session->state == LAUNCH_SESSION_CLOSED )
            continue;
        if( session_reap_members(table, session, 0) )
            pids_refresh(table, session);
        if( session->state == LAUNCH_SESSION_OPEN )
        {
            /* The pid is the primary signal; the heartbeat covers a leader
             * that is alive but no longer driving (a stalled frame loop gets
             * the whole timeout). */
            if( !spawner->process_alive(spawner->context, session->leader_pid) )
            {
                snprintf(reason, sizeof(reason), "the leader (pid %d) is gone",
                         session->leader_pid);
                session_terminate(table, session, reason);
            }
            else if( now - session->heartbeat_ms > table->heartbeat_timeout_ms )
            {
                snprintf(reason, sizeof(reason), "no heartbeat from the leader for %lld ms",
                         (long long)(now - session->heartbeat_ms));
                session_terminate(table, session, reason);
            }
        }
        else if( session->state == LAUNCH_SESSION_CLOSING )
        {
            if( session_alive_seats(session) == 0 )
                session_mark_closed(table, session);
            else if( now >= session->deadline_ms )
                session_terminate(table, session, NULL);
        }
        else if( session->state == LAUNCH_SESSION_TERMINATING )
        {
            if( session_alive_seats(session) == 0 )
                session_mark_closed(table, session);
            else if( now >= session->deadline_ms && !session->killed )
            {
                fprintf(stderr, "launch: session %s: members outlived the grace: SIGKILL\n",
                        session->id);
                session_signal(table, session, LAUNCH_SIGNAL_KILL);
                session->killed = 1;
            }
        }
        if( session->state != LAUNCH_SESSION_CLOSED && session_alive_seats(session) > 0 )
            live++;
    }
    return live;
}

static void
sleep_milliseconds(int milliseconds)
{
#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    struct timespec wait = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };
    nanosleep(&wait, NULL);
#endif
}

/* The host's own exit: no member outlives the client that started it. */
void
LaunchSessions_Free(struct LaunchSessions* table)
{
    int live = 0;

    if( !table )
        return;
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
    {
        struct LaunchSession* session = &table->sessions[i];
        if( session->state == LAUNCH_SESSION_FREE || session->state == LAUNCH_SESSION_CLOSED )
            continue;
        session_reap_members(table, session, 0);
        if( session_alive_seats(session) == 0 )
            continue;
        snprintf(session->reason, sizeof(session->reason), "the host is exiting");
        session_signal(table, session, LAUNCH_SIGNAL_TERMINATE);
        live++;
    }
    /* Up to a second for a clean exit, then SIGKILL and a blocking reap. */
    for( int waited = 0; live && waited < 1000; waited += 50 )
    {
        live = 0;
        sleep_milliseconds(50);
        for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
        {
            struct LaunchSession* session = &table->sessions[i];
            if( session->state == LAUNCH_SESSION_FREE || session->state == LAUNCH_SESSION_CLOSED )
                continue;
            session_reap_members(table, session, 0);
            live += session_alive_seats(session);
        }
    }
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
    {
        struct LaunchSession* session = &table->sessions[i];
        if( session->state == LAUNCH_SESSION_FREE || session->state == LAUNCH_SESSION_CLOSED )
            continue;
        if( session_alive_seats(session) > 0 )
        {
            session_signal(table, session, LAUNCH_SIGNAL_KILL);
            session_reap_members(table, session, 1);
        }
        if( strcmp(session->reason, "-") == 0 )
            snprintf(session->reason, sizeof(session->reason), "the host is exiting");
        session_mark_closed(table, session);
    }
    free(table);
}

#if !defined(_WIN32)

/* ---- POSIX: posix_spawn into the session's process group ---------------- */

static int
system_spawn(void* context, char* const* argv, char* const* environment, char const* log_path,
             int process_group, int* out_pid, int* out_process_group)
{
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    sigset_t defaults;
    sigset_t mask;
    short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
    pid_t pid = 0;
    int result;

    (void)context;
    assert(argv);
    assert(argv[0]);
    assert(environment);
    assert(log_path);
    assert(out_pid);
    assert(out_process_group);
    result = posix_spawn_file_actions_init(&actions);
    assert(result == 0);
    result = posix_spawnattr_init(&attributes);
    assert(result == 0);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    /* The leader's sockets (the party listener above all) stay the leader's. */
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attributes, flags);
    posix_spawnattr_setpgroup(&attributes, (pid_t)process_group);
    /* A client ignores SIGPIPE; its members start from the defaults, as a
     * member run.py starts does, so SIGTERM ends them. */
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGHUP);
    sigaddset(&defaults, SIGQUIT);
    sigaddset(&defaults, SIGCHLD);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    result = posix_spawn(&pid, argv[0], &actions, &attributes, argv, environment);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if( result != 0 )
        return result;
    *out_pid = (int)pid;
    *out_process_group = process_group > 0 ? process_group : (int)pid;
    return 0;
}

static int
system_signal_process(void* context, int pid, int signal_number)
{
    (void)context;
    assert(pid > 0);
    return kill((pid_t)pid, signal_number) == 0 ? 0 : errno;
}

static int
system_signal_group(void* context, int process_group, int signal_number)
{
    (void)context;
    assert(process_group > 1);
    return killpg((pid_t)process_group, signal_number) == 0 ? 0 : errno;
}

static int
system_process_alive(void* context, int pid)
{
    (void)context;
    assert(pid > 0);
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

static int
system_reap_child(void* context, int pid, int block, int* out_exit_code, int* out_signal)
{
    int status = 0;
    pid_t result;

    (void)context;
    assert(pid > 0);
    assert(out_exit_code);
    assert(out_signal);
    do
        result = waitpid((pid_t)pid, &status, block ? 0 : WNOHANG);
    while( result < 0 && errno == EINTR );
    if( result == 0 )
        return 0;
    if( result < 0 )
    {
        /* Not our child (or reaped elsewhere): ended when it no longer
         * exists. */
        if( system_process_alive(context, pid) )
            return 0;
        *out_exit_code = -1;
        *out_signal = 0;
        return 1;
    }
    *out_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    *out_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return 1;
}

static struct LaunchSessions_Spawner const g_system_spawner = {
    NULL,
    system_spawn,
    system_signal_process,
    system_signal_group,
    system_process_alive,
    system_reap_child,
};

struct LaunchSessions_Spawner const*
LaunchSessions_SystemSpawner(void)
{
    return &g_system_spawner;
}

int64_t
LaunchSessions_MonotonicMs(void* context)
{
    struct timespec now;

    (void)context;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int
LaunchSessions_OwnBinaryPath(char const* argv0, char* out, int out_size)
{
    char found[PATH_MAX];
    char resolved[PATH_MAX];
    int have = 0;

    assert(argv0);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
#if defined(__APPLE__)
    {
        uint32_t size = sizeof(found);
        have = _NSGetExecutablePath(found, &size) == 0;
    }
#elif defined(__linux__)
    {
        ssize_t length = readlink("/proc/self/exe", found, sizeof(found) - 1);
        if( length > 0 )
        {
            found[length] = '\0';
            have = 1;
        }
    }
#endif
    if( !have && strchr(argv0, '/') && strlen(argv0) < sizeof(found) )
    {
        snprintf(found, sizeof(found), "%s", argv0);
        have = 1;
    }
    if( !have || !realpath(found, resolved) || (int)strlen(resolved) >= out_size )
        return -1;
    snprintf(out, (size_t)out_size, "%s", resolved);
    return 0;
}

#endif /* !_WIN32 */

#if defined(_WIN32)

/* ---- Windows: CreateProcess in a job object per session ------------------
 * Written for the day the party link runs on Windows; until then
 * LaunchSessions_UnsupportedReason answers every request before a spawn. The
 * job is the process group: its id is the first member's pid, and closing or
 * terminating it ends every member (KILL_ON_JOB_CLOSE also ends them when the
 * host dies, which is what POSIX needs the members' watchdog for). */

enum
{
    LAUNCH_WINDOWS_SLOTS = LAUNCH_SESSIONS_SESSION_MAX * LAUNCH_SESSIONS_PARTY_MAX,
};

struct LaunchWindowsChild
{
    int pid;
    HANDLE process;
};

struct LaunchWindowsJob
{
    int group;
    HANDLE job;
};

static struct LaunchWindowsChild g_windows_children[LAUNCH_WINDOWS_SLOTS];
static struct LaunchWindowsJob g_windows_jobs[LAUNCH_SESSIONS_SESSION_MAX];

static void
windows_quote(struct LaunchText* line, char const* argument)
{
    text_append(line, "%s\"", line->size ? " " : "");
    for( char const* c = argument; *c; c++ )
        text_append(line, *c == '"' ? "\\\"" : "%c", *c);
    text_append(line, "\"");
}

static int
system_spawn(void* context, char* const* argv, char* const* environment, char const* log_path,
             int process_group, int* out_pid, int* out_process_group)
{
    struct LaunchText command_line = { 0 };
    struct LaunchText block = { 0 };
    SECURITY_ATTRIBUTES inherit = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    STARTUPINFOA startup = { 0 };
    PROCESS_INFORMATION process = { 0 };
    struct LaunchWindowsJob* job = NULL;
    struct LaunchWindowsChild* child = NULL;
    HANDLE log;
    BOOL started;

    (void)context;
    assert(argv);
    assert(environment);
    assert(log_path);
    for( int i = 0; argv[i]; i++ )
        windows_quote(&command_line, argv[i]);
    for( int i = 0; environment[i]; i++ )
    {
        text_append(&block, "%s", environment[i]);
        text_append(&block, "%c", '\0');
    }
    text_append(&block, "%c", '\0');
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
        if( process_group > 0 ? g_windows_jobs[i].group == process_group
                              : (!job && g_windows_jobs[i].group == 0) )
            job = &g_windows_jobs[i];
    for( int i = 0; i < LAUNCH_WINDOWS_SLOTS && !child; i++ )
        if( g_windows_children[i].pid == 0 )
            child = &g_windows_children[i];
    if( !job || !child )
    {
        free(command_line.data);
        free(block.data);
        return process_group > 0 ? ESRCH : EAGAIN;
    }
    log = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = NULL;
    startup.hStdOutput = log;
    startup.hStdError = log;
    started = CreateProcessA(argv[0], command_line.data, NULL, NULL, TRUE,
                             CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
                             block.data, NULL, &startup, &process);
    if( log != INVALID_HANDLE_VALUE )
        CloseHandle(log);
    free(command_line.data);
    free(block.data);
    if( !started )
        return EIO;
    if( !job->job )
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = { 0 };
        job->job = CreateJobObjectA(NULL, NULL);
        assert(job->job);
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job->job, JobObjectExtendedLimitInformation, &limits,
                                sizeof(limits));
        job->group = (int)process.dwProcessId;
    }
    AssignProcessToJobObject(job->job, process.hProcess);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    child->pid = (int)process.dwProcessId;
    child->process = process.hProcess;
    *out_pid = child->pid;
    *out_process_group = job->group;
    return 0;
}

static struct LaunchWindowsChild*
windows_child(int pid)
{
    for( int i = 0; i < LAUNCH_WINDOWS_SLOTS; i++ )
        if( g_windows_children[i].pid == pid )
            return &g_windows_children[i];
    return NULL;
}

static int
system_signal_process(void* context, int pid, int signal_number)
{
    struct LaunchWindowsChild* child = windows_child(pid);

    (void)context;
    (void)signal_number;
    if( !child )
        return ESRCH;
    return TerminateProcess(child->process, 1) ? 0 : EIO;
}

static int
system_signal_group(void* context, int process_group, int signal_number)
{
    (void)context;
    (void)signal_number;
    for( int i = 0; i < LAUNCH_SESSIONS_SESSION_MAX; i++ )
        if( g_windows_jobs[i].group == process_group && g_windows_jobs[i].job )
            return TerminateJobObject(g_windows_jobs[i].job, 1) ? 0 : EIO;
    return ESRCH;
}

static int
system_process_alive(void* context, int pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    DWORD code = 0;
    int alive;

    (void)context;
    if( !process )
        return 0;
    alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

static int
system_reap_child(void* context, int pid, int block, int* out_exit_code, int* out_signal)
{
    struct LaunchWindowsChild* child = windows_child(pid);
    DWORD code = 0;

    (void)context;
    assert(out_exit_code);
    assert(out_signal);
    if( !child )
    {
        *out_exit_code = -1;
        *out_signal = 0;
        return !system_process_alive(context, pid);
    }
    if( WaitForSingleObject(child->process, block ? INFINITE : 0) != WAIT_OBJECT_0 )
        return 0;
    GetExitCodeProcess(child->process, &code);
    CloseHandle(child->process);
    memset(child, 0, sizeof(*child));
    *out_exit_code = (int)code;
    *out_signal = 0;
    return 1;
}

static struct LaunchSessions_Spawner const g_system_spawner = {
    NULL,
    system_spawn,
    system_signal_process,
    system_signal_group,
    system_process_alive,
    system_reap_child,
};

struct LaunchSessions_Spawner const*
LaunchSessions_SystemSpawner(void)
{
    return &g_system_spawner;
}

int64_t
LaunchSessions_MonotonicMs(void* context)
{
    (void)context;
    return (int64_t)GetTickCount64();
}

int
LaunchSessions_OwnBinaryPath(char const* argv0, char* out, int out_size)
{
    DWORD length;

    assert(argv0);
    assert(out);
    assert(out_size > 0);
    length = GetModuleFileNameA(NULL, out, (DWORD)out_size);
    if( length == 0 || (int)length >= out_size )
    {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

#endif /* _WIN32 */

#endif /* LAUNCH_SESSIONS_DESKTOP */
