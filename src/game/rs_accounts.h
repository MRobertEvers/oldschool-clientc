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

#endif
