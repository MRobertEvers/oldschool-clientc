#ifndef SRC_WEBSITESERVER_WEBSITE_USERS_DB_H
#define SRC_WEBSITESERVER_WEBSITE_USERS_DB_H

/*
 * The website's user accounts: `<saves>/website.sqlite`, opened by the
 * website server and by nothing else.
 *
 *   web_login(key, name, password, created_at)   the gate's logins
 *   invite(code, name, created_at)                unredeemed invites
 *
 * A USER account is a web login: what the sign-in gate and the account site
 * know a visitor by, a bucket that player accounts are linked into
 * (website_links.h). It is nothing a world logs in with -- player accounts are
 * the account server's (accountserver/account_server.h).
 *
 * The website calls these directly; anything outside it asks through the
 * website's /svc/users routes (website_users.h, website_client.h).
 *
 * **Made once, from what it replaced.** The first open creates the schema and
 * copies in the logins and invites `<saves>/accounts.sqlite` held -- the
 * database before the split, whose records went to the hiscores server -- or,
 * with no such file, the gate's files before it (`<saves>/gate/<name>.ini`,
 * `gate/invites/<code>.ini`). Neither is read again.
 *
 * Every function opens the database on first use and aborts when it cannot:
 * a gate whose logins silently failed to open would refuse everyone.
 */

#include "torirsserver/torirs_server_auth.h"

#include <stddef.h>

/** `<saves>/website.sqlite`. Static storage. */
const char*
WebsiteUsersDb_Path(void);

/** Open the database now rather than at first use, importing what it
 *  replaced if it is new: the server's boot, so a problem shows there. */
void
WebsiteUsersDb_Open(void);

/** Close this process's connection; the next call opens it again. */
void
WebsiteUsersDb_Close(void);

/* ------------------------------------------------------------------ */
/* Web logins and invites                                              */
/* ------------------------------------------------------------------ */

/**
 * Check a web login for the sign-in gate. Never claims: a name with no login
 * is REFUSED, because every one came from an invite. Not a player account;
 * see torirs_server_auth.h.
 */
enum ToriRSServerAuthResult
WebsiteUsersDb_GateLogin(
    const char* name,
    const char* password);

/** An operator making a web login without an invite. Same rules as an
 *  invite's. */
enum ToriRSServerCreateResult
WebsiteUsersDb_GateLoginCreate(
    const char* name,
    const char* password,
    const char** out_problem);

/** Delete a web login: it cannot sign in again. A ticket already issued
 *  lasts until it expires, as when the login was a file and revoking it was
 *  deleting the file. 1 when there was one. */
int
WebsiteUsersDb_GateLoginRevoke(const char* name);

/** Every web login's name, oldest first, each handed to `each`. */
void
WebsiteUsersDb_GateLoginEach(
    void (*each)(const char* name, void* ctx),
    void* ctx);

/**
 * Make an invite, redeemed at the gate's `/invite/<code>` for a web login.
 * `name` NULL lets the visitor choose the name; otherwise only that name can
 * redeem it (it must pass AuthNameProblem).
 */
void
WebsiteUsersDb_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1]);

/** 1 when `code` is an unredeemed invite; `out_name` gets the name it is for,
 *  "" when the visitor chooses. */
int
WebsiteUsersDb_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap);

/** Spend `code` on a new web login `name` / `password`. The login and the
 *  spending are one transaction, so an invite is never spent twice.
 *  `*out_problem` is a sentence for BAD_NAME and WEAK, else NULL. */
enum ToriRSServerInviteResult
WebsiteUsersDb_InviteRedeem(
    const char* code,
    const char* name,
    const char* password,
    const char** out_problem);

#endif
