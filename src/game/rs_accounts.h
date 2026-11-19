#ifndef SRC_GAME_RS_ACCOUNTS_H
#define SRC_GAME_RS_ACCOUNTS_H

/*
 * The accounts this device has signed in with: each a display name and the
 * OAuth refresh token that signs it in again (torirs_server_oauth.h), which is
 * what puts a "Play Now" on the title screen instead of a password box. The
 * deob's launcher keeps exactly this pair per account (JX_DISPLAY_NAME and
 * JX_REFRESH_TOKEN, client.java method1632 / the properties it writes back
 * after a refresh).
 *
 * A leaf, like rs_prefs.c: the text form and the list operations, nothing
 * about how the file moves. The file is the client's own and travels the IO
 * queue as FILE_READ / FILE_WRITE (task_accounts_io.c), so on the browser lane
 * it lives in the page's IndexedDB and never reaches a server.
 *
 *   [account]
 *   name = Matt
 *   refresh = eyJhbGciOi...
 *
 * one section per account, most recently played first.
 *
 * Beside the list: the read-once, shown-once latch the title holds for this
 * file and for the site session, and the record of the one sign-in in flight,
 * with the rule that a second cannot begin before the first has answered.
 * None of it knows when a frame runs or what the title shows; the caller asks
 * at its own moments and does the loading, the rebuilding and the dialling.
 */

#define RS_ACCOUNTS_MAX 8
#define RS_ACCOUNTS_NAME_LEN 64
#define RS_ACCOUNTS_TOKEN_LEN 1024

struct RS_Account
{
    char name[RS_ACCOUNTS_NAME_LEN];
    char refresh_token[RS_ACCOUNTS_TOKEN_LEN];
};

struct RS_Accounts
{
    struct RS_Account list[RS_ACCOUNTS_MAX];
    int count;
};

/** TORIRS_ACCOUNTS, else "accounts.ini"; NULL when TORIRS_ACCOUNTS is set
 *  empty (a harness that must not keep a session). */
char const*
RS_Accounts_Path(void);

void
RS_Accounts_Init(struct RS_Accounts* accounts);

/** Replace `accounts` with the file's. A malformed or partial section is
 *  skipped, not fatal: the worst case is a Play Now that is not there. */
void
RS_Accounts_Decode(
    struct RS_Accounts* accounts,
    void const* data,
    int size);

/** The file's text, allocated from ToriMem_Process(); the caller frees it. */
void
RS_Accounts_Encode(
    struct RS_Accounts const* accounts,
    void** out_data,
    int* out_size);

/** The index of `name`, compared as the server keys accounts (case and
 *  space-versus-underscore folded), or -1. */
int
RS_Accounts_Find(
    struct RS_Accounts const* accounts,
    char const* name);

/**
 * Put `name` first with `refresh_token`: a sign-in, or a refresh that rotated
 * the token. An account already listed moves up rather than appearing twice;
 * past RS_ACCOUNTS_MAX the least recently played falls off.
 */
void
RS_Accounts_Remember(
    struct RS_Accounts* accounts,
    char const* name,
    char const* refresh_token);

/** Drop the account at `index`: its token was refused, or it was revoked. */
void
RS_Accounts_Forget(
    struct RS_Accounts* accounts,
    int index);

/*
 * The SITE session: the web login the gate's cookie signed this page in as
 * (the "user account"), and the player accounts linked under it
 * (torirs_server_auth.h "Linked player accounts"). Not kept on the device:
 * the gate's /account/players answers it each boot, so a link made elsewhere
 * shows up and an unlinked account goes away. A linked account carries no
 * token here -- the gate's session is what plays it.
 *
 *   {"user": "matt", "players": "Matt\nMatt Alt"}
 */
struct RS_SiteSession
{
    /** 1: the account server named a web login. 0: no gate in front of this
     *  world (or none reachable), and the form signs in as it always has. */
    int signed_in;
    char user[RS_ACCOUNTS_NAME_LEN];
    char players[RS_ACCOUNTS_MAX][RS_ACCOUNTS_NAME_LEN];
    int count;
};

/** Replace `site` with the /account/players answer `json`; anything that is
 *  not one (an error body, a refusal) is "not signed in". */
void
RS_SiteSession_Decode(
    struct RS_SiteSession* site,
    char const* json,
    int size);

/** The index of the linked player `name` (folded as RS_Accounts_Find), or -1. */
int
RS_SiteSession_Find(
    struct RS_SiteSession const* site,
    char const* name);

/** Add a player just linked, last, unless it is there or the list is full. */
void
RS_SiteSession_Add(
    struct RS_SiteSession* site,
    char const* name);

/*
 * Read once, shown once: the device's accounts file and the site session are
 * each asked for a single time per boot, and the title is rebuilt for the
 * answer a single time when it lands. A load task sets `done` through
 * RS_AccountsLoad_DoneFlag; everything else is these functions.
 */
struct RS_AccountsLoad
{
    int started;
    /** Set by the load task when it has an answer (or found none). */
    int done;
    /** The title has been rebuilt for the answer. */
    int shown;
};

/** 1 the first time it is asked, and the load counts as started from then. */
int
RS_AccountsLoad_TakeStart(struct RS_AccountsLoad* load);

/** Where the load task records that it has answered. */
int*
RS_AccountsLoad_DoneFlag(struct RS_AccountsLoad* load);

int
RS_AccountsLoad_Done(struct RS_AccountsLoad const* load);

/** 1 the first time it is asked after the load answered, and never again. */
int
RS_AccountsLoad_TakeShow(struct RS_AccountsLoad* load);

enum RS_AccountSignInGrant
{
    /** The credential form: username and password. */
    RS_ACCOUNT_SIGNIN_PASSWORD = 0,
    /** A Play Now: the account's saved refresh token. */
    RS_ACCOUNT_SIGNIN_REFRESH,
    /** The credential form, "Link account": the player joins the site's web
     *  login and plays. No refresh token: a linked account has none. */
    RS_ACCOUNT_SIGNIN_LINK,
    /** A linked account's Play Now (`username`), on the site's session. */
    RS_ACCOUNT_SIGNIN_SITE,
};

/** One sign-in, owned by the app. The task (task_accounts_io.h) fills the
 *  second half and sets `done` last; `busy` is the app's and the task never
 *  touches it. One is in flight from RS_AccountSignIn_Begin until
 *  RS_AccountSignIn_TakeAnswer says it answered. */
struct RS_AccountSignIn
{
    enum RS_AccountSignInGrant grant;
    char username[RS_ACCOUNTS_NAME_LEN];
    char password[64];
    char refresh_token[RS_ACCOUNTS_TOKEN_LEN];

    int done;
    /** 1: `name` and `otl` are good, dial the game; `new_refresh_token` too
     *  for the PASSWORD and REFRESH grants. */
    int ok;
    /** The refresh token was refused (invalid_grant): forget the account. */
    int refresh_refused;
    char name[RS_ACCOUNTS_NAME_LEN];
    char otl[64];
    char new_refresh_token[RS_ACCOUNTS_TOKEN_LEN];
    /** Why it failed, for the title's message line. */
    char error[160];

    /** A sign-in has begun and its answer has not been taken. */
    int busy;
};

/**
 * Start a sign-in with `grant`: the whole record is cleared, the grant set,
 * and it is in flight from here. The caller fills the first half and queues
 * the task. Beginning one while another is in flight is a caller bug.
 */
void
RS_AccountSignIn_Begin(
    struct RS_AccountSignIn* signin,
    enum RS_AccountSignInGrant grant);

/** A sign-in is in flight: begun, its answer not yet taken. */
int
RS_AccountSignIn_Busy(struct RS_AccountSignIn const* signin);

/** For a sign-in in flight: 1 when the task has answered, and it is then no
 *  longer in flight; 0 while it is still out. */
int
RS_AccountSignIn_TakeAnswer(struct RS_AccountSignIn* signin);

#endif
