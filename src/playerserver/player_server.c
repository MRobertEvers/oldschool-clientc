/*
 * The player account server's routes. See player_server.h.
 */
#include "player_server.h"

#include "mem/torirs_mem.h"
#include "player_lease.h"
#include "player_proto.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_savefile.h"

#include <assert.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE ps_lockfile;
typedef CRITICAL_SECTION ps_mutex;
#define ps_mutex_init(m) InitializeCriticalSection(m)
#define ps_mutex_destroy(m) DeleteCriticalSection(m)
#define ps_lock(m) EnterCriticalSection(m)
#define ps_unlock(m) LeaveCriticalSection(m)
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
#include <unistd.h>
typedef int ps_lockfile;
typedef pthread_mutex_t ps_mutex;
#define ps_mutex_init(m) pthread_mutex_init((m), NULL)
#define ps_mutex_destroy(m) pthread_mutex_destroy(m)
#define ps_lock(m) pthread_mutex_lock(m)
#define ps_unlock(m) pthread_mutex_unlock(m)
#endif

/* One account's requests run one at a time (a claim and a link of the same
 * name must not interleave their writes); accounts that hash apart run in
 * parallel. */
#define PS_ACCOUNT_STRIPES 64

static struct
{
    int open;
    ps_lockfile lockfile;
    ps_mutex leases_lock;
    ps_mutex names_lock;
    ps_mutex stripes[PS_ACCOUNT_STRIPES];
    struct PlayerLeases leases;
} g_ps;

/* ------------------------------------------------------------------ */
/* The answer                                                          */
/* ------------------------------------------------------------------ */

struct Answer
{
    char* data;
    size_t len;
    size_t cap;
};

static void
answer_init(struct Answer* a)
{
    a->cap = 512;
    a->len = 0;
    a->data = ToriMem_Alloc(ToriMem_Net(), a->cap);
    assert(a->data);
    a->data[0] = '\0';
}

static void
answer_printf(
    struct Answer* a,
    char const* fmt,
    ...)
{
    for( ;; )
    {
        va_list args;
        int n;

        va_start(args, fmt);
        n = vsnprintf(a->data + a->len, a->cap - a->len, fmt, args);
        va_end(args);
        assert(n >= 0);
        if( (size_t)n < a->cap - a->len )
        {
            a->len += (size_t)n;
            return;
        }
        a->cap = a->cap * 2 + (size_t)n;
        a->data = ToriMem_Realloc(ToriMem_Net(), a->data, a->cap);
        assert(a->data);
    }
}

static void
answer_bytes(
    struct Answer* a,
    char const* bytes,
    size_t len)
{
    if( len == 0 )
        return;
    assert(bytes);
    if( a->len + len + 1 > a->cap )
    {
        a->cap = a->len + len + 1;
        a->data = ToriMem_Realloc(ToriMem_Net(), a->data, a->cap);
        assert(a->data);
    }
    memcpy(a->data + a->len, bytes, len);
    a->len += len;
    a->data[a->len] = '\0';
}

static void
answer_send(
    struct Answer* a,
    struct HttpResponse* res,
    int status)
{
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "text/plain; charset=utf-8");
    res->body = a->data;
    res->body_len = (int)a->len;
    res->owns_body = 1;
}

static void
refuse(
    struct HttpResponse* res,
    int status,
    char const* why)
{
    struct Answer a;

    answer_init(&a);
    answer_printf(&a, "error\t%s\n", why);
    answer_send(&a, res, status);
}

/* `result <n>`, with a problem sentence when there is one. */
static void
answer_result(
    struct HttpResponse* res,
    int result,
    char const* problem)
{
    struct Answer a;
    char clean[256];

    answer_init(&a);
    PlayerProto_Clean(problem ? problem : "", clean, sizeof(clean));
    answer_printf(&a, "result\t%d\t%s\n", result, clean);
    answer_send(&a, res, 200);
}

/* ------------------------------------------------------------------ */
/* Fields                                                              */
/* ------------------------------------------------------------------ */

struct Fields
{
    char const* data;
    size_t len;
};

static struct Fields
fields_of(struct HttpRequest const* req)
{
    struct Fields f;
    char const* query;

    if( strcmp(req->method, "GET") == 0 )
    {
        query = strchr(req->path, '?');
        f.data = query ? query + 1 : NULL;
        f.len = query ? strlen(query + 1) : 0;
    }
    else
    {
        f.data = (char const*)req->body;
        f.len = req->body ? (size_t)req->body_len : 0;
    }
    return f;
}

static int
field(
    struct Fields const* f,
    char const* name,
    char* out,
    size_t cap)
{
    return PlayerProto_FormGet(f->data, f->len, name, out, cap);
}

static int
route_is(
    struct HttpRequest const* req,
    char const* method,
    char const* route)
{
    size_t len = strlen(route);

    return strcmp(req->method, method) == 0 && strncmp(req->path, route, len) == 0 &&
           (req->path[len] == '\0' || req->path[len] == '?');
}

/* ------------------------------------------------------------------ */
/* Locks                                                               */
/* ------------------------------------------------------------------ */

static ps_mutex*
stripe_of(char const* key)
{
    uint32_t hash = 2166136261u;

    for( ; *key; key++ )
        hash = (hash ^ (uint8_t)*key) * 16777619u;
    return &g_ps.stripes[hash % PS_ACCOUNT_STRIPES];
}

/* ------------------------------------------------------------------ */
/* Saves                                                               */
/* ------------------------------------------------------------------ */

/* The server's copy of `key`'s save, into `a` after its header: `*out_len`
 * bytes, 0 for a character that has never been saved. Under the account's
 * stripe. */
static long
save_read(
    char const* key,
    char** out_bytes)
{
    FILE* file = fopen(ToriRSServer_SaveCanonicalPath(key), "rb");
    long len = 0;
    char* bytes;

    *out_bytes = NULL;
    /* No file: a character that has never logged out. */
    if( !file )
        return 0;
    if( fseek(file, 0, SEEK_END) == 0 )
        len = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( len <= 0 )
    {
        fclose(file);
        return 0;
    }
    bytes = ToriMem_Alloc(ToriMem_Net(), (size_t)len);
    assert(bytes);
    if( fread(bytes, 1, (size_t)len, file) != (size_t)len )
    {
        /* A save that cannot be read must not be handed out as "none": the
         * login would start the character afresh. */
        fprintf(stderr, "player_server: cannot read %s\n", ToriRSServer_SaveCanonicalPath(key));
        abort();
    }
    fclose(file);
    *out_bytes = bytes;
    return len;
}

/* `body` (`len` bytes) as the server's copy of `key`'s save, atomically. */
static int
save_write(
    char const* key,
    uint8_t const* body,
    int len)
{
    char* text = ToriMem_Alloc(ToriMem_Net(), (size_t)len + 1);
    int ok;

    assert(text);
    memcpy(text, body, (size_t)len);
    text[len] = '\0';
    ok = ToriRSServer_SaveWriteText(ToriRSServer_SaveCanonicalPath(key), text);
    ToriMem_Free(ToriMem_Net(), text);
    return ok;
}

/* A request whose body is a save: its fields are in the query string. */
static struct Fields
query_fields_of(struct HttpRequest const* req)
{
    struct Fields f;
    char const* query = strchr(req->path, '?');

    f.data = query ? query + 1 : NULL;
    f.len = query ? strlen(query + 1) : 0;
    return f;
}

/* ------------------------------------------------------------------ */
/* Logins and leases                                                   */
/* ------------------------------------------------------------------ */

/* The `login` answer: the check's result, the account's flags, the lease or
 * the world that has the character, whether this world had it already, and
 * the server's copy of the save behind the line. */
static void
answer_login(
    struct HttpResponse* res,
    char const* verb,
    int result,
    int admin,
    int record,
    char const* lease,
    char const* holder,
    int was_holder,
    char const* save,
    long save_len,
    int with_lease)
{
    struct Answer a;

    answer_init(&a);
    if( with_lease )
    {
        answer_printf(&a, "%s\t%d\t%d\t%d\t%s\t%s\t%d\t%ld\n", verb, result, admin, record,
                      lease[0] ? lease : "-", holder[0] ? holder : "-", was_holder, save_len);
        answer_bytes(&a, save, (size_t)save_len);
    }
    else
        answer_printf(&a, "%s\t%d\t%d\t%d\n", verb, result, admin, record);
    answer_send(&a, res, 200);
}

/* `/auth`, `/login` and `/claim`: `check_password` 0 for a claim. */
static void
route_login(
    struct Fields const* f,
    struct HttpResponse* res,
    char const* verb,
    int check_password,
    int want_lease)
{
    char name[128];
    char password[128];
    char world[PLAYER_WORLD_MAX];
    char boot[PLAYER_BOOT_HEX + 1] = "";
    char key[PLAYER_KEY_MAX];
    char lease[PLAYER_LEASE_TOKEN_HEX + 1] = "";
    char holder[PLAYER_WORLD_MAX] = "";
    int was_holder = 0;
    int result = TORIRSSERVER_AUTH_OK;
    int admin = 0;
    int record = 0;
    char* save = NULL;
    long save_len = 0;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) )
    {
        refuse(res, 400, "name is required");
        return;
    }
    if( want_lease && (!field(f, "world", world, sizeof(world)) || !world[0]) )
    {
        refuse(res, 400, "world is required");
        return;
    }
    field(f, "boot", boot, sizeof(boot));
    if( check_password && !field(f, "password", password, sizeof(password)) )
    {
        refuse(res, 400, "password is required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        answer_login(res, verb, TORIRSSERVER_AUTH_UNUSABLE, 0, 0, "", "", 0, NULL, 0, want_lease);
        return;
    }

    stripe = stripe_of(key);
    ps_lock(stripe);
    if( check_password )
    {
        result = (int)ToriRSServer_AuthLogin(name, password);
        memset(password, 0, sizeof(password));
    }
    if( result == TORIRSSERVER_AUTH_OK || result == TORIRSSERVER_AUTH_CLAIMED )
    {
        admin = ToriRSServer_AuthIsAdmin(name);
        record = ToriRSServer_AuthIsRecorded(name);
        if( want_lease )
        {
            int granted;

            ps_lock(&g_ps.leases_lock);
            granted = PlayerLeases_Acquire(&g_ps.leases, key, world, boot, lease, &was_holder,
                                           holder, sizeof(holder));
            ps_unlock(&g_ps.leases_lock);
            if( granted )
                save_len = save_read(key, &save);
            else
                result = TORIRSSERVER_AUTH_ONLINE;
        }
    }
    ps_unlock(stripe);
    if( result == TORIRSSERVER_AUTH_ONLINE )
    {
        fprintf(stderr, "player_server: '%s' refused on %s: %s has it\n", key, world, holder);
        admin = 0;
        record = 0;
    }
    answer_login(res, verb, result, admin, record, lease, holder, was_holder, save, save_len,
                 want_lease);
    ToriMem_Free(ToriMem_Net(), save);
}

/* `/logout?world=&name=&lease=` with the save as the body: the world hands
 * the character back. Taken only from the lease's own world and token --
 * a world whose character was taken from it cannot overwrite the save of
 * whoever has it now -- and the save is on disk before the note goes. */
static void
route_logout(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct Fields f = query_fields_of(req);
    char name[128];
    char world[PLAYER_WORLD_MAX];
    char token[PLAYER_LEASE_TOKEN_HEX + 1];
    char key[PLAYER_KEY_MAX];
    struct PlayerLease const* row;
    char holder[PLAYER_WORLD_MAX] = "";
    int holds;
    int returned = 0;
    ps_mutex* stripe;

    if( !field(&f, "name", name, sizeof(name)) || !field(&f, "world", world, sizeof(world)) ||
        !field(&f, "lease", token, sizeof(token)) )
    {
        refuse(res, 400, "world, name and lease are required");
        return;
    }
    if( !req->body || req->body_len <= 0 || req->body_len > PLAYER_SAVE_MAX )
    {
        refuse(res, 400, "the save is the body");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        refuse(res, 400, "unusable name");
        return;
    }
    stripe = stripe_of(key);
    ps_lock(stripe);
    ps_lock(&g_ps.leases_lock);
    holds = PlayerLeases_Holds(&g_ps.leases, key, world, token);
    row = PlayerLeases_Find(&g_ps.leases, key);
    if( row )
        snprintf(holder, sizeof(holder), "%s", row->world);
    ps_unlock(&g_ps.leases_lock);
    if( holds && save_write(key, req->body, req->body_len) )
    {
        ps_lock(&g_ps.leases_lock);
        returned = PlayerLeases_Release(&g_ps.leases, key, world, token);
        ps_unlock(&g_ps.leases_lock);
    }
    else if( holds )
    {
        /* Not taken: the lease stays the world's, which keeps its copy and
         * asks again. */
        ps_unlock(stripe);
        refuse(res, 500, "the save could not be written");
        return;
    }
    ps_unlock(stripe);
    if( !returned )
        fprintf(stderr, "player_server: %s returned '%s' under a lease it does not hold (%s has "
                        "it); the save was not taken\n",
                world, key, holder[0] ? holder : "nobody");
    answer_result(res, returned, holder);
}

/* A lease given back with no save: the character never got as far as the
 * world, so the server's copy stands. */
static void
route_release(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char world[PLAYER_WORLD_MAX];
    char token[PLAYER_LEASE_TOKEN_HEX + 1];
    char key[PLAYER_KEY_MAX];
    int released;

    if( !field(f, "name", name, sizeof(name)) || !field(f, "world", world, sizeof(world)) ||
        !field(f, "lease", token, sizeof(token)) )
    {
        refuse(res, 400, "world, name and lease are required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    ps_lock(&g_ps.leases_lock);
    released = key[0] ? PlayerLeases_Release(&g_ps.leases, key, world, token) : 0;
    ps_unlock(&g_ps.leases_lock);
    answer_result(res, released, NULL);
}

/* An operator's `--account release`: the character back from whichever
 * world has it. That world's copy of the save is given up -- its return
 * will be refused -- so this is for a world that is not coming back. */
static void
route_revoke(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char key[PLAYER_KEY_MAX];
    char world[PLAYER_WORLD_MAX] = "";
    int revoked;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) )
    {
        refuse(res, 400, "name is required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        answer_result(res, 0, NULL);
        return;
    }
    stripe = stripe_of(key);
    ps_lock(stripe);
    ps_lock(&g_ps.leases_lock);
    revoked = PlayerLeases_Revoke(&g_ps.leases, key, world, sizeof(world));
    ps_unlock(&g_ps.leases_lock);
    ps_unlock(stripe);
    if( revoked )
        fprintf(stderr, "player_server: '%s' taken back from %s by an operator; that world's "
                        "copy of the save will be refused\n",
                key, world);
    answer_result(res, revoked, world);
}

static void
hello_dropped(
    char const* key,
    void* ctx)
{
    char const* world = ctx;

    fprintf(stderr, "player_server: %s holds no save for '%s' any more: the character is free, "
                    "from the save here\n",
            world, key);
}

/* `/hello`: a world has started. Everything it held is dropped except the
 * characters it names in `keep` -- the saves still on its disk, which it
 * returns next. */
static void
route_hello(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char world[PLAYER_WORLD_MAX];
    char boot[PLAYER_BOOT_HEX + 1] = "";
    size_t list_cap = f->len + 1;
    char* list;
    char (*keep)[PLAYER_KEY_MAX] = NULL;
    int keep_count = 0;
    int keep_cap = 0;
    int dropped;

    if( !field(f, "world", world, sizeof(world)) || !world[0] )
    {
        refuse(res, 400, "world is required");
        return;
    }
    field(f, "boot", boot, sizeof(boot));
    list = ToriMem_Alloc(ToriMem_Net(), list_cap);
    assert(list);
    field(f, "keep", list, list_cap);
    for( char* item = list; *item; )
    {
        char* comma = strchr(item, ',');

        if( comma )
            *comma = '\0';
        if( item[0] )
        {
            if( keep_count == keep_cap )
            {
                keep_cap = keep_cap ? keep_cap * 2 : 64;
                keep = ToriMem_Realloc(ToriMem_Net(), keep, (size_t)keep_cap * sizeof(*keep));
                assert(keep);
            }
            snprintf(keep[keep_count++], PLAYER_KEY_MAX, "%s", item);
        }
        if( !comma )
            break;
        item = comma + 1;
    }
    ps_lock(&g_ps.leases_lock);
    dropped = PlayerLeases_DropWorld(&g_ps.leases, world, boot,
                                     (char const (*)[PLAYER_KEY_MAX])keep,
                                     keep_count, hello_dropped, world);
    ps_unlock(&g_ps.leases_lock);
    fprintf(stderr, "player_server: world %s started, returning %d save(s); %d character(s) it "
                    "had no save for are free\n",
            world, keep_count, dropped);
    ToriMem_Free(ToriMem_Net(), keep);
    ToriMem_Free(ToriMem_Net(), list);
    answer_result(res, dropped, NULL);
}

static void
route_leases(struct HttpResponse* res)
{
    struct Answer a;

    answer_init(&a);
    ps_lock(&g_ps.leases_lock);
    for( int i = 0; i < g_ps.leases.count; i++ )
        answer_printf(&a, "lease\t%s\t%s\n", g_ps.leases.rows[i].key, g_ps.leases.rows[i].world);
    ps_unlock(&g_ps.leases_lock);
    answer_send(&a, res, 200);
}

/* `GET /save?name=`: the server's copy, and which world has the character
 * (whose copy is the newer one) -- the account site's player editor. */
static void
route_save_get(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char key[PLAYER_KEY_MAX];
    char world[PLAYER_WORLD_MAX] = "";
    struct PlayerLease const* row;
    char* save = NULL;
    long len;
    struct Answer a;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) )
    {
        refuse(res, 400, "name is required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        refuse(res, 400, "unusable name");
        return;
    }
    stripe = stripe_of(key);
    ps_lock(stripe);
    len = save_read(key, &save);
    ps_lock(&g_ps.leases_lock);
    row = PlayerLeases_Find(&g_ps.leases, key);
    if( row )
        snprintf(world, sizeof(world), "%s", row->world);
    ps_unlock(&g_ps.leases_lock);
    ps_unlock(stripe);
    answer_init(&a);
    answer_printf(&a, "save\t%d\t%s\t%ld\n", save != NULL, world[0] ? world : "-", len);
    answer_bytes(&a, save, (size_t)len);
    answer_send(&a, res, 200);
    ToriMem_Free(ToriMem_Net(), save);
}

/* `POST /save?name=` with the save as the body: an edit of a character no
 * world has. Refused while one does: that world's copy is the newer, and its
 * logout would replace this. */
static void
route_save_put(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct Fields f = query_fields_of(req);
    char name[128];
    char key[PLAYER_KEY_MAX];
    char world[PLAYER_WORLD_MAX] = "";
    struct PlayerLease const* row;
    int written = 0;
    ps_mutex* stripe;

    if( !field(&f, "name", name, sizeof(name)) )
    {
        refuse(res, 400, "name is required");
        return;
    }
    if( !req->body || req->body_len <= 0 || req->body_len > PLAYER_SAVE_MAX )
    {
        refuse(res, 400, "the save is the body");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        refuse(res, 400, "unusable name");
        return;
    }
    stripe = stripe_of(key);
    ps_lock(stripe);
    ps_lock(&g_ps.leases_lock);
    row = PlayerLeases_Find(&g_ps.leases, key);
    if( row )
        snprintf(world, sizeof(world), "%s", row->world);
    ps_unlock(&g_ps.leases_lock);
    if( !world[0] )
        written = save_write(key, req->body, req->body_len);
    ps_unlock(stripe);
    answer_result(res, written, world);
}

/* ------------------------------------------------------------------ */
/* Display names                                                       */
/* ------------------------------------------------------------------ */

static void
route_name_owner(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char key[PLAYER_KEY_MAX];
    char owner[PLAYER_KEY_MAX] = "";
    int held;
    struct Answer a;

    if( !field(f, "name", name, sizeof(name)) )
    {
        refuse(res, 400, "name is required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        refuse(res, 400, "unusable name");
        return;
    }
    ps_lock(&g_ps.names_lock);
    held = ToriRSServer_DisplayNameOwnerFile(name, owner, sizeof(owner));
    ps_unlock(&g_ps.names_lock);
    answer_init(&a);
    answer_printf(&a, "name\t%d\t%s\n", held, held && owner[0] ? owner : "-");
    answer_send(&a, res, 200);
}

/* `/name/claim` and `/name/release`. */
static void
route_name_change(
    struct Fields const* f,
    struct HttpResponse* res,
    int claim)
{
    char name[128];
    char account[128];
    char key[PLAYER_KEY_MAX];
    char account_key[PLAYER_KEY_MAX];
    int result = 1;

    if( !field(f, "name", name, sizeof(name)) || !field(f, "account", account, sizeof(account)) )
    {
        refuse(res, 400, "name and account are required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    ToriRSServer_SaveFileName(account, account_key, sizeof(account_key));
    if( !key[0] || !account_key[0] )
    {
        refuse(res, 400, "unusable name");
        return;
    }
    ps_lock(&g_ps.names_lock);
    if( claim )
        result = ToriRSServer_DisplayNameClaimFile(name, account);
    else
        ToriRSServer_DisplayNameReleaseFile(name, account);
    ps_unlock(&g_ps.names_lock);
    answer_result(res, result, NULL);
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

static void
account_line(
    struct Answer* a,
    char const* key)
{
    char owner[PLAYER_KEY_MAX] = "";
    char world[PLAYER_WORLD_MAX] = "";
    int exists;
    int linked;
    struct PlayerLease const* lease;
    ps_mutex* stripe = stripe_of(key);
    FILE* probe;

    ps_lock(stripe);
    probe = fopen(ToriRSServer_AccountPath(key), "rb");
    exists = probe != NULL;
    if( probe )
        fclose(probe);
    linked = ToriRSServer_AuthManagedBy(key, owner, sizeof(owner));
    if( linked != 1 )
        owner[0] = '\0';
    answer_printf(a, "account\t%s\t%d\t%d\t%d\t%d\t%s", key, exists, ToriRSServer_AuthIsAdmin(key),
                  ToriRSServer_AuthIsRecorded(key), linked, owner[0] ? owner : "-");
    ps_unlock(stripe);
    ps_lock(&g_ps.leases_lock);
    lease = PlayerLeases_Find(&g_ps.leases, key);
    if( lease )
        snprintf(world, sizeof(world), "%s", lease->world);
    ps_unlock(&g_ps.leases_lock);
    answer_printf(a, "\t%s\n", world[0] ? world : "-");
}

static void
route_account(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char key[PLAYER_KEY_MAX];
    struct Answer a;

    if( !field(f, "name", name, sizeof(name)) )
    {
        refuse(res, 400, "name is required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        refuse(res, 400, "unusable name");
        return;
    }
    answer_init(&a);
    account_line(&a, key);
    answer_send(&a, res, 200);
}

static void
route_accounts(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char filter[PLAYER_KEY_MAX] = "";
    char dir_path[1024];
    DIR* dir;
    struct dirent* entry;
    struct Answer a;

    field(f, "q", filter, sizeof(filter));
    answer_init(&a);
    snprintf(dir_path, sizeof(dir_path), "%s/accounts", ToriRSServer_SaveDir());
    dir = opendir(dir_path);
    /* No directory: no player account has been made yet. */
    if( dir )
    {
        while( (entry = readdir(dir)) != NULL )
        {
            char key[PLAYER_KEY_MAX];
            size_t len = strlen(entry->d_name);

            if( len <= 4 || len - 4 >= sizeof(key) ||
                strcmp(entry->d_name + len - 4, ".ini") != 0 )
                continue;
            snprintf(key, sizeof(key), "%.*s", (int)(len - 4), entry->d_name);
            if( filter[0] && !strstr(key, filter) )
                continue;
            account_line(&a, key);
        }
        closedir(dir);
    }
    answer_send(&a, res, 200);
}

static void
route_staff(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char login[PLAYER_KEY_MAX];

    if( !field(f, "login", login, sizeof(login)) )
    {
        refuse(res, 400, "login is required");
        return;
    }
    answer_result(res, ToriRSServer_AuthLoginIsStaff(login), NULL);
}

static void
route_create(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char password[128];
    char admin[8] = "";
    char key[PLAYER_KEY_MAX];
    char const* problem = NULL;
    enum ToriRSServerCreateResult result;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) || !field(f, "password", password, sizeof(password)) )
    {
        refuse(res, 400, "name and password are required");
        return;
    }
    field(f, "admin", admin, sizeof(admin));
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    stripe = stripe_of(key);
    ps_lock(stripe);
    result = ToriRSServer_AuthCreate(name, password, atoi(admin) != 0, &problem);
    ps_unlock(stripe);
    memset(password, 0, sizeof(password));
    answer_result(res, (int)result, problem);
}

static void
route_password(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char password[128];
    char key[PLAYER_KEY_MAX];
    char const* problem = NULL;
    int written;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) || !field(f, "password", password, sizeof(password)) )
    {
        refuse(res, 400, "name and password are required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    stripe = stripe_of(key);
    ps_lock(stripe);
    written = ToriRSServer_AuthSetPassword(name, password, &problem);
    ps_unlock(stripe);
    memset(password, 0, sizeof(password));
    answer_result(res, written, problem);
}

/* `/account/admin` and `/account/record`. */
static void
route_flag(
    struct Fields const* f,
    struct HttpResponse* res,
    int (*set)(char const* display_name, int on))
{
    char name[128];
    char on[8];
    char key[PLAYER_KEY_MAX];
    int written;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) || !field(f, "on", on, sizeof(on)) )
    {
        refuse(res, 400, "name and on are required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    stripe = stripe_of(key);
    ps_lock(stripe);
    written = set(name, atoi(on) != 0);
    ps_unlock(stripe);
    answer_result(res, written, NULL);
}

static void
answer_link(
    struct HttpResponse* res,
    enum ToriRSServerLinkResult result,
    char const* token)
{
    struct Answer a;

    answer_init(&a);
    answer_printf(&a, "link\t%d\t%s\n", (int)result,
                  result == TORIRSSERVER_LINK_OK && token[0] ? token : "-");
    answer_send(&a, res, 200);
}

/* `/account/link` (with the account's password) and `/account/assign`
 * (staff's, without). */
static void
route_link(
    struct Fields const* f,
    struct HttpResponse* res,
    int assign)
{
    char name[128];
    char password[128] = "";
    char user[PLAYER_KEY_MAX];
    char key[PLAYER_KEY_MAX];
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1] = "";
    enum ToriRSServerLinkResult result;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) || !field(f, "user", user, sizeof(user)) ||
        (!assign && !field(f, "password", password, sizeof(password))) )
    {
        refuse(res, 400, assign ? "name and user are required"
                                 : "name, password and user are required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    stripe = stripe_of(key);
    ps_lock(stripe);
    result = assign ? ToriRSServer_AuthAssign(name, user, token)
                    : ToriRSServer_AuthLink(name, password, user, token);
    ps_unlock(stripe);
    memset(password, 0, sizeof(password));
    answer_link(res, result, token);
    memset(token, 0, sizeof(token));
}

static void
route_verify(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char user[PLAYER_KEY_MAX];
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    char key[PLAYER_KEY_MAX];
    int verified;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) || !field(f, "user", user, sizeof(user)) ||
        !field(f, "token", token, sizeof(token)) )
    {
        refuse(res, 400, "name, user and token are required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    stripe = stripe_of(key);
    ps_lock(stripe);
    verified = ToriRSServer_AuthLinkVerify(name, user, token);
    ps_unlock(stripe);
    memset(token, 0, sizeof(token));
    answer_result(res, verified, NULL);
}

static void
route_unlink(
    struct Fields const* f,
    struct HttpResponse* res)
{
    char name[128];
    char key[PLAYER_KEY_MAX];
    int unlinked;
    ps_mutex* stripe;

    if( !field(f, "name", name, sizeof(name)) )
    {
        refuse(res, 400, "name is required");
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    stripe = stripe_of(key);
    ps_lock(stripe);
    unlinked = ToriRSServer_AuthUnlink(name);
    ps_unlock(stripe);
    answer_result(res, unlinked, NULL);
}

/* ------------------------------------------------------------------ */
/* The handler                                                         */
/* ------------------------------------------------------------------ */

/* The saves directory's lock, held for the process's life (the system drops
 * it when the process ends, however it ends). 0 when another process has it. */
static int
lockfile_take(char const* path)
{
#ifdef _WIN32
    g_ps.lockfile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    return g_ps.lockfile != INVALID_HANDLE_VALUE;
#else
    g_ps.lockfile = open(path, O_RDWR | O_CREAT, 0644);
    if( g_ps.lockfile < 0 )
    {
        fprintf(stderr, "player_server: cannot open %s\n", path);
        abort();
    }
    if( flock(g_ps.lockfile, LOCK_EX | LOCK_NB) == 0 )
        return 1;
    close(g_ps.lockfile);
    return 0;
#endif
}

static void
lockfile_drop(void)
{
#ifdef _WIN32
    CloseHandle(g_ps.lockfile);
#else
    close(g_ps.lockfile);
#endif
}

int
PlayerServer_Open(void)
{
    char path[1024];

    assert(!g_ps.open);
    snprintf(path, sizeof(path), "%s/player_server.lock", ToriRSServer_SaveDir());
    ToriRSServer_SaveMkdirParent(path);
    if( !lockfile_take(path) )
    {
        fprintf(stderr, "player_server: another player account server has %s (%s)\n",
                ToriRSServer_SaveDir(), path);
        return 0;
    }
    ps_mutex_init(&g_ps.leases_lock);
    ps_mutex_init(&g_ps.names_lock);
    for( int i = 0; i < PS_ACCOUNT_STRIPES; i++ )
        ps_mutex_init(&g_ps.stripes[i]);
    snprintf(path, sizeof(path), "%s/player_leases.journal", ToriRSServer_SaveDir());
    ToriRSServer_SaveMkdirParent(path);
    PlayerLeases_Open(&g_ps.leases, path);
    g_ps.open = 1;
    if( g_ps.leases.count )
        fprintf(stderr, "player_server: %d character(s) out on their worlds, held until each "
                        "world returns its save\n",
                g_ps.leases.count);
    return 1;
}

void
PlayerServer_Close(void)
{
    if( !g_ps.open )
        return;
    PlayerLeases_Close(&g_ps.leases);
    for( int i = 0; i < PS_ACCOUNT_STRIPES; i++ )
        ps_mutex_destroy(&g_ps.stripes[i]);
    ps_mutex_destroy(&g_ps.leases_lock);
    ps_mutex_destroy(&g_ps.names_lock);
    lockfile_drop();
    g_ps.open = 0;
}

void
PlayerServer_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct Fields f;

    (void)user;
    assert(req);
    assert(res);
    assert(g_ps.open);
    f = fields_of(req);

    if( route_is(req, "POST", "/auth") )
        route_login(&f, res, "auth", 1, 0);
    else if( route_is(req, "POST", "/login") )
        route_login(&f, res, "login", 1, 1);
    else if( route_is(req, "POST", "/claim") )
        route_login(&f, res, "login", 0, 1);
    else if( route_is(req, "POST", "/logout") )
        route_logout(req, res);
    else if( route_is(req, "POST", "/release") )
        route_release(&f, res);
    else if( route_is(req, "POST", "/hello") )
        route_hello(&f, res);
    else if( route_is(req, "GET", "/leases") )
        route_leases(res);
    else if( route_is(req, "GET", "/save") )
        route_save_get(&f, res);
    else if( route_is(req, "POST", "/save") )
        route_save_put(req, res);
    else if( route_is(req, "GET", "/name") )
        route_name_owner(&f, res);
    else if( route_is(req, "POST", "/name/claim") )
        route_name_change(&f, res, 1);
    else if( route_is(req, "POST", "/name/release") )
        route_name_change(&f, res, 0);
    else if( route_is(req, "POST", "/account/release") )
        route_revoke(&f, res);
    else if( route_is(req, "GET", "/account") )
        route_account(&f, res);
    else if( route_is(req, "GET", "/accounts") )
        route_accounts(&f, res);
    else if( route_is(req, "GET", "/staff") )
        route_staff(&f, res);
    else if( route_is(req, "POST", "/account/create") )
        route_create(&f, res);
    else if( route_is(req, "POST", "/account/password") )
        route_password(&f, res);
    else if( route_is(req, "POST", "/account/admin") )
        route_flag(&f, res, ToriRSServer_AuthSetAdmin);
    else if( route_is(req, "POST", "/account/record") )
        route_flag(&f, res, ToriRSServer_AuthSetRecorded);
    else if( route_is(req, "POST", "/account/link") )
        route_link(&f, res, 0);
    else if( route_is(req, "POST", "/account/assign") )
        route_link(&f, res, 1);
    else if( route_is(req, "POST", "/account/verify") )
        route_verify(&f, res);
    else if( route_is(req, "POST", "/account/unlink") )
        route_unlink(&f, res);
    else
        refuse(res, 404, "no such route");
}

/* ------------------------------------------------------------------ */
/* In a world's process                                                */
/* ------------------------------------------------------------------ */

static struct
{
    int started;
    int port;
    volatile int finished;
    volatile int status;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
} g_inproc;

#ifdef _WIN32
static DWORD WINAPI
inproc_main(LPVOID arg)
#else
static void*
inproc_main(void* arg)
#endif
{
    (void)arg;
    g_inproc.status = HttpServer_RunOn(1, g_inproc.port, PlayerServer_Handle, NULL);
    g_inproc.finished = 1;
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int
PlayerServer_StartInProcess(int port)
{
    assert(g_ps.open);
    assert(!g_inproc.started);
    assert(port > 0);
    g_inproc.port = port;
    g_inproc.finished = 0;
#ifdef _WIN32
    g_inproc.thread = CreateThread(NULL, 0, inproc_main, NULL, 0, NULL);
    assert(g_inproc.thread);
#else
    {
        int const created = pthread_create(&g_inproc.thread, NULL, inproc_main, NULL);

        assert(created == 0);
        (void)created;
    }
#endif
    g_inproc.started = 1;
    /* Up, or out with its reason: a bind on a port another player server has
     * fails at once, so two seconds is generous. */
    for( int waited = 0; waited < 2000 && !HttpServer_Running() && !g_inproc.finished; waited += 10 )
    {
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10 * 1000);
#endif
    }
    if( HttpServer_Running() )
        return 1;
    PlayerServer_StopInProcess();
    return 0;
}

void
PlayerServer_StopInProcess(void)
{
    if( !g_inproc.started )
        return;
    HttpServer_RequestStop();
#ifdef _WIN32
    WaitForSingleObject(g_inproc.thread, INFINITE);
    CloseHandle(g_inproc.thread);
#else
    pthread_join(g_inproc.thread, NULL);
#endif
    g_inproc.started = 0;
}
