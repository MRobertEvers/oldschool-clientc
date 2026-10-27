/*
 * The account flow's IO. See task_accounts_io.h.
 */
#include "game/task_accounts_io.h"

#include "mem/torirs_mem.h"
#include "net/flat_json.h"

#include "asyncio.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One slot each: every task here has one request in flight at a time. */
#define ACCOUNTS_IO_SLOT 0

/* The one public client the account server knows (TORIRSSERVER_OAUTH_CLIENT_ID)
 * and the deob's scope for "may mint a game login". */
#define ACCOUNTS_CLIENT_ID "torirs-client"
#define ACCOUNTS_SCOPE "gamesso.token.create"

/* ------------------------------------------------------------------ */
/* Load / save                                                         */
/* ------------------------------------------------------------------ */

struct Task_AccountsLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_Accounts* accounts;
    int* loaded;
    char path[TORIRS_IOITEM_MAX_PATH];
};

static int
Task_AccountsLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AccountsLoad* task = (struct Task_AccountsLoad*)task_base;
    struct ToriRS_IOItem* item;

    PT_BEGIN(&task->pt);

    RS_Accounts_Init(task->accounts);
    ToriRS_IO_QueueFileRead(io, ACCOUNTS_IO_SLOT, task->path);
    PT_YIELD(&task->pt);

    item = ToriRS_IO_TaskSlot(io, ACCOUNTS_IO_SLOT);
    /* No file is a device that has never signed in, not an error. */
    if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
        RS_Accounts_Decode(task->accounts, IOITEM_DATA(item), IOITEM_DATA_SIZE(item));
    ToriRS_IO_ClearItem(item);
    *task->loaded = 1;

    PT_END(&task->pt);
}

static void
Task_AccountsLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Process(), task_base);
}

static struct ToriRS_TaskVTable Task_AccountsLoad_VTable = {
    .run = Task_AccountsLoad_Run,
    .free = Task_AccountsLoad_Free,
};

struct ToriRS_Task*
CreateTask_AccountsLoad(
    struct RS_Accounts* accounts,
    char const* path,
    int* loaded)
{
    struct Task_AccountsLoad* task;

    assert(accounts);
    assert(path && *path);
    assert(loaded);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AccountsLoad_VTable;
    strcpy(task->task.name, "AccountsLoad");
    task->accounts = accounts;
    task->loaded = loaded;
    snprintf(task->path, sizeof(task->path), "%s", path);
    PT_INIT(&task->pt);
    return &task->task;
}

struct Task_AccountsSave
{
    struct ToriRS_Task task;
    struct pt pt;
    char path[TORIRS_IOITEM_MAX_PATH];
    struct ToriRS_IOItem* item;
    /** The encoded file, owned here: the write only borrows it. */
    void* data;
    int size;
};

static int
Task_AccountsSave_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AccountsSave* task = (struct Task_AccountsSave*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileWrite(io, ACCOUNTS_IO_SLOT, task->path, task->data, task->size);
    PT_YIELD(&task->pt);

    task->item = ToriRS_IO_TaskSlot(io, ACCOUNTS_IO_SLOT);
    if( IOITEM_ERROR_CODE(task->item) != 0 )
        TORIRS_LOG("accounts: could not write %s\n", task->path);
    ToriRS_IO_ClearItem(task->item);

    PT_END(&task->pt);
}

static void
Task_AccountsSave_Free(struct ToriRS_Task* task_base)
{
    struct Task_AccountsSave* task = (struct Task_AccountsSave*)task_base;

    ToriMem_Free(ToriMem_Process(), task->data);
    ToriMem_Free(ToriMem_Process(), task);
}

static struct ToriRS_TaskVTable Task_AccountsSave_VTable = {
    .run = Task_AccountsSave_Run,
    .free = Task_AccountsSave_Free,
};

struct ToriRS_Task*
CreateTask_AccountsSave(
    struct RS_Accounts const* accounts,
    char const* path)
{
    struct Task_AccountsSave* task;

    assert(accounts);
    assert(path && *path);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AccountsSave_VTable;
    strcpy(task->task.name, "AccountsSave");
    snprintf(task->path, sizeof(task->path), "%s", path);
    /* Encoded now, while the caller's list is the one it asked to save. */
    RS_Accounts_Encode(accounts, &task->data, &task->size);
    PT_INIT(&task->pt);
    return &task->task;
}

/* ------------------------------------------------------------------ */
/* Sign-in                                                             */
/* ------------------------------------------------------------------ */

#define SIGNIN_BODY_MAX 2600

struct Task_AccountSignIn
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_AccountSignIn* signin;
    /** The request body, LENT to the item across each yield. */
    char body[SIGNIN_BODY_MAX];
    /** The token endpoint's answer, kept for the play request. */
    char access_token[RS_ACCOUNTS_TOKEN_LEN];
};

/* `text` percent-encoded onto `out` (form-urlencoded value). */
static void
form_append(
    char* out,
    size_t cap,
    char const* key,
    char const* text)
{
    static char const digits[] = "0123456789ABCDEF";
    size_t used = strlen(out);

    used += (size_t)snprintf(out + used, cap - used, "%s%s=", used ? "&" : "", key);
    for( ; *text && used + 4 < cap; text++ )
    {
        unsigned char ch = (unsigned char)*text;

        if( isalnum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '~' )
            out[used++] = (char)ch;
        else
        {
            out[used++] = '%';
            out[used++] = digits[ch >> 4];
            out[used++] = digits[ch & 15];
        }
    }
    out[used] = '\0';
    assert(used + 4 < cap);
}

/* Queue `auth/<endpoint>` lending `body` (a task's own array). */
static void
queue_request(
    char* body,
    struct ToriRS_IOBatch* io,
    char const* endpoint)
{
    char path[64];
    struct ToriRS_IOItem* item;

    snprintf(path, sizeof(path), "auth/%s", endpoint);
    ToriRS_IO_QueueScript(io, ACCOUNTS_IO_SLOT, path);
    item = ToriRS_IO_TaskSlot(io, ACCOUNTS_IO_SLOT);
    item->data = body;
    item->data_size = (int)strlen(body);
}

/*
 * Take the answer out of the slot as a NUL-terminated string into `out`.
 * 0 when there was none: the executor never answered in place (the body is
 * still lent) or the transport failed.
 */
static int
take_answer(
    char* body,
    size_t body_cap,
    struct ToriRS_IOBatch* io,
    char* out,
    size_t cap)
{
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, ACCOUNTS_IO_SLOT);
    int ok = 0;

    if( IOITEM_DATA(item) == body )
    {
        IOITEM_DATA(item) = NULL;
        IOITEM_DATA_SIZE(item) = 0;
    }
    else if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) &&
             (size_t)IOITEM_DATA_SIZE(item) < cap )
    {
        memcpy(out, IOITEM_DATA(item), (size_t)IOITEM_DATA_SIZE(item));
        out[IOITEM_DATA_SIZE(item)] = '\0';
        ok = 1;
    }
    ToriRS_IO_ClearItem(item);
    memset(body, 0, body_cap);
    return ok;
}

/* An OAuth error body's words, for the player. */
static void
describe_error(
    char const* json,
    char* out,
    size_t cap)
{
    char error[48] = "";

    if( FlatJson_String(json, strlen(json), "error_description", out, cap) && out[0] )
        return;
    FlatJson_String(json, strlen(json), "error", error, sizeof(error));
    if( strcmp(error, "invalid_grant") == 0 )
        snprintf(out, cap, "Invalid username or password.");
    else
        snprintf(out, cap, "The login server refused the request (%s).",
                 error[0] ? error : "no reason given");
}

static int
Task_AccountSignIn_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AccountSignIn* task = (struct Task_AccountSignIn*)task_base;
    struct RS_AccountSignIn* signin = task->signin;
    /* Read out of the slot before the next yield, never carried across one. */
    static char answer[3 * RS_ACCOUNTS_TOKEN_LEN];

    PT_BEGIN(&task->pt);

    /* A link, or a linked account's Play Now: one request on the site's
     * session, answered with the one-time login itself -- no token grant, so
     * nothing for the device to keep. */
    if( signin->grant == RS_ACCOUNT_SIGNIN_LINK || signin->grant == RS_ACCOUNT_SIGNIN_SITE )
    {
        task->body[0] = '\0';
        if( signin->grant == RS_ACCOUNT_SIGNIN_LINK )
        {
            form_append(task->body, sizeof(task->body), "username", signin->username);
            form_append(task->body, sizeof(task->body), "password", signin->password);
        }
        else
            form_append(task->body, sizeof(task->body), "player", signin->username);
        memset(signin->password, 0, sizeof(signin->password));
        queue_request(task->body, io,
                      signin->grant == RS_ACCOUNT_SIGNIN_LINK ? "link" : "userplay");
        PT_YIELD(&task->pt);

        if( !take_answer(task->body, sizeof(task->body), io, answer, sizeof(answer)) )
        {
            snprintf(signin->error, sizeof(signin->error), "Unable to reach the login server.");
            signin->done = 1;
            PT_EXIT(&task->pt);
        }
        if( !FlatJson_String(answer, strlen(answer), "token", signin->otl, sizeof(signin->otl)) ||
            !FlatJson_String(answer, strlen(answer), "name", signin->name, sizeof(signin->name)) )
        {
            describe_error(answer, signin->error, sizeof(signin->error));
            signin->done = 1;
            PT_EXIT(&task->pt);
        }
        signin->ok = 1;
        signin->done = 1;
        PT_EXIT(&task->pt);
    }

    /* 1. The token endpoint (the deob's shield/oauth/token). */
    task->body[0] = '\0';
    form_append(task->body, sizeof(task->body), "client_id", ACCOUNTS_CLIENT_ID);
    form_append(task->body, sizeof(task->body), "scope", ACCOUNTS_SCOPE);
    if( signin->grant == RS_ACCOUNT_SIGNIN_REFRESH )
    {
        form_append(task->body, sizeof(task->body), "grant_type", "refresh_token");
        form_append(task->body, sizeof(task->body), "refresh_token", signin->refresh_token);
    }
    else
    {
        form_append(task->body, sizeof(task->body), "grant_type", "password");
        form_append(task->body, sizeof(task->body), "username", signin->username);
        form_append(task->body, sizeof(task->body), "password", signin->password);
    }
    memset(signin->password, 0, sizeof(signin->password));
    queue_request(task->body, io, "token");
    PT_YIELD(&task->pt);

    if( !take_answer(task->body, sizeof(task->body), io, answer, sizeof(answer)) )
    {
        snprintf(signin->error, sizeof(signin->error), "Unable to reach the login server.");
        signin->done = 1;
        PT_EXIT(&task->pt);
    }
    if( !FlatJson_String(answer, strlen(answer), "access_token", task->access_token,
                         sizeof(task->access_token)) ||
        !FlatJson_String(answer, strlen(answer), "refresh_token", signin->new_refresh_token,
                         sizeof(signin->new_refresh_token)) ||
        !FlatJson_String(answer, strlen(answer), "name", signin->name, sizeof(signin->name)) )
    {
        char error[48] = "";

        describe_error(answer, signin->error, sizeof(signin->error));
        FlatJson_String(answer, strlen(answer), "error", error, sizeof(error));
        signin->refresh_refused =
            signin->grant == RS_ACCOUNT_SIGNIN_REFRESH && strcmp(error, "invalid_grant") == 0;
        signin->done = 1;
        PT_EXIT(&task->pt);
    }

    /* 2. The play call (the deob's public/v1/games/<id>/play): the access
     *    token, as a form parameter (RFC 6750 2.2), for a one-time login. */
    task->body[0] = '\0';
    form_append(task->body, sizeof(task->body), "access_token", task->access_token);
    memset(task->access_token, 0, sizeof(task->access_token));
    queue_request(task->body, io, "play");
    PT_YIELD(&task->pt);

    if( !take_answer(task->body, sizeof(task->body), io, answer, sizeof(answer)) )
    {
        snprintf(signin->error, sizeof(signin->error), "Unable to reach the login server.");
        signin->done = 1;
        PT_EXIT(&task->pt);
    }
    if( !FlatJson_String(answer, strlen(answer), "token", signin->otl, sizeof(signin->otl)) )
    {
        describe_error(answer, signin->error, sizeof(signin->error));
        signin->done = 1;
        PT_EXIT(&task->pt);
    }
    signin->ok = 1;
    signin->done = 1;

    PT_END(&task->pt);
}

/* The body is the task's own array, so nothing lent outlives it. */
static void
Task_AccountSignIn_Free(struct ToriRS_Task* task_base)
{
    struct Task_AccountSignIn* task = (struct Task_AccountSignIn*)task_base;

    memset(task->body, 0, sizeof(task->body));
    memset(task->access_token, 0, sizeof(task->access_token));
    ToriMem_Free(ToriMem_Process(), task);
}

static struct ToriRS_TaskVTable Task_AccountSignIn_VTable = {
    .run = Task_AccountSignIn_Run,
    .free = Task_AccountSignIn_Free,
};

struct ToriRS_Task*
CreateTask_AccountSignIn(struct RS_AccountSignIn* signin)
{
    struct Task_AccountSignIn* task;

    assert(signin);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AccountSignIn_VTable;
    strcpy(task->task.name, "AccountSignIn");
    task->signin = signin;
    signin->done = 0;
    signin->ok = 0;
    signin->refresh_refused = 0;
    signin->error[0] = '\0';
    PT_INIT(&task->pt);
    return &task->task;
}

/* ------------------------------------------------------------------ */
/* The site session                                                    */
/* ------------------------------------------------------------------ */

struct Task_SiteSessionLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_SiteSession* site;
    int* done;
    /** The request body, LENT to the item across the yield. */
    char body[64];
};

static int
Task_SiteSessionLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_SiteSessionLoad* task = (struct Task_SiteSessionLoad*)task_base;
    /* Read out of the slot before the next yield, never carried across one. */
    static char answer[4096];

    PT_BEGIN(&task->pt);

    memset(task->site, 0, sizeof(*task->site));
    task->body[0] = '\0';
    form_append(task->body, sizeof(task->body), "client_id", ACCOUNTS_CLIENT_ID);
    queue_request(task->body, io, "user");
    PT_YIELD(&task->pt);

    /* No answer is a world with no gate, or a desktop with no account server
     * named: the form signs in with passwords, as without one. */
    if( take_answer(task->body, sizeof(task->body), io, answer, sizeof(answer)) )
        RS_SiteSession_Decode(task->site, answer, (int)strlen(answer));
    if( task->site->signed_in )
        TORIRS_LOG("accounts: site session '%s', %d linked account(s)\n", task->site->user,
                   task->site->count);
    *task->done = 1;

    PT_END(&task->pt);
}

static void
Task_SiteSessionLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Process(), task_base);
}

static struct ToriRS_TaskVTable Task_SiteSessionLoad_VTable = {
    .run = Task_SiteSessionLoad_Run,
    .free = Task_SiteSessionLoad_Free,
};

struct ToriRS_Task*
CreateTask_SiteSessionLoad(
    struct RS_SiteSession* site,
    int* done)
{
    struct Task_SiteSessionLoad* task;

    assert(site);
    assert(done);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_SiteSessionLoad_VTable;
    strcpy(task->task.name, "SiteSessionLoad");
    task->site = site;
    task->done = done;
    PT_INIT(&task->pt);
    return &task->task;
}
