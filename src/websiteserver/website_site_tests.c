/*
 * Staff: watch a quest test (`/admin/tests`).
 *
 * Compiled INTO website_site.c (included above its routes, after the player
 * editor): it is that file's pages and uses its page frame, buffers, staff
 * test, recording names and the editor's JSON helpers. Never linked alone.
 *
 * The browser's client cannot play a test itself: the quest driver needs the
 * in-process world (its cheats, the server's own readers, a save written from
 * the test's fixture), and the web build links no server. So the test is
 * played HERE, on this machine, by the server's own runner -- one
 * `torirsserver --scriptrun` (torirs_server_scriptrun.c) on a throwaway
 * character made from the test's fixture, in a world of its own -- and
 * recorded (`--record`): every packet the character was sent, stamped 600 ms
 * a tick. The run takes seconds; watching it is the replay page
 * (/admin/replay), at the recording's own pace or faster.
 *
 *   GET  /admin/tests              the quest tests, the run in progress or the
 *                                  last one's steps, the recent test recordings
 *   POST /admin/tests/run  id=..   start that test (one run at a time)
 *   POST /admin/tests/stop         end the run in progress
 *   GET  /admin/tests/status       {"state","id","title","secs","rows","last",
 *                                   "result","watch"} for the page's poll
 *
 * What it reads, all relative to the working directory (the package root):
 *   the tests      script/tests/tests.ini, the scripts manifest
 *                  (tools/raid_gate/prepare_scripts.py; the package writes it
 *                  with the quest tests under script/tests/quests/). Only
 *                  quest rows whose source is there are offered.
 *   the runner     TORIRS_SCRIPTRUN_BIN, else bin/torirsserver.
 * What it writes, under the saves (TORIRSSERVER_SAVES):
 *   test_runs/<stem>/          the run: out.log, ledger.tsv, saves/ -- the
 *                              throwaway character's save. Replaced by the
 *                              next run of that test.
 *   recordings/test_<stem>/<yyyymmdd-hhmmss>.bin
 *                              the recording; the Recordings page lists it
 *                              too, under the account test_<stem>.
 *
 * The runner gets the world's environment (cache, content) minus the live
 * services' (account and hiscores servers, the gate, the saves), plus
 * TORIRSSERVER_DEV=1 for the tests' setup cheats, and runs at nice 10 so the
 * live world's tick comes first. It is stopped after TESTS_WALL_MAX_S.
 */

#ifndef _WIN32
#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

#define TESTS_MAX 512
#define TESTS_MANIFEST "script/tests/tests.ini"
#define TESTS_SCRIPT_ROOT "script"
#define TESTS_WALL_MAX_S (20 * 60)
#define TESTS_LEDGER_ROWS_MAX 600

struct SiteTest
{
    char id[64];
    char title[160];
    /* Under TESTS_SCRIPT_ROOT, as the manifest names them. */
    char source[256];
    char fixture[256];
    int legs;
    int party;
    int max_frames;
};

enum SiteTestState
{
    SITE_TEST_IDLE,
    SITE_TEST_RUNNING,
    SITE_TEST_DONE,
};

/* The one run: in progress, or the last one finished. */
static struct
{
    enum SiteTestState state;
#ifndef _WIN32
    pid_t pid;
#endif
    char id[64];
    char title[160];
    char who[SITE_NAME_MAX];
    /* test_runs/<stem> under the saves. */
    char session[1024];
    /* "test_<stem>/<yyyymmdd-hhmmss>.bin": the replay page's `f`. */
    char recording[128];
    time_t started;
    time_t ended;
    /* Stopped from the page, or by the wall limit. */
    int stopped;
    /* The runner's exit: 0 PASS, 1 FAIL, else it did not run the test. -1
     * when nobody could wait for it. */
    int exit_code;
} g_test_run;

/* "test_" + [a-z0-9_] of the id: the recordings' account name, which
 * recording_account_ok accepts. */
static void
test_stem(
    char const* id,
    char* out,
    size_t cap)
{
    size_t n = 0;

    assert(cap > 8);
    n = (size_t)snprintf(out, cap, "test_");
    for( char const* c = id; *c && n + 1 < cap && n + 1 < SITE_NAME_MAX; c++ )
    {
        unsigned char ch = (unsigned char)*c;

        if( isalnum(ch) )
            out[n++] = (char)tolower(ch);
        else
            out[n++] = '_';
    }
    out[n] = '\0';
}

static int
test_cmp(
    void const* a,
    void const* b)
{
    struct SiteTest const* x = a;
    struct SiteTest const* y = b;

    /* The quests themselves first, the probes (_ids) after them. */
    if( (x->id[0] == '_') != (y->id[0] == '_') )
        return x->id[0] == '_' ? 1 : -1;
    return strcmp(x->title, y->title);
}

/* The quest tests the manifest names and this machine has, sorted; the
 * caller frees *out (ToriMem_Net). 0 when there is no manifest. */
static int
tests_load(struct SiteTest** out)
{
    FILE* f = fopen(TESTS_MANIFEST, "r");
    char line[512];
    struct SiteTest* list = ToriMem_Alloc(ToriMem_Net(), TESTS_MAX * sizeof(*list));
    struct SiteTest row;
    int in_row = 0;
    int quest = 0;
    int available = 0;
    int count = 0;

    assert(list);
    *out = list;
    if( !f )
        return 0;
    memset(&row, 0, sizeof(row));
    for( ;; )
    {
        int const end = fgets(line, sizeof(line), f) == NULL;
        size_t len;

        /* A row ends at the next section or the end of the file. */
        if( end || line[0] == '[' )
        {
            if( in_row && quest && available && count < TESTS_MAX && row.source[0] )
            {
                char path[600];
                struct stat st;

                snprintf(path, sizeof(path), "%s/%s", TESTS_SCRIPT_ROOT, row.source);
                if( stat(path, &st) == 0 )
                    list[count++] = row;
            }
            in_row = 0;
            if( end )
                break;
            memset(&row, 0, sizeof(row));
            row.party = 1;
            quest = 0;
            available = 0;
            if( strncmp(line, "[test:", 6) == 0 )
            {
                char* close = strchr(line + 6, ']');

                if( close )
                {
                    *close = '\0';
                    snprintf(row.id, sizeof(row.id), "%s", line + 6);
                    in_row = row.id[0] != '\0';
                }
            }
            continue;
        }
        len = strcspn(line, "\r\n");
        line[len] = '\0';
        if( !in_row )
            continue;
        if( strncmp(line, "suite=", 6) == 0 )
            quest = strcmp(line + 6, "quest") == 0;
        else if( strncmp(line, "title=", 6) == 0 )
            snprintf(row.title, sizeof(row.title), "%s", line + 6);
        else if( strncmp(line, "source=", 7) == 0 )
            snprintf(row.source, sizeof(row.source), "%s", line + 7);
        else if( strncmp(line, "fixture=", 8) == 0 )
            snprintf(row.fixture, sizeof(row.fixture), "%s", line + 8);
        else if( strncmp(line, "legs=", 5) == 0 )
            row.legs = atoi(line + 5);
        else if( strncmp(line, "party=", 6) == 0 )
            row.party = atoi(line + 6);
        else if( strncmp(line, "max_frames=", 11) == 0 )
            row.max_frames = atoi(line + 11);
        else if( strncmp(line, "available=", 10) == 0 )
            available = atoi(line + 10) != 0;
    }
    fclose(f);
    for( int i = 0; i < count; i++ )
    {
        if( !list[i].title[0] )
            snprintf(list[i].title, sizeof(list[i].title), "%s", list[i].id);
    }
    qsort(list, (size_t)count, sizeof(*list), test_cmp);
    return count;
}

static struct SiteTest const*
tests_find(
    struct SiteTest const* list,
    int count,
    char const* id)
{
    for( int i = 0; i < count; i++ )
    {
        if( strcmp(list[i].id, id) == 0 )
            return &list[i];
    }
    return NULL;
}

/* The run's ledger: its row count, and the last row's step and verdict. */
static int
test_ledger_tail(
    char* last,
    size_t cap)
{
    char path[1100];
    char line[1024];
    FILE* f;
    int rows = 0;

    last[0] = '\0';
    snprintf(path, sizeof(path), "%s/ledger.tsv", g_test_run.session);
    f = fopen(path, "r");
    if( !f )
        return 0;
    while( fgets(line, sizeof(line), f) )
    {
        /* name \t tick \t step \t verdict \t detail */
        char* field[4] = { NULL, NULL, NULL, NULL };
        char* at = line;

        for( int i = 0; i < 4 && at; i++ )
        {
            field[i] = at;
            at = strchr(at, '\t');
            if( at )
                *at++ = '\0';
        }
        if( !field[3] )
            continue;
        rows++;
        field[3][strcspn(field[3], "\r\n")] = '\0';
        snprintf(last, cap, "%s %s", field[2], field[3]);
    }
    fclose(f);
    return rows;
}

#ifndef _WIN32
static int
test_rm_one(
    char const* path,
    struct stat const* st,
    int flag,
    struct FTW* ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    remove(path);
    return 0;
}

/* `rm -rf` of the last run of this test. */
static void
test_rm_tree(char const* dir)
{
    nftw(dir, test_rm_one, 16, FTW_DEPTH | FTW_PHYS);
}

/* The runner's environment: the world's, less the live services'. */
static char**
test_environment(void)
{
    static char const* const k_drop[] = {
        "TORIRS_ACCOUNT_SERVER=", "TORIRS_HISCORES_SERVER=", "TORIRS_WEBSITE_SERVER=",
        "TORIRS_IO_SERVER=",      "TORIRSSERVER_GATE=",      "TORIRSSERVER_GATE_URL=",
        "TORIRSSERVER_SAVES=",    "TORIRSSERVER_BIND=",      "TORIRSSERVER_DEV=",
    };
    int count = 0;
    char** env;
    int n = 0;

    while( environ[count] )
        count++;
    env = ToriMem_Alloc(ToriMem_Net(), (size_t)(count + 2) * sizeof(*env));
    assert(env);
    for( int i = 0; i < count; i++ )
    {
        int drop = 0;

        for( size_t k = 0; k < sizeof(k_drop) / sizeof(k_drop[0]); k++ )
        {
            if( strncmp(environ[i], k_drop[k], strlen(k_drop[k])) == 0 )
                drop = 1;
        }
        if( !drop )
            env[n++] = environ[i];
    }
    /* The tests' setup cheats (::give, ::setlevel, ::tele). */
    env[n++] = "TORIRSSERVER_DEV=1";
    env[n] = NULL;
    return env;
}

/* A finished runner: reaped, its end noted. Every read of the run calls it. */
static void
test_reap(void)
{
    int status = 0;
    pid_t got;

    if( g_test_run.state != SITE_TEST_RUNNING )
        return;
    got = waitpid(g_test_run.pid, &status, WNOHANG);
    if( got == 0 )
    {
        if( time(NULL) - g_test_run.started > TESTS_WALL_MAX_S && !g_test_run.stopped )
        {
            fprintf(stderr, "website_server: test %s ran past %d s; stopping it\n", g_test_run.id,
                    TESTS_WALL_MAX_S);
            g_test_run.stopped = 1;
            kill(g_test_run.pid, SIGTERM);
        }
        return;
    }
    g_test_run.state = SITE_TEST_DONE;
    g_test_run.ended = time(NULL);
    g_test_run.exit_code = got == g_test_run.pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    fprintf(stderr, "website_server: test %s ended (exit %d) after %lld s\n", g_test_run.id,
            g_test_run.exit_code, (long long)(g_test_run.ended - g_test_run.started));
}

/* Start `test`; NULL, or why not. */
static char const*
test_start(
    struct SiteTest const* test,
    char const* who)
{
    static char reason[160];
    char stem[SITE_NAME_MAX];
    char bin[512];
    char source[600];
    char fixture[600];
    char record[1400];
    char log_path[1100];
    char ticks[16];
    char bots[16];
    char stamp[32];
    char const* argv[24];
    char** env;
    char const* bin_env = getenv("TORIRS_SCRIPTRUN_BIN");
    posix_spawn_file_actions_t actions;
    struct stat st;
    struct tm tm_utc;
    time_t now = time(NULL);
    pid_t pid;
    int argc = 0;
    int rc;

    assert(test);
    assert(who);
    test_reap();
    if( g_test_run.state == SITE_TEST_RUNNING )
        return "A test is already running; stop it or wait for it.";
    snprintf(bin, sizeof(bin), "%s", bin_env && bin_env[0] ? bin_env : "bin/torirsserver");
    if( stat(bin, &st) != 0 )
    {
        snprintf(reason, sizeof(reason), "No runner at %s.", bin);
        return reason;
    }
    test_stem(test->id, stem, sizeof(stem));
    gmtime_r(&now, &tm_utc);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_utc);

    memset(&g_test_run, 0, sizeof(g_test_run));
    snprintf(g_test_run.id, sizeof(g_test_run.id), "%s", test->id);
    snprintf(g_test_run.title, sizeof(g_test_run.title), "%s", test->title);
    snprintf(g_test_run.who, sizeof(g_test_run.who), "%s", who);
    snprintf(g_test_run.session, sizeof(g_test_run.session), "%s/test_runs/%s", ToriRSServer_SaveDir(),
             stem + 5);
    snprintf(g_test_run.recording, sizeof(g_test_run.recording), "%s/%s.bin", stem, stamp);
    test_rm_tree(g_test_run.session);
    snprintf(log_path, sizeof(log_path), "%s/saves/x", g_test_run.session);
    ToriRSServer_SaveMkdirParent(log_path);
    snprintf(log_path, sizeof(log_path), "%s/out.log", g_test_run.session);
    snprintf(record, sizeof(record), "%s/recordings/%s", ToriRSServer_SaveDir(), g_test_run.recording);
    snprintf(source, sizeof(source), "%s/%s", TESTS_SCRIPT_ROOT, test->source);
    snprintf(fixture, sizeof(fixture), "%s/%s", TESTS_SCRIPT_ROOT, test->fixture);
    /* The test's own frame budget, as ticks (30 frames a tick), twice over:
     * a stuck run ends on its own. */
    snprintf(ticks, sizeof(ticks), "%d", test->max_frames > 0 ? test->max_frames / 15 + 1000 : 20000);
    snprintf(bots, sizeof(bots), "%d", test->party > 1 ? test->party : 1);

    argv[argc++] = "nice";
    argv[argc++] = "-n";
    argv[argc++] = "10";
    argv[argc++] = bin;
    argv[argc++] = "--scriptrun";
    argv[argc++] = source;
    argv[argc++] = "--bots";
    argv[argc++] = bots;
    argv[argc++] = "--name";
    argv[argc++] = test->id[0] == '_' ? test->id + 1 : test->id;
    argv[argc++] = "--ticks";
    argv[argc++] = ticks;
    argv[argc++] = "--script-root";
    argv[argc++] = TESTS_SCRIPT_ROOT;
    argv[argc++] = "--session";
    argv[argc++] = g_test_run.session;
    if( test->fixture[0] )
    {
        argv[argc++] = "--fixture";
        argv[argc++] = fixture;
    }
    argv[argc++] = "--record";
    argv[argc++] = record;
    argv[argc] = NULL;

    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    env = test_environment();
    rc = posix_spawnp(&pid, "nice", &actions, NULL, (char* const*)argv, env);
    posix_spawn_file_actions_destroy(&actions);
    ToriMem_Free(ToriMem_Net(), env);
    if( rc != 0 )
    {
        snprintf(reason, sizeof(reason), "Could not start the runner: %s.", strerror(rc));
        g_test_run.state = SITE_TEST_IDLE;
        return reason;
    }
    g_test_run.pid = pid;
    g_test_run.state = SITE_TEST_RUNNING;
    g_test_run.started = now;
    fprintf(stderr, "website_server: '%s' runs test %s (pid %d) -> %s\n", who, test->id, (int)pid,
            record);
    return NULL;
}

static void
test_stop(void)
{
    test_reap();
    if( g_test_run.state != SITE_TEST_RUNNING )
        return;
    g_test_run.stopped = 1;
    kill(g_test_run.pid, SIGTERM);
}
#else
static void
test_reap(void)
{
}

static char const*
test_start(
    struct SiteTest const* test,
    char const* who)
{
    (void)test;
    (void)who;
    return "Tests run on a Linux or macOS server only.";
}

static void
test_stop(void)
{
}
#endif

/* "PASS", "FAIL", "STOPPED" or "ERROR" for a finished run. */
static char const*
test_result(void)
{
    if( g_test_run.stopped )
        return "STOPPED";
    if( g_test_run.exit_code == 0 )
        return "PASS";
    if( g_test_run.exit_code == 1 )
        return "FAIL";
    return "ERROR";
}

/* Whether the run's recording has a byte past its header. */
/* What the finished run's out.log says: the world ticks it ran (the
 * runner's `SCRIPTRUN PASS|FAIL: N ticks` line; -1 when it has none, a run
 * killed before it could say), and the first FAIL row's text in `why`. */
static int
test_run_outcome(
    char* why,
    size_t cap)
{
    char path[1100];
    char line[1024];
    FILE* f;
    int ticks = -1;

    why[0] = '\0';
    snprintf(path, sizeof(path), "%s/out.log", g_test_run.session);
    f = fopen(path, "r");
    if( !f )
        return -1;
    while( fgets(line, sizeof(line), f) )
    {
        char const* fail = strstr(line, "] FAIL ");
        int n;

        line[strcspn(line, "\r\n")] = '\0';
        if( (sscanf(line, "SCRIPTRUN PASS: %d ticks", &n) == 1 ||
             sscanf(line, "SCRIPTRUN FAIL: %d ticks", &n) == 1) )
            ticks = n;
        if( fail && !why[0] )
            snprintf(why, cap, "%s", fail + 2);
    }
    fclose(f);
    return ticks;
}

/* Whether the run left something to watch: a recording past its header, of
 * a run that ticked the world. A test that failed before its first tick (a
 * file the runner could not start) recorded the login and nothing else, and
 * the replay would stand at the title screen. */
static int
test_recording_exists(void)
{
    char path[1400];
    char why[300];
    struct stat st;

    if( g_test_run.state == SITE_TEST_DONE && test_run_outcome(why, sizeof(why)) == 0 )
        return 0;
    snprintf(path, sizeof(path), "%s/recordings/%s", ToriRSServer_SaveDir(), g_test_run.recording);
    return stat(path, &st) == 0 && st.st_size > 256;
}

static void
page_tests_status(struct HttpResponse* res)
{
    struct SiteBuf b;
    char last[300];
    int rows;
    time_t end;

    test_reap();
    end = g_test_run.state == SITE_TEST_DONE ? g_test_run.ended : time(NULL);
    rows = g_test_run.state == SITE_TEST_IDLE ? 0 : test_ledger_tail(last, sizeof(last));
    if( g_test_run.state == SITE_TEST_IDLE )
        last[0] = '\0';
    buf_init(&b);
    buf_puts(&b, "{\"state\":");
    buf_json(&b, g_test_run.state == SITE_TEST_RUNNING ? "running"
                 : g_test_run.state == SITE_TEST_DONE  ? "done"
                                                       : "idle");
    buf_puts(&b, ",\"id\":");
    buf_json(&b, g_test_run.id);
    buf_puts(&b, ",\"title\":");
    buf_json(&b, g_test_run.title);
    buf_printf(&b, ",\"secs\":%lld,\"rows\":%d,\"last\":",
               g_test_run.state == SITE_TEST_IDLE ? 0LL : (long long)(end - g_test_run.started), rows);
    buf_json(&b, last);
    buf_puts(&b, ",\"result\":");
    buf_json(&b, g_test_run.state == SITE_TEST_DONE ? test_result() : "");
    buf_puts(&b, ",\"watch\":");
    buf_json(&b, g_test_run.state != SITE_TEST_IDLE && test_recording_exists() ? g_test_run.recording : "");
    buf_puts(&b, "}");
    pe_json_send(&b, res, 200);
}

/* The finished run's steps, from its ledger. */
static void
tests_steps(struct SiteBuf* b)
{
    char path[1100];
    char line[2048];
    FILE* f;
    int rows = 0;

    snprintf(path, sizeof(path), "%s/ledger.tsv", g_test_run.session);
    f = fopen(path, "r");
    if( !f )
    {
        buf_puts(b, "<p class=\"note\">The run wrote no steps: see <code>");
        buf_esc(b, g_test_run.session);
        buf_puts(b, "/out.log</code>.</p>");
        return;
    }
    buf_puts(b, "<div class=\"scroll\" style=\"max-height:480px\"><table><thead><tr>"
                "<th class=\"num\">Tick</th><th>Step</th><th>Verdict</th><th>Detail</th>"
                "</tr></thead><tbody>");
    while( fgets(line, sizeof(line), f) && rows < TESTS_LEDGER_ROWS_MAX )
    {
        char* field[5] = { NULL, NULL, NULL, NULL, NULL };
        char* at = line;
        int pass;

        line[strcspn(line, "\r\n")] = '\0';
        for( int i = 0; i < 5 && at; i++ )
        {
            field[i] = at;
            at = i < 4 ? strchr(at, '\t') : NULL;
            if( at )
                *at++ = '\0';
        }
        if( !field[3] )
            continue;
        rows++;
        pass = strcmp(field[3], "PASS") == 0;
        buf_puts(b, "<tr><td class=\"num\">");
        buf_esc(b, field[1]);
        buf_puts(b, "</td><td>");
        buf_esc(b, field[2]);
        buf_printf(b, "</td><td><b class=\"%s\">", pass ? "ok" : "bad");
        buf_esc(b, field[3]);
        buf_puts(b, "</b></td><td class=\"note\">");
        if( field[4] )
        {
            /* A detail can be a paragraph: the first 200 bytes say what it is. */
            char cut[208];

            snprintf(cut, sizeof(cut), "%.200s%s", field[4], strlen(field[4]) > 200 ? "..." : "");
            buf_esc(b, cut);
        }
        buf_puts(b, "</td></tr>");
    }
    fclose(f);
    buf_puts(b, "</tbody></table></div>");
}

static void
page_tests(
    struct WebsiteGate* gate,
    char const* who,
    struct HttpResponse* res,
    int status,
    char const* error)
{
    struct SiteBuf b;
    struct SiteTest* tests;
    struct SiteRecording* recs;
    int const count = tests_load(&tests);
    int rec_count;
    int shown = 0;
    int running;

    test_reap();
    running = g_test_run.state == SITE_TEST_RUNNING;
    rec_count = recordings_list("", &recs);
    page_begin(&b, gate, who, "Tests", "/admin/tests");
    buf_puts(&b, "<div class=\"title\"><h1>Tests<small>play a quest test on a throwaway "
                 "character, then watch it</small></h1></div>\n");
    if( error )
    {
        buf_puts(&b, "<p class=\"flash bad\">");
        buf_esc(&b, error);
        buf_puts(&b, "</p>");
    }
    buf_puts(&b, "<div class=\"stack\">");

    /* The run. */
    buf_puts(&b, "<section class=\"panel\"><h2>Run</h2><div id=\"run\">");
    if( g_test_run.state == SITE_TEST_IDLE )
        buf_puts(&b, "<p>Pick a test below. It plays on this server in a world of its own, on "
                     "a new character made from the test's fixture, in a few seconds; then it "
                     "opens in the replay viewer.</p>");
    else
    {
        buf_puts(&b, "<p><b>");
        buf_esc(&b, g_test_run.title);
        buf_puts(&b, "</b> <span class=\"note\">");
        buf_esc(&b, g_test_run.id);
        buf_puts(&b, "</span> &middot; <span id=\"runstate\">");
        if( running )
            buf_puts(&b, "running&hellip;");
        else
        {
            char const* result = test_result();

            buf_printf(&b, "<b class=\"%s\">%s</b> in %lld s", strcmp(result, "PASS") == 0 ? "ok" : "bad",
                       result, (long long)(g_test_run.ended - g_test_run.started));
        }
        buf_puts(&b, "</span></p>");
    }
    buf_puts(&b, "<div class=\"row\" style=\"align-items:center;gap:12px\">");
    if( running )
        buf_puts(&b, "<form method=\"post\" action=\"/admin/tests/stop\" style=\"margin:0\">"
                     "<button class=\"btn quiet\">Stop</button></form>");
    if( g_test_run.state == SITE_TEST_DONE && test_recording_exists() )
    {
        buf_puts(&b, "<a class=\"btn\" id=\"watch\" href=\"/admin/replay?f=");
        buf_url(&b, g_test_run.recording);
        buf_puts(&b, "\">Watch</a>");
    }
    buf_puts(&b, "<label style=\"margin:0\">Speed <select id=\"speed\" style=\"display:inline;"
                 "width:auto;margin:0\">");
    for( size_t i = 0; i < sizeof(k_replay_speeds) / sizeof(k_replay_speeds[0]); i++ )
        buf_printf(&b, "<option value=\"%s\">%s</option>", k_replay_speeds[i][0], k_replay_speeds[i][1]);
    buf_puts(&b, "</select></label><label style=\"margin:0\"><input type=\"checkbox\" id=\"auto\" "
                 "style=\"display:inline;width:auto\" checked> Watch when it finishes</label></div>");
    if( g_test_run.state == SITE_TEST_DONE )
    {
        char why[300];
        int const ticks = test_run_outcome(why, sizeof(why));

        if( ticks == 0 )
        {
            buf_puts(&b, "<p class=\"flash bad\">The test stopped before its first tick, so there "
                         "is nothing to watch");
            if( why[0] )
            {
                buf_puts(&b, ": <code>");
                buf_esc(&b, why);
                buf_puts(&b, "</code>");
            }
            buf_puts(&b, ".</p>");
        }
        tests_steps(&b);
    }
    buf_puts(&b, "</div></section>");

    /* The tests. */
    buf_puts(&b, "<section class=\"panel\"><h2>Quest tests</h2>");
    if( count == 0 )
        buf_puts(&b, "<p>No tests here: the package writes <code>" TESTS_MANIFEST "</code> and "
                     "<code>script/tests/quests/</code>.</p>");
    else
    {
        buf_puts(&b, "<input type=\"search\" id=\"find\" class=\"search\" placeholder=\"Find a test"
                     "\" autocomplete=\"off\"><div class=\"scroll\" style=\"max-height:560px\"><table>"
                     "<thead><tr><th>Test</th><th>Id</th><th class=\"num\">Legs</th><th></th></tr>"
                     "</thead><tbody id=\"tests\">");
        for( int i = 0; i < count; i++ )
        {
            struct SiteTest const* t = &tests[i];

            buf_puts(&b, "<tr><td>");
            buf_esc(&b, t->title);
            buf_puts(&b, "</td><td class=\"note\">");
            buf_esc(&b, t->id);
            buf_printf(&b, "</td><td class=\"num\">%d</td><td><form method=\"post\" "
                           "action=\"/admin/tests/run\" style=\"margin:0\"><input type=\"hidden\" "
                           "name=\"id\" value=\"",
                       t->legs);
            buf_esc(&b, t->id);
            buf_printf(&b, "\"><button class=\"btn\"%s>Run</button></form></td></tr>",
                       running ? " disabled" : "");
        }
        buf_puts(&b, "</tbody></table></div>");
        buf_printf(&b, "<p class=\"note\" style=\"margin-top:12px\">%d tests. The replay shows what "
                       "the server sent the character, not the runner's pointer. A test that "
                       "logs out ends its recording there.</p>",
                   count);
    }
    buf_puts(&b, "</section>");

    /* Recent test recordings. */
    buf_puts(&b, "<section class=\"panel\"><h2>Recent runs</h2>");
    for( int i = 0; i < rec_count && shown < 20; i++ )
    {
        struct SiteRecording const* r = &recs[i];

        if( strncmp(r->account, "test_", 5) != 0 )
            continue;
        if( shown++ == 0 )
            buf_puts(&b, "<div class=\"scroll\"><table><thead><tr><th>Test</th><th>When</th>"
                         "<th class=\"num\">Size</th><th></th></tr></thead><tbody>");
        buf_puts(&b, "<tr><td>");
        buf_esc(&b, r->account + 5);
        buf_puts(&b, "</td><td class=\"when\">");
        buf_when(&b, r->start);
        buf_puts(&b, "</td><td class=\"num\">");
        buf_bytes(&b, r->size);
        buf_puts(&b, "</td><td><a class=\"watch\" href=\"/admin/replay?f=");
        buf_url(&b, r->account);
        buf_puts(&b, "/");
        buf_url(&b, r->file);
        buf_puts(&b, "\">Watch</a></td></tr>");
    }
    if( shown )
        buf_puts(&b, "</tbody></table></div>");
    else
        buf_puts(&b, "<p>None yet.</p>");
    buf_puts(&b, "</section></div>");

    /* The search, the speed on every Watch link (remembered), and the poll
     * while a run is going: a finished run reloads the page, or goes straight
     * to its replay. */
    buf_puts(&b, "<script>(function(){\n"
        "var sp=document.getElementById('speed'),auto=document.getElementById('auto');\n"
        "try{var s=localStorage.getItem('torirs.tests.speed');if(s)sp.value=s;"
        "auto.checked=localStorage.getItem('torirs.tests.auto')!=='0';}catch(e){}\n"
        "function watchUrl(f){return '/admin/replay?f='+encodeURIComponent(f).replace(/%2F/g,'/')"
        "+'&speed='+encodeURIComponent(sp.value);}\n"
        "function links(){document.querySelectorAll('a#watch,a.watch').forEach(function(a){"
        "var f=new URL(a.href).searchParams.get('f');a.href=watchUrl(f);});}\n"
        "sp.onchange=function(){try{localStorage.setItem('torirs.tests.speed',sp.value);}catch(e){}links();};\n"
        "auto.onchange=function(){try{localStorage.setItem('torirs.tests.auto',auto.checked?'1':'0');}"
        "catch(e){}};\n"
        "links();\n"
        "var find=document.getElementById('find');\n"
        "if(find){find.oninput=function(){var q=find.value.toLowerCase();"
        "document.querySelectorAll('#tests tr').forEach(function(tr){"
        "tr.style.display=tr.textContent.toLowerCase().indexOf(q)>=0?'':'none';});};}\n");
    if( running )
        buf_puts(&b, "var st=document.getElementById('runstate');\n"
            "function poll(){fetch('/admin/tests/status',{cache:'no-store'}).then(function(r){"
            "return r.json();}).then(function(j){\n"
            "if(j.state==='running'){st.textContent='running, '+j.secs+' s, '+j.rows+' steps'"
            "+(j.last?' \\u2014 '+j.last:'');setTimeout(poll,1000);return;}\n"
            "if(j.state==='done'&&j.watch&&auto.checked){location.href=watchUrl(j.watch);return;}\n"
            "location.reload();}).catch(function(){setTimeout(poll,2000);});}\n"
            "poll();\n");
    buf_puts(&b, "})();</script>");
    ToriMem_Free(ToriMem_Net(), tests);
    ToriMem_Free(ToriMem_Net(), recs);
    page_send(&b, res, status);
}

/* The Tests routes; 1 when it answered. Staff only: the caller checked. */
static int
tests_route(
    struct WebsiteGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    int is_get,
    int is_post)
{
    if( is_get && WebsiteGate_PathIs(req->path, "/admin/tests") )
    {
        page_tests(gate, who, res, 200, NULL);
        return 1;
    }
    if( is_get && WebsiteGate_PathIs(req->path, "/admin/tests/status") )
    {
        page_tests_status(res);
        return 1;
    }
    if( is_post && WebsiteGate_PathIs(req->path, "/admin/tests/run") )
    {
        char id[64] = "";
        struct SiteTest* tests;
        int const count = tests_load(&tests);
        struct SiteTest const* test;
        char const* refused;

        WebsiteGate_FormField(req, "id", id, sizeof(id));
        test = tests_find(tests, count, id);
        refused = test ? test_start(test, who) : "No such test.";
        ToriMem_Free(ToriMem_Net(), tests);
        if( refused )
            page_tests(gate, who, res, 400, refused);
        else
            redirect(res, "/admin/tests");
        return 1;
    }
    if( is_post && WebsiteGate_PathIs(req->path, "/admin/tests/stop") )
    {
        test_stop();
        redirect(res, "/admin/tests");
        return 1;
    }
    return 0;
}
