/*
 * torirsd: every server of one deployment, from one call -- up together, down
 * together.
 *
 *   torirsd [torirsd.ini]          start them; run until stopped
 *   torirsd --check [torirsd.ini]  print what would run, in order, and exit
 *
 * The five servers (docs/WEB_SERVERS.md) are separate processes on purpose --
 * a crash in the website cannot corrupt the world -- but they are one
 * deployment: none is useful alone, and a set with one missing is a set that
 * misbehaves quietly (a world with no account server refuses every login). So
 * this one process owns them:
 *
 *   **Up in order.** Each `[server.<name>]`, in the file's order, is started
 *   and must answer on its `ready` port before the next starts: the account
 *   and hiscores servers before the IO server before the website before the
 *   game, so nothing comes up asking a server that is not there yet. One that
 *   exits or does not answer within `start_timeout_ms` fails the start.
 *
 *   **Down in reverse.** On SIGTERM or SIGINT (Ctrl-C, `systemctl stop`) each
 *   is stopped and waited for in the opposite order -- the game first, so its
 *   logouts hand the characters' saves back to the account server while that
 *   server is still there to take them; the account server last. One that
 *   does not exit within `stop_timeout_ms` is killed.
 *
 *   **Down together.** When any server exits on its own, the rest are stopped
 *   the same way and torirsd exits 1, so whatever runs it (systemd's
 *   Restart=always, supervise.ps1) brings the whole set back up -- never a
 *   half-set with one server restarted under the others.
 *
 * Each server's stdout and stderr append to `<logs>/<name>.log`; torirsd's
 * own lines go to its stderr (the journal, under systemd). Each child runs in
 * its own process group, so a Ctrl-C on the terminal reaches torirsd alone and
 * the order above holds; under systemd the unit must say `KillMode=mixed` for
 * the same reason (systemd otherwise signals every process at once).
 *
 * The file:
 *
 *   [torirsd]
 *   logs = logs                  where the servers' logs go
 *   start_timeout_ms = 120000    per server; the game loads its content
 *   stop_timeout_ms = 60000      per server; the game saves everyone
 *
 *   [env]
 *   TORIRSSERVER_SAVES = saves   set for every server
 *
 *   [server.account]             one per server, started in this order
 *   run = bin/account_server
 *   args = --port 8091           split at spaces; "double quotes" keep one
 *   ready = 8091                 127.0.0.1:<port> answers once it is up
 *                                (0 or none: up once it is running)
 *   env = KEY=VALUE              for this server only; repeatable
 */
#include "3rd/ini/ini.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define TORIRSD_MAX_SERVERS 16
#define TORIRSD_MAX_ARGS 48
#define TORIRSD_MAX_ENV 16
#define TORIRSD_CONFIG_MAX (64 * 1024)
/* The INI reader's own limit on one value. */
#define TORIRSD_VALUE_MAX 512

struct TorirsdServer
{
    char name[48];
    char run[512];
    char args[1024];
    int ready_port;
    char env[TORIRSD_MAX_ENV][512];
    int env_count;
    int started;
    int exited;
#ifdef _WIN32
    HANDLE process;
    DWORD pid;
#else
    pid_t pid;
#endif
};

static struct
{
    char logs[512];
    long start_timeout_ms;
    long stop_timeout_ms;
    char env[TORIRSD_MAX_ENV * 2][512];
    int env_count;
    struct TorirsdServer servers[TORIRSD_MAX_SERVERS];
    int count;
} g_config;

static volatile sig_atomic_t g_stop;

/* ------------------------------------------------------------------ */
/* Small things                                                        */
/* ------------------------------------------------------------------ */

static long
now_ms(void)
{
#ifdef _WIN32
    return (long)GetTickCount64();
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
#endif
}

static void
sleep_ms(long ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    /* A signal ends it early, which is what a stop wants. */
    nanosleep(&ts, NULL);
#endif
}

static void
say(char const* fmt, ...)
{
    va_list args;
    time_t t = time(NULL);
    char stamp[32];

    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(stderr, "torirsd %s: ", stamp);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

/* `KEY=VALUE` into this process's environment, which every child inherits. */
static int
env_put(char const* pair)
{
    char key[256];
    char const* eq = strchr(pair, '=');

    if( !eq || eq == pair || (size_t)(eq - pair) >= sizeof(key) )
        return 0;
    snprintf(key, sizeof(key), "%.*s", (int)(eq - pair), pair);
#ifdef _WIN32
    return SetEnvironmentVariableA(key, eq + 1) != 0;
#else
    return setenv(key, eq + 1, 1) == 0;
#endif
}

/* `line` split at spaces into `argv` (in place); "double quotes" keep spaces
 * and are dropped. The count. */
static int
split_args(
    char* line,
    char** argv,
    int max)
{
    int count = 0;
    char* in = line;

    while( *in && count < max )
    {
        char* out;

        while( *in == ' ' || *in == '\t' )
            in++;
        if( !*in )
            break;
        argv[count++] = out = in;
        while( *in && *in != ' ' && *in != '\t' )
        {
            if( *in == '"' )
            {
                in++;
                while( *in && *in != '"' )
                    *out++ = *in++;
                if( *in == '"' )
                    in++;
                continue;
            }
            *out++ = *in++;
        }
        if( *in )
            in++;
        *out = '\0';
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* The file                                                            */
/* ------------------------------------------------------------------ */

/* `src` without its leading and trailing blanks: the INI reader hands a key
 * and its value as they sit around the '=' ("run " / " bin/x"). */
static void
trimmed(
    char* dst,
    size_t cap,
    char const* src)
{
    size_t len;

    while( *src == ' ' || *src == '\t' )
        src++;
    len = strlen(src);
    while( len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r') )
        len--;
    if( len >= cap )
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int
config_load(char const* path)
{
    static uint8_t data[TORIRSD_CONFIG_MAX];
    FILE* file = fopen(path, "rb");
    size_t size;
    struct INIReader reader;
    struct INIElement element;
    char section[64] = "";
    struct TorirsdServer* server = NULL;
    int rc;

    if( !file )
    {
        say("cannot open %s: %s", path, strerror(errno));
        return 0;
    }
    size = fread(data, 1, sizeof(data), file);
    fclose(file);
    if( size == sizeof(data) )
    {
        say("%s is larger than %d bytes", path, TORIRSD_CONFIG_MAX);
        return 0;
    }
    memset(&g_config, 0, sizeof(g_config));
    snprintf(g_config.logs, sizeof(g_config.logs), "logs");
    g_config.start_timeout_ms = 120000;
    g_config.stop_timeout_ms = 60000;

    ini_reader_init(&reader);
    while( (rc = ini_reader_next(&reader, data, (uint32_t)size, &element)) == TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            snprintf(section, sizeof(section), "%s", element._section.name);
            server = NULL;
            if( strncmp(section, "server.", 7) == 0 )
            {
                if( g_config.count == TORIRSD_MAX_SERVERS )
                {
                    say("%s: more than %d servers", path, TORIRSD_MAX_SERVERS);
                    return 0;
                }
                server = &g_config.servers[g_config.count++];
                memset(server, 0, sizeof(*server));
                snprintf(server->name, sizeof(server->name), "%s", section + 7);
            }
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL )
            continue;
        {
            char key[64];
            char value[TORIRSD_VALUE_MAX];
            int known = 1;

            trimmed(key, sizeof(key), element._keyval.name);
            trimmed(value, sizeof(value), element._keyval.value);
            if( strcmp(section, "torirsd") == 0 )
            {
                if( strcmp(key, "logs") == 0 )
                    snprintf(g_config.logs, sizeof(g_config.logs), "%s", value);
                else if( strcmp(key, "start_timeout_ms") == 0 )
                    g_config.start_timeout_ms = atol(value);
                else if( strcmp(key, "stop_timeout_ms") == 0 )
                    g_config.stop_timeout_ms = atol(value);
                else
                    known = 0;
            }
            else if( strcmp(section, "env") == 0 )
            {
                if( g_config.env_count == (int)(sizeof(g_config.env) / sizeof(g_config.env[0])) )
                {
                    say("%s: [env] has more than %d variables", path, g_config.env_count);
                    return 0;
                }
                snprintf(g_config.env[g_config.env_count++], sizeof(g_config.env[0]), "%s=%s",
                         key, value);
            }
            else if( server )
            {
                if( strcmp(key, "run") == 0 )
                    snprintf(server->run, sizeof(server->run), "%s", value);
                else if( strcmp(key, "args") == 0 )
                    snprintf(server->args, sizeof(server->args), "%s", value);
                else if( strcmp(key, "ready") == 0 )
                    server->ready_port = atoi(value);
                else if( strcmp(key, "env") == 0 )
                {
                    if( server->env_count == TORIRSD_MAX_ENV )
                    {
                        say("%s: [server.%s] has more than %d env =", path, server->name,
                            TORIRSD_MAX_ENV);
                        return 0;
                    }
                    snprintf(server->env[server->env_count++], sizeof(server->env[0]), "%s",
                             value);
                }
                else
                    known = 0;
            }
            else
                known = 0;
            if( !known )
            {
                say("%s: [%s] has no setting called '%s'", path, section, key);
                return 0;
            }
        }
    }
    if( rc != TORI_INI_ERR_NONE )
    {
        say("%s does not read as INI (%d)", path, rc);
        return 0;
    }
    if( g_config.count == 0 )
    {
        say("%s names no [server.<name>]", path);
        return 0;
    }
    for( int i = 0; i < g_config.count; i++ )
    {
        if( !g_config.servers[i].run[0] )
        {
            say("%s: [server.%s] has no run =", path, g_config.servers[i].name);
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Is it up                                                            */
/* ------------------------------------------------------------------ */

static int
port_answers(int port)
{
    struct sockaddr_in addr;
    int ok;
#ifdef _WIN32
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);

    if( fd == INVALID_SOCKET )
        return 0;
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if( fd < 0 )
        return 0;
#endif
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return ok;
}

/* ------------------------------------------------------------------ */
/* Processes                                                           */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

static int
server_spawn(struct TorirsdServer* s)
{
    char command[2048];
    char line[1024];
    char* argv[TORIRSD_MAX_ARGS];
    int argc;
    char log_path[1024];
    SECURITY_ATTRIBUTES inherit = { sizeof(inherit), NULL, TRUE };
    HANDLE log;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    BOOL made;

    snprintf(line, sizeof(line), "%s", s->args);
    argc = split_args(line, argv, TORIRSD_MAX_ARGS);
    snprintf(command, sizeof(command), "\"%s\"", s->run);
    for( int i = 0; i < argc; i++ )
        snprintf(command + strlen(command), sizeof(command) - strlen(command), " \"%s\"",
                 argv[i]);
    snprintf(log_path, sizeof(log_path), "%s\\%s.log", g_config.logs, s->name);
    log = CreateFileA(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if( log == INVALID_HANDLE_VALUE )
    {
        say("%s: cannot open %s", s->name, log_path);
        return 0;
    }
    for( int i = 0; i < s->env_count; i++ )
        env_put(s->env[i]);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    made = CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL,
                          &si, &pi);
    /* This server's own variables were for it alone. */
    for( int i = 0; i < s->env_count; i++ )
    {
        char key[256];
        char const* eq = strchr(s->env[i], '=');

        if( eq && (size_t)(eq - s->env[i]) < sizeof(key) )
        {
            snprintf(key, sizeof(key), "%.*s", (int)(eq - s->env[i]), s->env[i]);
            SetEnvironmentVariableA(key, NULL);
        }
    }
    CloseHandle(log);
    if( !made )
    {
        say("%s: cannot start %s (%lu)", s->name, s->run, (unsigned long)GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    s->process = pi.hProcess;
    s->pid = pi.dwProcessId;
    s->started = 1;
    s->exited = 0;
    return 1;
}

/* 1 when `s` has exited (now or before), its code into `*code`. */
static int
server_reap(
    struct TorirsdServer* s,
    int* code)
{
    DWORD status = 0;

    if( !s->started || s->exited )
        return 1;
    if( WaitForSingleObject(s->process, 0) != WAIT_OBJECT_0 )
        return 0;
    GetExitCodeProcess(s->process, &status);
    CloseHandle(s->process);
    s->exited = 1;
    *code = (int)status;
    return 1;
}

static void
server_signal(
    struct TorirsdServer* s,
    int hard)
{
    if( hard )
        TerminateProcess(s->process, 1);
    else
        /* The child's own process group: CTRL_BREAK reaches it alone (SIGBREAK
         * in its C runtime, which each server stops on). */
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, s->pid);
}

static BOOL WINAPI
on_console(DWORD event)
{
    (void)event;
    g_stop = 1;
    return TRUE;
}

#else

static int
server_spawn(struct TorirsdServer* s)
{
    char line[1024];
    char* argv[TORIRSD_MAX_ARGS + 2];
    int argc;
    char log_path[1024];
    pid_t pid;

    snprintf(line, sizeof(line), "%s", s->args);
    argv[0] = s->run;
    argc = 1 + split_args(line, argv + 1, TORIRSD_MAX_ARGS);
    argv[argc] = NULL;
    snprintf(log_path, sizeof(log_path), "%s/%s.log", g_config.logs, s->name);
    pid = fork();
    if( pid < 0 )
    {
        say("%s: cannot fork: %s", s->name, strerror(errno));
        return 0;
    }
    if( pid == 0 )
    {
        int log = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);

        /* Its own process group: a terminal's Ctrl-C reaches torirsd alone,
         * which stops the servers in their order. */
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        if( log >= 0 )
        {
            dup2(log, STDOUT_FILENO);
            dup2(log, STDERR_FILENO);
            close(log);
        }
        for( int i = 0; i < s->env_count; i++ )
            env_put(s->env[i]);
        execv(s->run, argv);
        fprintf(stderr, "torirsd: cannot run %s: %s\n", s->run, strerror(errno));
        _exit(127);
    }
    s->pid = pid;
    s->started = 1;
    s->exited = 0;
    return 1;
}

static int
server_reap(
    struct TorirsdServer* s,
    int* code)
{
    int status = 0;
    pid_t got;

    if( !s->started || s->exited )
        return 1;
    got = waitpid(s->pid, &status, WNOHANG);
    if( got == 0 )
        return 0;
    s->exited = 1;
    if( got < 0 )
        *code = -1;
    else if( WIFEXITED(status) )
        *code = WEXITSTATUS(status);
    else if( WIFSIGNALED(status) )
        *code = 128 + WTERMSIG(status);
    else
        *code = -1;
    return 1;
}

static void
server_signal(
    struct TorirsdServer* s,
    int hard)
{
    kill(s->pid, hard ? SIGKILL : SIGTERM);
}

static void
on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

#endif

/* ------------------------------------------------------------------ */
/* Up and down                                                         */
/* ------------------------------------------------------------------ */

/* Stop every started server, last first, each waited for. */
static void
stop_all(void)
{
    for( int i = g_config.count - 1; i >= 0; i-- )
    {
        struct TorirsdServer* s = &g_config.servers[i];
        long deadline;
        int code = 0;

        if( !s->started || server_reap(s, &code) )
            continue;
        say("stopping %s", s->name);
        server_signal(s, 0);
        deadline = now_ms() + g_config.stop_timeout_ms;
        while( !server_reap(s, &code) && now_ms() < deadline )
            sleep_ms(100);
        if( !s->exited )
        {
            say("%s did not stop within %ld ms; killed", s->name, g_config.stop_timeout_ms);
            server_signal(s, 1);
            while( !server_reap(s, &code) )
                sleep_ms(50);
        }
        else
            say("%s stopped (%d)", s->name, code);
    }
}

/* Start `s` and wait for it to answer. 0 when it did not come up. */
static int
start_one(struct TorirsdServer* s)
{
    long deadline;
    int code = 0;

    say("starting %s: %s %s", s->name, s->run, s->args);
    if( !server_spawn(s) )
        return 0;
    if( s->ready_port <= 0 )
        return 1;
    deadline = now_ms() + g_config.start_timeout_ms;
    while( now_ms() < deadline && !g_stop )
    {
        if( server_reap(s, &code) )
        {
            say("%s exited (%d) before it answered on %d; see %s/%s.log", s->name, code,
                s->ready_port, g_config.logs, s->name);
            return 0;
        }
        if( port_answers(s->ready_port) )
        {
            say("%s is up on %d", s->name, s->ready_port);
            return 1;
        }
        sleep_ms(200);
    }
    if( !g_stop )
        say("%s did not answer on %d within %ld ms", s->name, s->ready_port,
            g_config.start_timeout_ms);
    return 0;
}

static void
print_plan(char const* path)
{
    printf("%s: logs in %s; start %ld ms, stop %ld ms per server\n", path, g_config.logs,
           g_config.start_timeout_ms, g_config.stop_timeout_ms);
    for( int i = 0; i < g_config.env_count; i++ )
        printf("  env   %s\n", g_config.env[i]);
    for( int i = 0; i < g_config.count; i++ )
    {
        struct TorirsdServer const* s = &g_config.servers[i];

        printf("  %d. %-10s %s %s", i + 1, s->name, s->run, s->args);
        if( s->ready_port > 0 )
            printf("   (up when :%d answers)", s->ready_port);
        printf("\n");
        for( int e = 0; e < s->env_count; e++ )
            printf("              env %s\n", s->env[e]);
    }
    printf("  stop order: the reverse\n");
}

int
main(
    int argc,
    char** argv)
{
    char const* path = "torirsd.ini";
    int check = 0;
    int failed = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--check") == 0 )
            check = 1;
        else if( argv[i][0] != '-' )
            path = argv[i];
        else
        {
            fprintf(stderr, "usage: %s [--check] [torirsd.ini]\n", argv[0]);
            return 2;
        }
    }
    if( !config_load(path) )
        return 2;
    if( check )
    {
        print_plan(path);
        return 0;
    }
    for( int i = 0; i < g_config.env_count; i++ )
        if( !env_put(g_config.env[i]) )
            say("[env] %s is not KEY=VALUE; skipped", g_config.env[i]);
#ifdef _WIN32
    {
        WSADATA wsa;

        WSAStartup(MAKEWORD(2, 2), &wsa);
        CreateDirectoryA(g_config.logs, NULL);
        SetConsoleCtrlHandler(on_console, TRUE);
    }
#else
    mkdir(g_config.logs, 0755);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
#endif

    /* "Up" is "its port answers": a port something already answers on would
     * pass a server that never bound it -- another torirsd on the same file,
     * or one of a crashed set's servers still running. Refuse before
     * anything starts. */
    for( int i = 0; i < g_config.count; i++ )
    {
        struct TorirsdServer const* s = &g_config.servers[i];

        if( s->ready_port > 0 && port_answers(s->ready_port) )
        {
            say("%s's port %d already answers -- another torirsd, or a server left from an "
                "earlier one, is running; nothing started",
                s->name, s->ready_port);
            return 1;
        }
    }

    say("%d server(s) from %s", g_config.count, path);
    for( int i = 0; i < g_config.count && !g_stop; i++ )
    {
        if( !start_one(&g_config.servers[i]) )
        {
            failed = 1;
            break;
        }
    }
    if( !failed && !g_stop )
        say("all %d up", g_config.count);

    /* Until a stop, or any server leaving on its own. */
    while( !failed && !g_stop )
    {
        for( int i = 0; i < g_config.count; i++ )
        {
            int code = 0;

            if( server_reap(&g_config.servers[i], &code) )
            {
                say("%s exited (%d); stopping the rest", g_config.servers[i].name, code);
                failed = 1;
                break;
            }
        }
        if( !failed )
            sleep_ms(500);
    }
    if( g_stop && !failed )
        say("stop asked; stopping in reverse order");
    stop_all();
    say(failed ? "down (a server failed)" : "down");
    return failed ? 1 : 0;
}
