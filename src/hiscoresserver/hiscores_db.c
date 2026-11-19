/*
 * The hiscores database. See hiscores_db.h.
 */
#include "hiscores_db.h"

#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_history.h"
#include "torirsserver/torirs_server_savefile.h"

#include <sqlite3.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define HISCORES_SCHEMA_VERSION 2
/* configs/all.varp varp101_qp, as a save's [varps] names it. */
#define ACCOUNTS_VARP_QP 101
/* A fresh character's Hitpoints: level 10, in tenths. */
#define ACCOUNTS_FRESH_HITPOINTS_TENTHS 11540
#define ACCOUNTS_HITPOINTS 3

static char const k_schema[] =
    "CREATE TABLE player ("
    " id INTEGER PRIMARY KEY, key TEXT NOT NULL UNIQUE, name TEXT NOT NULL,"
    " ranked INTEGER NOT NULL, last_seen INTEGER NOT NULL);"
    "CREATE TABLE score ("
    " player INTEGER NOT NULL, category INTEGER NOT NULL, score INTEGER NOT NULL,"
    " since INTEGER NOT NULL, ranked INTEGER NOT NULL,"
    " PRIMARY KEY (player, category)) WITHOUT ROWID;"
    "CREATE INDEX score_rank ON score (category, score DESC, since) WHERE ranked = 1;"
    "CREATE TABLE xp_day ("
    " player INTEGER NOT NULL, stat INTEGER NOT NULL, day INTEGER NOT NULL,"
    " xp INTEGER NOT NULL, PRIMARY KEY (player, stat, day)) WITHOUT ROWID;"
    "CREATE TABLE session ("
    " player INTEGER NOT NULL, start INTEGER NOT NULL, end INTEGER NOT NULL,"
    " xp_gained INTEGER NOT NULL, top_stat INTEGER NOT NULL, top_gain INTEGER NOT NULL,"
    " PRIMARY KEY (player, start)) WITHOUT ROWID;"
    "CREATE TABLE event ("
    " id INTEGER PRIMARY KEY, player INTEGER NOT NULL, time INTEGER NOT NULL,"
    " kind INTEGER NOT NULL, amount INTEGER NOT NULL, label TEXT NOT NULL);"
    "CREATE INDEX event_player ON event (player, id DESC);";

/* Schema 2: each world's last record applied, which makes a resend harmless. */
static char const k_schema_2[] =
    "CREATE TABLE source (name TEXT PRIMARY KEY, seq INTEGER NOT NULL) WITHOUT ROWID;";

enum HiscoresStatement
{
    ST_PLAYER_PUT,
    ST_PLAYER_ID,
    ST_PLAYER_RANKED,
    ST_SCORE_RANKED,
    ST_SCORE_PUT,
    ST_XP_DAY_PUT,
    ST_SESSION_PUT,
    ST_EVENT_PUT,
    ST_PLAYER_GET,
    ST_SCORES_GET,
    ST_HISCORES,
    ST_RANKED_COUNT,
    ST_RANK_SELF,
    ST_RANK_AHEAD,
    ST_XP_AT,
    ST_XP_FIRST,
    ST_XP_DAYS,
    ST_SESSIONS,
    ST_SESSION_TOTALS,
    ST_EVENTS,
    ST_SOURCE_GET,
    ST_SOURCE_PUT,
    ST_COUNT,
};

static char const* const k_sql[ST_COUNT] = {
    [ST_PLAYER_PUT] = "INSERT INTO player (key, name, ranked, last_seen) VALUES (?1, ?2, 1, ?3)"
                      " ON CONFLICT (key) DO UPDATE SET name = excluded.name,"
                      " last_seen = MAX(last_seen, excluded.last_seen)",
    [ST_PLAYER_ID] = "SELECT id, ranked FROM player WHERE key = ?1",
    [ST_PLAYER_RANKED] = "UPDATE player SET ranked = ?2 WHERE id = ?1",
    [ST_SCORE_RANKED] = "UPDATE score SET ranked = ?2 WHERE player = ?1 AND ranked <> ?2",
    /* The WHERE makes an unchanged score no change at all, so `since` keeps
     * the moment it was reached and sqlite3_changes says whether it moved. */
    [ST_SCORE_PUT] = "INSERT INTO score (player, category, score, since, ranked)"
                     " VALUES (?1, ?2, ?3, ?4, ?5)"
                     " ON CONFLICT (player, category) DO UPDATE SET score = excluded.score,"
                     " since = excluded.since WHERE score <> excluded.score",
    [ST_XP_DAY_PUT] = "INSERT INTO xp_day (player, stat, day, xp) VALUES (?1, ?2, ?3, ?4)"
                      " ON CONFLICT (player, stat, day) DO UPDATE SET xp = excluded.xp",
    [ST_SESSION_PUT] = "INSERT OR REPLACE INTO session"
                       " (player, start, end, xp_gained, top_stat, top_gain)"
                       " VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
    [ST_EVENT_PUT] = "INSERT INTO event (player, time, kind, amount, label)"
                     " VALUES (?1, ?2, ?3, ?4, ?5)",
    [ST_PLAYER_GET] = "SELECT id, key, name, ranked, last_seen FROM player WHERE key = ?1",
    [ST_SCORES_GET] = "SELECT category, score FROM score WHERE player = ?1",
    /* `ranked = 1` spelled exactly as the partial index's WHERE, or the
     * planner cannot use it. */
    [ST_HISCORES] = "SELECT p.key, p.name, s.score FROM score s JOIN player p ON p.id = s.player"
                    " WHERE s.category = ?1 AND s.ranked = 1"
                    " ORDER BY s.score DESC, s.since, s.player LIMIT ?3 OFFSET ?2",
    [ST_RANKED_COUNT] = "SELECT COUNT(*) FROM score WHERE category = ?1 AND ranked = 1",
    [ST_RANK_SELF] = "SELECT score, since, ranked FROM score WHERE player = ?1 AND category = ?2",
    /* Three ranges of the index rather than one OR, which the planner would
     * answer by walking the whole category. */
    [ST_RANK_AHEAD] =
        "SELECT (SELECT COUNT(*) FROM score WHERE category = ?1 AND ranked = 1 AND score > ?2)"
        " + (SELECT COUNT(*) FROM score WHERE category = ?1 AND ranked = 1 AND score = ?2"
        " AND since < ?3)"
        " + (SELECT COUNT(*) FROM score WHERE category = ?1 AND ranked = 1 AND score = ?2"
        " AND since = ?3 AND player < ?4)",
    [ST_XP_AT] = "SELECT xp FROM xp_day WHERE player = ?1 AND stat = ?2 AND day <= ?3"
                 " ORDER BY day DESC LIMIT 1",
    [ST_XP_FIRST] = "SELECT day FROM xp_day WHERE player = ?1 AND stat = ?2 ORDER BY day LIMIT 1",
    [ST_XP_DAYS] = "SELECT day, xp FROM xp_day WHERE player = ?1 AND stat = ?2"
                   " ORDER BY day DESC LIMIT ?3",
    [ST_SESSIONS] = "SELECT start, end, xp_gained, top_stat, top_gain FROM session"
                    " WHERE player = ?1 ORDER BY start DESC LIMIT ?2",
    [ST_SESSION_TOTALS] = "SELECT COUNT(*), COALESCE(SUM(CASE WHEN start > 0 AND end > start"
                          " THEN end - start ELSE 0 END), 0) FROM session WHERE player = ?1",
    [ST_EVENTS] = "SELECT time, kind, amount, label FROM event WHERE player = ?1"
                  " ORDER BY id DESC LIMIT ?2",
    [ST_SOURCE_GET] = "SELECT seq FROM source WHERE name = ?1",
    [ST_SOURCE_PUT] = "INSERT INTO source (name, seq) VALUES (?1, ?2)"
                      " ON CONFLICT (name) DO UPDATE SET seq = excluded.seq",
};

static struct
{
    sqlite3* db;
    sqlite3_stmt* st[ST_COUNT];
} g_hiscores;

/* ------------------------------------------------------------------ */
/* The connection                                                      */
/* ------------------------------------------------------------------ */

static void
must(
    sqlite3* db,
    int rc,
    char const* what)
{
    if( rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW )
    {
        fprintf(stderr, "hiscores_server: database: %s: %s\n", what,
                db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        abort();
    }
}

static void
exec(
    sqlite3* db,
    char const* sql)
{
    must(db, sqlite3_exec(db, sql, NULL, NULL, NULL), sql);
}


const char*
HiscoresDb_Path(void)
{
    static char path[1024];

    snprintf(path, sizeof(path), "%s/hiscores.sqlite", ToriRSServer_SaveDir());
    return path;
}

static void hiscores_import(int from_accounts);

static sqlite3*
hiscores_db(void)
{
    char const* path;
    char old_path[1024];
    sqlite3_stmt* version;
    int schema;
    int attached = 0;
    struct stat info;

    if( g_hiscores.db )
        return g_hiscores.db;
    path = HiscoresDb_Path();
    ToriRSServer_SaveMkdirParent(path);
    snprintf(old_path, sizeof(old_path), "%s/accounts.sqlite", ToriRSServer_SaveDir());
    must(NULL,
         sqlite3_open_v2(path, &g_hiscores.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL),
         "open");
    sqlite3_busy_timeout(g_hiscores.db, 10000);
    exec(g_hiscores.db, "PRAGMA journal_mode = WAL");
    exec(g_hiscores.db, "PRAGMA synchronous = NORMAL");
    /* The database before the split, if there is one: the first open of this
     * one copies its rows out of it. Attached outside the transaction, which
     * sqlite requires, and only while this one is new. */
    if( stat(old_path, &info) == 0 )
    {
        char* sql = sqlite3_mprintf("ATTACH DATABASE %Q AS old", old_path);

        assert(sql);
        exec(g_hiscores.db, sql);
        sqlite3_free(sql);
        attached = 1;
    }

    /* EXCLUSIVE, then read the version: two processes opening a new file at
     * once must not both make it. */
    exec(g_hiscores.db, "BEGIN EXCLUSIVE");
    must(g_hiscores.db,
         sqlite3_prepare_v2(g_hiscores.db, "PRAGMA main.user_version", -1, &version, NULL),
         "prepare user_version");
    must(g_hiscores.db, sqlite3_step(version), "user_version");
    schema = sqlite3_column_int(version, 0);
    sqlite3_finalize(version);
    if( schema == 0 )
    {
        exec(g_hiscores.db, k_schema);
        exec(g_hiscores.db, k_schema_2);
    }
    if( schema > HISCORES_SCHEMA_VERSION )
    {
        fprintf(stderr, "hiscores_server: %s is schema %d; this build knows %d\n", path, schema,
                HISCORES_SCHEMA_VERSION);
        abort();
    }
    for( int i = 0; i < ST_COUNT; i++ )
        must(g_hiscores.db,
             sqlite3_prepare_v2(g_hiscores.db, k_sql[i], -1, &g_hiscores.st[i], NULL), k_sql[i]);
    if( schema == 0 )
    {
        hiscores_import(attached);
        fprintf(stderr, "hiscores_server: %s made, from %s\n", path,
                attached ? old_path : "the saves and history files");
    }
    if( schema < HISCORES_SCHEMA_VERSION )
    {
        char pragma[64];

        snprintf(pragma, sizeof(pragma), "PRAGMA main.user_version = %d", HISCORES_SCHEMA_VERSION);
        exec(g_hiscores.db, pragma);
    }
    exec(g_hiscores.db, "COMMIT");
    if( attached )
        exec(g_hiscores.db, "DETACH DATABASE old");
    return g_hiscores.db;
}

void
HiscoresDb_Open(void)
{
    hiscores_db();
}

void
HiscoresDb_Close(void)
{
    if( !g_hiscores.db )
        return;
    for( int i = 0; i < ST_COUNT; i++ )
        sqlite3_finalize(g_hiscores.st[i]);
    sqlite3_close(g_hiscores.db);
    memset(&g_hiscores, 0, sizeof(g_hiscores));
}

/* The statement, reset and unbound, ready for its arguments. */
static sqlite3_stmt*
st(enum HiscoresStatement which)
{
    sqlite3_stmt* s;

    hiscores_db();
    s = g_hiscores.st[which];
    sqlite3_reset(s);
    sqlite3_clear_bindings(s);
    return s;
}

/* One step of `s`: 1 for a row, 0 for the end. Aborts on an error. */
static int
step(sqlite3_stmt* s)
{
    int rc = sqlite3_step(s);

    must(g_hiscores.db, rc, sqlite3_sql(s));
    return rc == SQLITE_ROW;
}

/* Run a statement that returns nothing; the rows it changed. */
static int
run(sqlite3_stmt* s)
{
    step(s);
    sqlite3_reset(s);
    return sqlite3_changes(g_hiscores.db);
}

static void
bind_text(
    sqlite3_stmt* s,
    int at,
    char const* text)
{
    must(g_hiscores.db, sqlite3_bind_text(s, at, text, -1, SQLITE_TRANSIENT), "bind");
}

static void
bind_int(
    sqlite3_stmt* s,
    int at,
    long long value)
{
    must(g_hiscores.db, sqlite3_bind_int64(s, at, value), "bind");
}

static void
column_text(
    sqlite3_stmt* s,
    int column,
    char* out,
    size_t cap)
{
    unsigned char const* text = sqlite3_column_text(s, column);

    snprintf(out, cap, "%s", text ? (char const*)text : "");
}

/* ------------------------------------------------------------------ */
/* Players, scores, history                                            */
/* ------------------------------------------------------------------ */

/* The player saved as `key`, made when new: its id and whether it is ranked. */
static long long
player_ensure(
    char const* key,
    char const* name,
    long long now,
    int* out_ranked)
{
    sqlite3_stmt* s = st(ST_PLAYER_PUT);
    long long id;
    int found;

    bind_text(s, 1, key);
    bind_text(s, 2, name);
    bind_int(s, 3, now);
    run(s);
    s = st(ST_PLAYER_ID);
    bind_text(s, 1, key);
    found = step(s);
    /* The upsert above made it if it was not there. */
    assert(found);
    (void)found;
    id = sqlite3_column_int64(s, 0);
    *out_ranked = sqlite3_column_int(s, 1);
    sqlite3_reset(s);
    return id;
}

/* The player whose account is `account_name`, as the world names it,
 * shown as `display_name` (its account name when "", as before display
 * names). */
static long long
player_ensure_named(
    char const* account_name,
    char const* display_name,
    long long now,
    int* out_ranked)
{
    char key[HISCORES_NAME_MAX];

    ToriRSServer_SaveFileName(account_name, key, sizeof(key));
    /* The world only queues players that logged in, whose names were
     * usable to get there. */
    assert(key[0]);
    return player_ensure(key, display_name[0] ? display_name : account_name, now, out_ranked);
}

static void
player_set_ranked(
    long long id,
    int ranked)
{
    sqlite3_stmt* s = st(ST_PLAYER_RANKED);

    bind_int(s, 1, id);
    bind_int(s, 2, ranked);
    run(s);
    s = st(ST_SCORE_RANKED);
    bind_int(s, 1, id);
    bind_int(s, 2, ranked);
    run(s);
}

/* 1 when the score moved (or is new). */
static int
score_put(
    long long id,
    int category,
    long long score,
    long long now,
    int ranked)
{
    sqlite3_stmt* s = st(ST_SCORE_PUT);

    bind_int(s, 1, id);
    bind_int(s, 2, category);
    bind_int(s, 3, score);
    bind_int(s, 4, now);
    bind_int(s, 5, ranked);
    return run(s) > 0;
}

static void
xp_day_put(
    long long id,
    int stat,
    long long day,
    long long xp)
{
    sqlite3_stmt* s = st(ST_XP_DAY_PUT);

    bind_int(s, 1, id);
    bind_int(s, 2, stat);
    bind_int(s, 3, day);
    bind_int(s, 4, xp);
    run(s);
}

/* The listed skills' total experience, whole. */
static long long
total_xp(int const xp_tenths[TORIRSSERVER_HISCORESLOG_STATS])
{
    long long total = 0;

    for( int i = 0; i < HISCORES_SKILLS; i++ )
        total += xp_tenths[i] / 10;
    return total;
}

/* Every score from a stat table, and a day's row for each that moved. */
static void
stats_put(
    long long id,
    int ranked,
    long long now,
    int const xp_tenths[TORIRSSERVER_HISCORESLOG_STATS],
    int quest_points)
{
    long long day = now / 86400;
    int total_level = 0;

    for( int i = 0; i < TORIRSSERVER_HISCORESLOG_STATS; i++ )
    {
        long long xp = xp_tenths[i] / 10;

        if( score_put(id, i, xp, now, ranked) )
            xp_day_put(id, i, day, xp);
        if( i < HISCORES_SKILLS )
            total_level += HiscoresProto_LevelForXp(xp);
    }
    if( score_put(id, HISCORES_OVERALL,
                  HiscoresProto_OverallScore(total_level, total_xp(xp_tenths)), now, ranked) )
        xp_day_put(id, HISCORES_TOTAL_XP, day, total_xp(xp_tenths));
    score_put(id, HISCORES_QUEST_POINTS, quest_points, now, ranked);
}

/* A session: its gain over the listed skills, and the skill that gained most. */
static void
session_put(
    long long id,
    long long start,
    long long end,
    int const from_tenths[TORIRSSERVER_HISCORESLOG_STATS],
    int const to_tenths[TORIRSSERVER_HISCORESLOG_STATS])
{
    sqlite3_stmt* s;
    int top_stat = -1;
    long long top_gain = 0;

    for( int i = 0; i < HISCORES_SKILLS; i++ )
    {
        long long gain = to_tenths[i] / 10 - from_tenths[i] / 10;

        if( gain > top_gain )
        {
            top_gain = gain;
            top_stat = i;
        }
    }
    s = st(ST_SESSION_PUT);
    bind_int(s, 1, id);
    bind_int(s, 2, start);
    bind_int(s, 3, end);
    bind_int(s, 4, total_xp(to_tenths) - total_xp(from_tenths));
    bind_int(s, 5, top_stat);
    bind_int(s, 6, top_gain);
    run(s);
}

static void
record_apply(struct ToriRSServerHiscoresRecord const* r)
{
    int ranked;
    long long id = player_ensure_named(r->name, r->display, r->time, &ranked);

    switch( r->kind )
    {
    case TORIRSSERVER_HISCORESLOG_LOGIN:
        /* The account record's flag, as of this login: staff are not ranked. */
        if( ranked != !r->admin )
        {
            ranked = !r->admin;
            player_set_ranked(id, ranked);
        }
        stats_put(id, ranked, r->time, r->xp_tenths, r->quest_points);
        return;
    case TORIRSSERVER_HISCORESLOG_CHANGED:
        stats_put(id, ranked, r->time, r->xp_tenths, r->quest_points);
        return;
    case TORIRSSERVER_HISCORESLOG_LOGOUT:
        stats_put(id, ranked, r->time, r->xp_tenths, r->quest_points);
        session_put(id, r->session_start, r->time, r->session_xp_tenths, r->xp_tenths);
        return;
    case TORIRSSERVER_HISCORESLOG_EVENT:
    {
        sqlite3_stmt* s = st(ST_EVENT_PUT);

        bind_int(s, 1, id);
        bind_int(s, 2, r->time);
        bind_int(s, 3, r->event);
        bind_int(s, 4, r->amount);
        bind_text(s, 5, r->label);
        run(s);
        return;
    }
    }
    fprintf(stderr, "torirsserver: account log record of kind %d\n", (int)r->kind);
    abort();
}

/* The source's last applied record number; 0 before its first. */
static long long
source_seq(char const* source)
{
    sqlite3_stmt* s = st(ST_SOURCE_GET);
    long long seq = 0;

    bind_text(s, 1, source);
    if( step(s) )
        seq = sqlite3_column_int64(s, 0);
    sqlite3_reset(s);
    return seq;
}

long long
HiscoresDb_ApplyBatch(
    const char* source,
    struct ToriRSServerHiscoresRecord const* records,
    long long const* seqs,
    int count)
{
    long long last;
    sqlite3_stmt* s;

    assert(source);
    assert(count >= 0);
    if( count == 0 )
        return source_seq(source);
    assert(records);
    assert(seqs);
    exec(hiscores_db(), "BEGIN IMMEDIATE");
    last = source_seq(source);
    for( int i = 0; i < count; i++ )
    {
        if( seqs[i] <= last )
            continue;
        record_apply(&records[i]);
        last = seqs[i];
    }
    s = st(ST_SOURCE_PUT);
    bind_text(s, 1, source);
    bind_int(s, 2, last);
    run(s);
    exec(g_hiscores.db, "COMMIT");
    return last;
}

void
HiscoresDb_SetRanked(
    const char* display_name,
    int ranked)
{
    char key[HISCORES_NAME_MAX];
    sqlite3_stmt* s;
    long long id;

    assert(display_name);
    ToriRSServer_SaveFileName(display_name, key, sizeof(key));
    s = st(ST_PLAYER_ID);
    bind_text(s, 1, key);
    if( !step(s) )
    {
        sqlite3_reset(s);
        return;
    }
    id = sqlite3_column_int64(s, 0);
    sqlite3_reset(s);
    player_set_ranked(id, ranked);
}

/* ------------------------------------------------------------------ */
/* Reads                                                               */
/* ------------------------------------------------------------------ */

int
HiscoresDb_Player(
    const char* key,
    struct HiscoresPlayer* out)
{
    sqlite3_stmt* s;

    assert(key);
    assert(out);
    memset(out, 0, sizeof(*out));
    s = st(ST_PLAYER_GET);
    bind_text(s, 1, key);
    if( !step(s) )
    {
        sqlite3_reset(s);
        return 0;
    }
    out->id = sqlite3_column_int64(s, 0);
    column_text(s, 1, out->key, sizeof(out->key));
    column_text(s, 2, out->name, sizeof(out->name));
    out->ranked = sqlite3_column_int(s, 3);
    out->last_seen = sqlite3_column_int64(s, 4);
    sqlite3_reset(s);

    s = st(ST_SCORES_GET);
    bind_int(s, 1, out->id);
    while( step(s) )
    {
        int category = sqlite3_column_int(s, 0);
        long long score = sqlite3_column_int64(s, 1);

        if( category >= 0 && category < TORIRSSERVER_HISCORESLOG_STATS )
            out->xp[category] = score;
        else if( category == HISCORES_QUEST_POINTS )
            out->quest_points = (int)score;
    }
    sqlite3_reset(s);
    return 1;
}

int
HiscoresDb_Hiscores(
    int category,
    int first,
    int max,
    struct HiscoresRow* out)
{
    sqlite3_stmt* s;
    int count = 0;

    assert(out);
    assert(first >= 0);
    s = st(ST_HISCORES);
    bind_int(s, 1, category);
    bind_int(s, 2, first);
    bind_int(s, 3, max);
    while( count < max && step(s) )
    {
        column_text(s, 0, out[count].key, sizeof(out[count].key));
        column_text(s, 1, out[count].name, sizeof(out[count].name));
        out[count].score = sqlite3_column_int64(s, 2);
        count++;
    }
    sqlite3_reset(s);
    return count;
}

int
HiscoresDb_RankedCount(int category)
{
    sqlite3_stmt* s = st(ST_RANKED_COUNT);
    int count;

    bind_int(s, 1, category);
    step(s);
    count = sqlite3_column_int(s, 0);
    sqlite3_reset(s);
    return count;
}

int
HiscoresDb_Rank(
    long long player,
    int category)
{
    sqlite3_stmt* s = st(ST_RANK_SELF);
    long long score;
    long long since;
    int rank;

    bind_int(s, 1, player);
    bind_int(s, 2, category);
    if( !step(s) || !sqlite3_column_int(s, 2) )
    {
        sqlite3_reset(s);
        return 0;
    }
    score = sqlite3_column_int64(s, 0);
    since = sqlite3_column_int64(s, 1);
    sqlite3_reset(s);

    s = st(ST_RANK_AHEAD);
    bind_int(s, 1, category);
    bind_int(s, 2, score);
    bind_int(s, 3, since);
    bind_int(s, 4, player);
    step(s);
    rank = sqlite3_column_int(s, 0) + 1;
    sqlite3_reset(s);
    return rank;
}

long long
HiscoresDb_XpAt(
    long long player,
    int stat,
    long long day)
{
    sqlite3_stmt* s = st(ST_XP_AT);
    long long xp = -1;

    bind_int(s, 1, player);
    bind_int(s, 2, stat);
    bind_int(s, 3, day);
    if( step(s) )
        xp = sqlite3_column_int64(s, 0);
    sqlite3_reset(s);
    return xp;
}

long long
HiscoresDb_XpFirstDay(
    long long player,
    int stat)
{
    sqlite3_stmt* s = st(ST_XP_FIRST);
    long long day = -1;

    bind_int(s, 1, player);
    bind_int(s, 2, stat);
    if( step(s) )
        day = sqlite3_column_int64(s, 0);
    sqlite3_reset(s);
    return day;
}

int
HiscoresDb_XpDays(
    long long player,
    int stat,
    long long* out_day,
    long long* out_xp,
    int max)
{
    sqlite3_stmt* s;
    int count = 0;

    assert(out_day);
    assert(out_xp);
    s = st(ST_XP_DAYS);
    bind_int(s, 1, player);
    bind_int(s, 2, stat);
    bind_int(s, 3, max);
    while( count < max && step(s) )
    {
        out_day[count] = sqlite3_column_int64(s, 0);
        out_xp[count] = sqlite3_column_int64(s, 1);
        count++;
    }
    sqlite3_reset(s);
    /* Read newest first (the LIMIT keeps the newest); handed back oldest first. */
    for( int i = 0; i < count / 2; i++ )
    {
        long long day = out_day[i];
        long long xp = out_xp[i];

        out_day[i] = out_day[count - 1 - i];
        out_xp[i] = out_xp[count - 1 - i];
        out_day[count - 1 - i] = day;
        out_xp[count - 1 - i] = xp;
    }
    return count;
}

int
HiscoresDb_Sessions(
    long long player,
    struct HiscoresSession* out,
    int max,
    int* out_count,
    long long* out_played)
{
    sqlite3_stmt* s;
    int count = 0;

    assert(out);
    assert(out_count);
    assert(out_played);
    s = st(ST_SESSION_TOTALS);
    bind_int(s, 1, player);
    step(s);
    *out_count = sqlite3_column_int(s, 0);
    *out_played = sqlite3_column_int64(s, 1);
    sqlite3_reset(s);

    s = st(ST_SESSIONS);
    bind_int(s, 1, player);
    bind_int(s, 2, max);
    while( count < max && step(s) )
    {
        out[count].start = sqlite3_column_int64(s, 0);
        out[count].end = sqlite3_column_int64(s, 1);
        out[count].xp_gained = sqlite3_column_int64(s, 2);
        out[count].top_stat = sqlite3_column_int(s, 3);
        out[count].top_gain = sqlite3_column_int64(s, 4);
        count++;
    }
    sqlite3_reset(s);
    return count;
}

int
HiscoresDb_Events(
    long long player,
    struct HiscoresEvent* out,
    int max)
{
    sqlite3_stmt* s;
    int count = 0;

    assert(out);
    s = st(ST_EVENTS);
    bind_int(s, 1, player);
    bind_int(s, 2, max);
    while( count < max && step(s) )
    {
        out[count].time = sqlite3_column_int64(s, 0);
        out[count].kind = (enum ToriRSServerAdventureKind)sqlite3_column_int(s, 1);
        out[count].amount = sqlite3_column_int(s, 2);
        column_text(s, 3, out[count].label, sizeof(out[count].label));
        count++;
    }
    sqlite3_reset(s);
    return count;
}

int
HiscoresDb_PlayerView(
    const char* key,
    int history,
    struct HiscoresPlayerView* out)
{
    long long today = (long long)time(NULL) / 86400;
    long long total = 0;

    assert(key);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !HiscoresDb_Player(key, &out->player) )
        return 0;
    out->rank_overall = HiscoresDb_Rank(out->player.id, HISCORES_OVERALL);
    for( int i = 0; i < HISCORES_SKILLS; i++ )
    {
        out->rank[i] = HiscoresDb_Rank(out->player.id, i);
        total += out->player.xp[i];
    }
    out->event_count = HiscoresDb_Events(out->player.id, out->events, HISCORES_EVENTS_SHOWN);
    if( !history )
        return 1;

    out->has_history = HiscoresDb_XpFirstDay(out->player.id, HISCORES_TOTAL_XP) >= 0;
    for( int w = 0; w < HISCORES_GAIN_WINDOWS; w++ )
    {
        long long day = today - g_hiscores_gain_window_days[w];

        out->gain_partial[w] = HiscoresDb_XpAt(out->player.id, HISCORES_TOTAL_XP, day) < 0;
        for( int i = 0; i <= HISCORES_SKILLS; i++ )
        {
            int stat = i == HISCORES_SKILLS ? HISCORES_TOTAL_XP : i;
            long long now = i == HISCORES_SKILLS ? total : out->player.xp[i];
            long long then = HiscoresDb_XpAt(out->player.id, stat, day);

            /* Older than the history: from its first day, or no gain at all
             * for a stat it never recorded. */
            if( then < 0 )
            {
                long long first = HiscoresDb_XpFirstDay(out->player.id, stat);

                then = first < 0 ? now : HiscoresDb_XpAt(out->player.id, stat, first);
            }
            out->gain_base[w][i] = then;
        }
    }
    out->chart_count = HiscoresDb_XpDays(out->player.id, HISCORES_TOTAL_XP, out->chart_day,
                                        out->chart_xp, HISCORES_CHART_DAYS);
    out->session_count = HiscoresDb_Sessions(out->player.id, out->sessions, HISCORES_SESSIONS_SHOWN,
                                            &out->session_total, &out->played);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The one-time import of the files this replaced                      */
/* ------------------------------------------------------------------ */

/*
 * An INI file's lines to `each(section, key, value, ctx)`, trimmed. 0 when
 * the file cannot be opened. These files are the server's own output, read
 * once; a line that is not `key = value` is skipped.
 */
static int
ini_each(
    char const* path,
    void (*each)(char const* section, char const* key, char const* value, void* ctx),
    void* ctx)
{
    char line[512];
    char section[64] = "";
    FILE* file = fopen(path, "rb");

    if( !file )
        return 0;
    while( fgets(line, sizeof(line), file) )
    {
        char* eq;
        char* value;

        line[strcspn(line, "\r\n")] = '\0';
        if( line[0] == '[' )
        {
            snprintf(section, sizeof(section), "%.*s", (int)strcspn(line + 1, "]"), line + 1);
            continue;
        }
        if( line[0] == ';' || !(eq = strchr(line, '=')) )
            continue;
        *eq = '\0';
        value = eq + 1;
        while( *value == ' ' )
            value++;
        for( char* trim = eq - 1; trim >= line && *trim == ' '; trim-- )
            *trim = '\0';
        each(section, line, value, ctx);
    }
    fclose(file);
    return 1;
}

/* `dir`'s `*.ini` stems, each with its mtime. */
static void
dir_each_ini(
    char const* dir,
    void (*each)(char const* dir, char const* stem, long long mtime))
{
    DIR* handle = opendir(dir);
    struct dirent* entry;

    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char stem[HISCORES_NAME_MAX];
        char path[1024];
        struct stat info;
        size_t len = strlen(entry->d_name);

        if( len <= 4 || len - 4 >= sizeof(stem) || strcmp(entry->d_name + len - 4, ".ini") != 0 )
            continue;
        snprintf(stem, sizeof(stem), "%.*s", (int)(len - 4), entry->d_name);
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &info) != 0 || !S_ISREG(info.st_mode) )
            continue;
        each(dir, stem, (long long)info.st_mtime);
    }
    closedir(handle);
}

struct ImportFields
{
    char name[HISCORES_NAME_MAX];
    int xp_tenths[TORIRSSERVER_HISCORESLOG_STATS];
    int quest_points;
};

static void
import_field(
    char const* section,
    char const* key,
    char const* value,
    void* ctx)
{
    struct ImportFields* f = ctx;

    /* A character save: the name it shows, a `display_name` (after `name` in
     * the file) over the account's own. */
    if( strcmp(section, "player") == 0 &&
        (strcmp(key, "name") == 0 || strcmp(key, "display_name") == 0) )
        snprintf(f->name, sizeof(f->name), "%s", value);
    else if( strcmp(section, "stats") == 0 )
    {
        /* `<stat> = <boosted> <xp_tenths>` */
        int stat = atoi(key);
        char const* xp = strchr(value, ' ');

        if( stat >= 0 && stat < TORIRSSERVER_HISCORESLOG_STATS && xp )
            f->xp_tenths[stat] = atoi(xp + 1);
    }
    else if( strcmp(section, "varps") == 0 && atoi(key) == ACCOUNTS_VARP_QP )
        f->quest_points = atoi(value);
}

/* history/<key>.log: each logout's table as that day's experience, and each
 * login-logout pair as a session. Before the save's own stats, which are
 * newer and overwrite the last day. */
static void
import_history(
    long long id,
    char const* key)
{
    static struct ToriRSServerHistory history;
    struct ToriRSServerHistoryEntry const* previous = NULL;
    long long login = 0;

    ToriRSServer_HistoryRead(key, &history);
    for( int i = 0; i < history.count; i++ )
    {
        struct ToriRSServerHistoryEntry const* e = &history.entries[i];
        int tenths[TORIRSSERVER_HISCORESLOG_STATS];
        long long day = e->time / 86400;

        if( e->kind == TORIRSSERVER_HISTORY_LOGIN )
        {
            login = e->time;
            continue;
        }
        for( int s = 0; s < TORIRSSERVER_HISCORESLOG_STATS; s++ )
        {
            tenths[s] = (int)e->xp_tenths[s];
            xp_day_put(id, s, day, e->xp_tenths[s] / 10);
        }
        xp_day_put(id, HISCORES_TOTAL_XP, day, total_xp(tenths));
        if( previous )
        {
            int from[TORIRSSERVER_HISCORESLOG_STATS];

            for( int s = 0; s < TORIRSSERVER_HISCORESLOG_STATS; s++ )
                from[s] = (int)previous->xp_tenths[s];
            session_put(id, login, e->time, from, tenths);
        }
        else
            /* The first logout has nothing to measure its gain against. */
            session_put(id, login, e->time, tenths, tenths);
        login = 0;
        previous = e;
    }
}

static void
import_player(
    char const* dir,
    char const* stem,
    long long mtime)
{
    struct ImportFields f;
    char path[1024];
    long long id;
    int ranked;

    memset(&f, 0, sizeof(f));
    f.xp_tenths[ACCOUNTS_HITPOINTS] = ACCOUNTS_FRESH_HITPOINTS_TENTHS;
    snprintf(path, sizeof(path), "%s/%s.ini", dir, stem);
    if( !ini_each(path, import_field, &f) || !f.name[0] )
        return;
    id = player_ensure(stem, f.name, mtime, &ranked);
    if( ToriRSServer_AuthIsAdmin(stem) )
    {
        ranked = 0;
        player_set_ranked(id, 0);
    }
    import_history(id, stem);
    stats_put(id, ranked, mtime, f.xp_tenths, f.quest_points);
}

static void
hiscores_import(int from_accounts)
{
    /* What the account database (before the split) held, as it held it. */
    if( from_accounts )
    {
        exec(g_hiscores.db, "INSERT INTO player SELECT * FROM old.player;"
                            "INSERT INTO score SELECT * FROM old.score;"
                            "INSERT INTO xp_day SELECT * FROM old.xp_day;"
                            "INSERT INTO session SELECT * FROM old.session;"
                            "INSERT INTO event SELECT * FROM old.event;"
                            "INSERT INTO source SELECT * FROM old.source;");
        return;
    }
    dir_each_ini(ToriRSServer_SaveDir(), import_player);
}
