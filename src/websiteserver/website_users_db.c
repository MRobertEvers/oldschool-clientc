/*
 * The website's user accounts: web logins and invites. See website_users_db.h.
 */
#include "website_users_db.h"

#include "torirsserver/torirs_server_savefile.h"

#include <sqlite3.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define USERS_SCHEMA_VERSION 1
#define USERS_NAME_MAX 64

static char const k_schema[] =
    "CREATE TABLE web_login ("
    " key TEXT PRIMARY KEY, name TEXT NOT NULL, password TEXT NOT NULL,"
    " created_at INTEGER NOT NULL) WITHOUT ROWID;"
    "CREATE TABLE invite ("
    " code TEXT PRIMARY KEY, name TEXT, created_at INTEGER NOT NULL) WITHOUT ROWID;";

enum UsersStatement
{
    ST_LOGIN_GET,
    ST_LOGIN_PUT,
    ST_LOGIN_DROP,
    ST_LOGIN_LIST,
    ST_INVITE_PUT,
    ST_INVITE_GET,
    ST_INVITE_DROP,
    ST_COUNT,
};

static char const* const k_sql[ST_COUNT] = {
    [ST_LOGIN_GET] = "SELECT password FROM web_login WHERE key = ?1",
    [ST_LOGIN_PUT] = ("INSERT OR IGNORE INTO web_login (key, name, password, created_at)"
                      " VALUES (?1, ?2, ?3, ?4)"),
    [ST_LOGIN_DROP] = "DELETE FROM web_login WHERE key = ?1",
    [ST_LOGIN_LIST] = "SELECT name FROM web_login ORDER BY created_at, key",
    [ST_INVITE_PUT] = "INSERT OR IGNORE INTO invite (code, name, created_at) VALUES (?1, ?2, ?3)",
    [ST_INVITE_GET] = "SELECT name FROM invite WHERE code = ?1",
    [ST_INVITE_DROP] = "DELETE FROM invite WHERE code = ?1",
};

static struct
{
    sqlite3* db;
    sqlite3_stmt* st[ST_COUNT];
} g_users;

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
        fprintf(stderr, "website_server: user database: %s: %s\n", what,
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

static void
wipe(
    void* data,
    size_t len)
{
    volatile unsigned char* p = data;

    while( len-- )
        *p++ = 0;
}

const char*
WebsiteUsersDb_Path(void)
{
    static char path[1024];

    snprintf(path, sizeof(path), "%s/website.sqlite", ToriRSServer_SaveDir());
    return path;
}

static void users_import(int from_accounts);

static sqlite3*
users_db(void)
{
    char const* path;
    char old_path[1024];
    sqlite3_stmt* version;
    int schema;
    int attached = 0;
    struct stat info;

    if( g_users.db )
        return g_users.db;
    path = WebsiteUsersDb_Path();
    ToriRSServer_SaveMkdirParent(path);
    snprintf(old_path, sizeof(old_path), "%s/accounts.sqlite", ToriRSServer_SaveDir());
    must(NULL,
         sqlite3_open_v2(path, &g_users.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL),
         "open");
#ifndef _WIN32
    /* Password records and invite codes: owner-only, like the key files. */
    chmod(path, 0600);
#endif
    sqlite3_busy_timeout(g_users.db, 10000);
    exec(g_users.db, "PRAGMA journal_mode = WAL");
    exec(g_users.db, "PRAGMA synchronous = NORMAL");
    /* The database before the split, if there is one: the first open of this
     * one copies its rows out of it. Attached outside the transaction, which
     * sqlite requires, and only while this one is new. */
    if( stat(old_path, &info) == 0 )
    {
        char* sql = sqlite3_mprintf("ATTACH DATABASE %Q AS old", old_path);

        assert(sql);
        exec(g_users.db, sql);
        sqlite3_free(sql);
        attached = 1;
    }

    /* EXCLUSIVE, then read the version: two processes opening a new file at
     * once must not both make it. */
    exec(g_users.db, "BEGIN EXCLUSIVE");
    must(g_users.db,
         sqlite3_prepare_v2(g_users.db, "PRAGMA main.user_version", -1, &version, NULL),
         "prepare user_version");
    must(g_users.db, sqlite3_step(version), "user_version");
    schema = sqlite3_column_int(version, 0);
    sqlite3_finalize(version);
    if( schema == 0 )
    {
        exec(g_users.db, k_schema);
    }
    if( schema > USERS_SCHEMA_VERSION )
    {
        fprintf(stderr, "website_server: %s is schema %d; this build knows %d\n", path, schema,
                USERS_SCHEMA_VERSION);
        abort();
    }
    for( int i = 0; i < ST_COUNT; i++ )
        must(g_users.db,
             sqlite3_prepare_v2(g_users.db, k_sql[i], -1, &g_users.st[i], NULL), k_sql[i]);
    if( schema == 0 )
    {
        users_import(attached);
        fprintf(stderr, "website_server: %s made, from %s\n", path,
                attached ? old_path : "the gate's login and invite files");
    }
    if( schema < USERS_SCHEMA_VERSION )
    {
        char pragma[64];

        snprintf(pragma, sizeof(pragma), "PRAGMA main.user_version = %d", USERS_SCHEMA_VERSION);
        exec(g_users.db, pragma);
    }
    exec(g_users.db, "COMMIT");
    if( attached )
        exec(g_users.db, "DETACH DATABASE old");
    return g_users.db;
}

void
WebsiteUsersDb_Open(void)
{
    users_db();
}

void
WebsiteUsersDb_Close(void)
{
    if( !g_users.db )
        return;
    for( int i = 0; i < ST_COUNT; i++ )
        sqlite3_finalize(g_users.st[i]);
    sqlite3_close(g_users.db);
    memset(&g_users, 0, sizeof(g_users));
}

/* The statement, reset and unbound, ready for its arguments. */
static sqlite3_stmt*
st(enum UsersStatement which)
{
    sqlite3_stmt* s;

    users_db();
    s = g_users.st[which];
    sqlite3_reset(s);
    sqlite3_clear_bindings(s);
    return s;
}

/* One step of `s`: 1 for a row, 0 for the end. Aborts on an error. */
static int
step(sqlite3_stmt* s)
{
    int rc = sqlite3_step(s);

    must(g_users.db, rc, sqlite3_sql(s));
    return rc == SQLITE_ROW;
}

/* Run a statement that returns nothing; the rows it changed. */
static int
run(sqlite3_stmt* s)
{
    step(s);
    sqlite3_reset(s);
    return sqlite3_changes(g_users.db);
}

static void
bind_text(
    sqlite3_stmt* s,
    int at,
    char const* text)
{
    must(g_users.db, sqlite3_bind_text(s, at, text, -1, SQLITE_TRANSIENT), "bind");
}

static void
bind_int(
    sqlite3_stmt* s,
    int at,
    long long value)
{
    must(g_users.db, sqlite3_bind_int64(s, at, value), "bind");
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
/* Web logins and invites                                              */
/* ------------------------------------------------------------------ */

/* An invite code arrives in a URL: exactly a code's worth of lowercase hex,
 * or it is not one. */
static int
invite_code_ok(char const* code)
{
    size_t len = strlen(code);

    if( len != TORIRSSERVER_INVITE_CODE_BYTES * 2 )
        return 0;
    for( size_t i = 0; i < len; i++ )
        if( !isdigit((unsigned char)code[i]) && (code[i] < 'a' || code[i] > 'f') )
            return 0;
    return 1;
}

/* Whether a web login is keyed `key`. */
static int
login_exists(char const* key)
{
    sqlite3_stmt* s = st(ST_LOGIN_GET);
    int found;

    bind_text(s, 1, key);
    found = step(s);
    sqlite3_reset(s);
    return found;
}

static void
login_put(
    char const* key,
    char const* name,
    char const* password,
    long long now)
{
    char record[TORIRSSERVER_AUTH_RECORD_MAX];
    uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES];
    sqlite3_stmt* s;

    ToriRSServer_AuthRandom(salt, sizeof(salt));
    ToriRSServer_AuthRecord(password, salt, TORIRSSERVER_AUTH_ITERATIONS, record);
    s = st(ST_LOGIN_PUT);
    bind_text(s, 1, key);
    bind_text(s, 2, name);
    bind_text(s, 3, record);
    bind_int(s, 4, now);
    run(s);
    wipe(record, sizeof(record));
}

enum ToriRSServerAuthResult
WebsiteUsersDb_GateLogin(
    const char* name,
    const char* password)
{
    char key[USERS_NAME_MAX];
    char record[TORIRSSERVER_AUTH_RECORD_MAX];
    sqlite3_stmt* s;
    int ok;

    assert(name);
    assert(password);
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] || !password[0] )
        return TORIRSSERVER_AUTH_UNUSABLE;
    s = st(ST_LOGIN_GET);
    bind_text(s, 1, key);
    /* Never a claim: every web login was made by an invite or an operator. */
    if( !step(s) )
    {
        sqlite3_reset(s);
        return TORIRSSERVER_AUTH_REFUSED;
    }
    column_text(s, 0, record, sizeof(record));
    sqlite3_reset(s);
    ok = ToriRSServer_AuthVerify(password, record);
    wipe(record, sizeof(record));
    return ok ? TORIRSSERVER_AUTH_OK : TORIRSSERVER_AUTH_REFUSED;
}

enum ToriRSServerCreateResult
WebsiteUsersDb_GateLoginCreate(
    const char* name,
    const char* password,
    const char** out_problem)
{
    char key[USERS_NAME_MAX];

    assert(name);
    assert(password);
    assert(out_problem);
    *out_problem = ToriRSServer_AuthNameProblem(name);
    if( *out_problem )
        return TORIRSSERVER_CREATE_BAD_NAME;
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    assert(key[0]); /* AuthNameProblem passed, so the name has a letter */
    if( login_exists(key) )
        return TORIRSSERVER_CREATE_EXISTS;
    *out_problem = ToriRSServer_AuthPasswordProblem(name, password);
    if( *out_problem )
        return TORIRSSERVER_CREATE_WEAK;
    login_put(key, name, password, (long long)time(NULL));
    return TORIRSSERVER_CREATE_OK;
}

int
WebsiteUsersDb_GateLoginRevoke(const char* name)
{
    char key[USERS_NAME_MAX];
    sqlite3_stmt* s;

    assert(name);
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    s = st(ST_LOGIN_DROP);
    bind_text(s, 1, key);
    return run(s) > 0;
}

void
WebsiteUsersDb_GateLoginEach(
    void (*each)(const char* name, void* ctx),
    void* ctx)
{
    sqlite3_stmt* s = st(ST_LOGIN_LIST);
    char name[USERS_NAME_MAX];

    assert(each);
    while( step(s) )
    {
        column_text(s, 0, name, sizeof(name));
        each(name, ctx);
    }
    sqlite3_reset(s);
}

void
WebsiteUsersDb_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1])
{
    uint8_t bytes[TORIRSSERVER_INVITE_CODE_BYTES];
    sqlite3_stmt* s;

    assert(!name || !ToriRSServer_AuthNameProblem(name));
    assert(out_code);
    ToriRSServer_AuthRandom(bytes, sizeof(bytes));
    ToriRSServer_AuthHex(bytes, sizeof(bytes), out_code);
    s = st(ST_INVITE_PUT);
    bind_text(s, 1, out_code);
    if( name )
        bind_text(s, 2, name);
    bind_int(s, 3, (long long)time(NULL));
    run(s);
}

int
WebsiteUsersDb_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap)
{
    sqlite3_stmt* s;
    int found;

    assert(code);
    assert(out_name);
    if( !invite_code_ok(code) )
        return 0;
    s = st(ST_INVITE_GET);
    bind_text(s, 1, code);
    found = step(s);
    if( found )
        column_text(s, 0, out_name, cap);
    sqlite3_reset(s);
    return found;
}

enum ToriRSServerInviteResult
WebsiteUsersDb_InviteRedeem(
    const char* code,
    const char* name,
    const char* password,
    const char** out_problem)
{
    char fixed[USERS_NAME_MAX];
    char want[USERS_NAME_MAX];
    char key[USERS_NAME_MAX];
    enum ToriRSServerInviteResult result = TORIRSSERVER_INVITE_OK;
    sqlite3_stmt* s;

    assert(code);
    assert(name);
    assert(password);
    assert(out_problem);
    *out_problem = NULL;
    /* IMMEDIATE: the read and the spend are one write transaction, so two
     * visitors redeeming one code cannot both find it unspent. */
    exec(users_db(), "BEGIN IMMEDIATE");
    if( !WebsiteUsersDb_InviteLookup(code, fixed, sizeof(fixed)) )
        result = TORIRSSERVER_INVITE_UNKNOWN;
    if( result == TORIRSSERVER_INVITE_OK && fixed[0] )
    {
        ToriRSServer_SaveFileName(fixed, want, sizeof(want));
        ToriRSServer_SaveFileName(name, key, sizeof(key));
        if( strcmp(want, key) != 0 )
            result = TORIRSSERVER_INVITE_WRONG_NAME;
    }
    if( result == TORIRSSERVER_INVITE_OK )
    {
        *out_problem = ToriRSServer_AuthNameProblem(name);
        if( *out_problem )
            result = TORIRSSERVER_INVITE_BAD_NAME;
    }
    if( result == TORIRSSERVER_INVITE_OK )
    {
        ToriRSServer_SaveFileName(name, key, sizeof(key));
        assert(key[0]); /* AuthNameProblem passed, so the name has a letter */
        if( login_exists(key) )
            result = TORIRSSERVER_INVITE_NAME_TAKEN;
    }
    if( result == TORIRSSERVER_INVITE_OK )
    {
        *out_problem = ToriRSServer_AuthPasswordProblem(name, password);
        if( *out_problem )
            result = TORIRSSERVER_INVITE_WEAK;
    }
    if( result != TORIRSSERVER_INVITE_OK )
    {
        exec(g_users.db, "ROLLBACK");
        return result;
    }
    login_put(key, name, password, (long long)time(NULL));
    s = st(ST_INVITE_DROP);
    bind_text(s, 1, code);
    run(s);
    exec(g_users.db, "COMMIT");
    return TORIRSSERVER_INVITE_OK;
}

/* ------------------------------------------------------------------ */
/* Made once                                                           */
/* ------------------------------------------------------------------ */

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
        char stem[USERS_NAME_MAX];
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
    char want_section[16];
    char password[TORIRSSERVER_AUTH_RECORD_MAX];
    char name[USERS_NAME_MAX];
};

/* A web login's [account] or an invite's [invite]. */
static void
import_field(
    char const* section,
    char const* key,
    char const* value,
    void* ctx)
{
    struct ImportFields* f = ctx;

    if( strcmp(section, f->want_section) != 0 )
        return;
    if( strcmp(key, "password") == 0 )
        snprintf(f->password, sizeof(f->password), "%s", value);
    else if( strcmp(key, "name") == 0 )
        snprintf(f->name, sizeof(f->name), "%s", value);
}

static void
import_login(
    char const* dir,
    char const* stem,
    long long mtime)
{
    struct ImportFields f;
    char path[1024];
    sqlite3_stmt* s;

    memset(&f, 0, sizeof(f));
    snprintf(f.want_section, sizeof(f.want_section), "account");
    snprintf(path, sizeof(path), "%s/%s.ini", dir, stem);
    if( !ini_each(path, import_field, &f) || !f.password[0] )
        return;
    s = st(ST_LOGIN_PUT);
    bind_text(s, 1, stem);
    bind_text(s, 2, stem);
    bind_text(s, 3, f.password);
    bind_int(s, 4, mtime);
    run(s);
    wipe(&f, sizeof(f));
}

static void
import_invite(
    char const* dir,
    char const* stem,
    long long mtime)
{
    struct ImportFields f;
    char path[1024];
    sqlite3_stmt* s;

    if( !invite_code_ok(stem) )
        return;
    memset(&f, 0, sizeof(f));
    snprintf(f.want_section, sizeof(f.want_section), "invite");
    snprintf(path, sizeof(path), "%s/%s.ini", dir, stem);
    if( !ini_each(path, import_field, &f) )
        return;
    s = st(ST_INVITE_PUT);
    bind_text(s, 1, stem);
    if( f.name[0] )
        bind_text(s, 2, f.name);
    bind_int(s, 3, mtime);
    run(s);
}

static void
users_import(int from_accounts)
{
    char dir[1024];

    /* What the account database (before the split) held, as it held it. */
    if( from_accounts )
    {
        exec(g_users.db, "INSERT INTO web_login SELECT * FROM old.web_login;"
                         "INSERT INTO invite SELECT * FROM old.invite;");
        return;
    }
    snprintf(dir, sizeof(dir), "%s/gate", ToriRSServer_SaveDir());
    dir_each_ini(dir, import_login);
    snprintf(dir, sizeof(dir), "%s/gate/invites", ToriRSServer_SaveDir());
    dir_each_ini(dir, import_invite);
}
