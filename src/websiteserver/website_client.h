#ifndef SRC_WEBSITESERVER_WEBSITE_CLIENT_H
#define SRC_WEBSITESERVER_WEBSITE_CLIENT_H

/*
 * Asking the website server for its user accounts -- web logins and invites
 * (website_users.h) -- from outside it: `torirsserver --account login, list,
 * revoke, invite`. The website itself opens its database directly
 * (website_users_db.h). Blocking HTTP, signed, to TORIRS_WEBSITE_SERVER
 * (`host:port`), else 127.0.0.1 on WEBSITE_SERVER_PORT. When it does not
 * answer, each call says so -- an UNAVAILABLE result, or -1.
 */

#include "service/http_server.h"
#include "torirsserver/torirs_server_auth.h"

#include <stddef.h>

/** The website is in this process: every call is `handler`. */
void
WebsiteClient_SetLocal(HttpHandler handler);

enum ToriRSServerAuthResult
WebsiteClient_GateLogin(
    const char* name,
    const char* password);

/** `*out_problem` is a sentence for BAD_NAME and WEAK (static storage, until
 *  the next call), else NULL. */
enum ToriRSServerCreateResult
WebsiteClient_GateLoginCreate(
    const char* name,
    const char* password,
    const char** out_problem);

/** 1 revoked, 0 there was no such login, -1 no answer. */
int
WebsiteClient_GateLoginRevoke(const char* name);

/** Every web login's name to `each`. 1 done, -1 no answer. */
int
WebsiteClient_GateLoginEach(void (*each)(const char* name));

/** 1 with the code in `out_code`; -1 no answer (or a name the server
 *  refused, which the caller checked first with AuthNameProblem). */
int
WebsiteClient_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1]);

/** 1 an unredeemed invite (`out_name` its name, "" any), 0 none, -1 no
 *  answer. */
int
WebsiteClient_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap);

/** As WebsiteClient_GateLoginCreate's `out_problem`. */
enum ToriRSServerInviteResult
WebsiteClient_InviteRedeem(
    const char* code,
    const char* name,
    const char* password,
    const char** out_problem);

#endif
