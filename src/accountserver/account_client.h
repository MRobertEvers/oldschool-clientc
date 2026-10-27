#ifndef SRC_ACCOUNTSERVER_ACCOUNT_CLIENT_H
#define SRC_ACCOUNTSERVER_ACCOUNT_CLIENT_H

/*
 * Asking the account server (account_server.h): io_server's gate and site,
 * and `torirsserver --account`. Blocking HTTP to localhost, so for processes
 * where a wait is acceptable -- a page load, an operator's command -- and
 * never the game's tick, which only sends (torirs_server_accountlink.h).
 *
 * The server is TORIRS_ACCOUNT_SERVER (`host:port`), else 127.0.0.1 on
 * ACCOUNT_SERVER_PORT. When it does not answer, each call says so -- an
 * UNAVAILABLE result, or -1 -- so a page can say "the account service is not
 * answering" instead of "wrong password" or "no such player".
 */

#include "account_proto.h"
#include "torirsserver/torirs_server_auth.h"

#include <stddef.h>

enum ToriRSServerAuthResult
AccountClient_GateLogin(
    const char* name,
    const char* password);

/** `*out_problem` is a sentence for BAD_NAME and WEAK (static storage, until
 *  the next call), else NULL. */
enum ToriRSServerCreateResult
AccountClient_GateLoginCreate(
    const char* name,
    const char* password,
    const char** out_problem);

/** 1 revoked, 0 there was no such login, -1 no answer. */
int
AccountClient_GateLoginRevoke(const char* name);

/** Every web login's name to `each`. 1 done, -1 no answer. */
int
AccountClient_GateLoginEach(void (*each)(const char* name));

/** 1 with the code in `out_code`; -1 no answer (or a name the server
 *  refused, which the caller checked first with AuthNameProblem). */
int
AccountClient_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1]);

/** 1 an unredeemed invite (`out_name` its name, "" any), 0 none, -1 no
 *  answer. */
int
AccountClient_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap);

/** As AccountClient_GateLoginCreate's `out_problem`. */
enum ToriRSServerInviteResult
AccountClient_InviteRedeem(
    const char* code,
    const char* name,
    const char* password,
    const char** out_problem);

/** 1 set, -1 no answer. */
int
AccountClient_SetRanked(
    const char* display_name,
    int ranked);

/** The page view (account_proto.h); `history` 1 for the owner's. 1 found, 0
 *  no such player, -1 no answer. */
int
AccountClient_PlayerView(
    const char* key,
    int history,
    struct AccountPlayerView* out);

/** Ranks `first`.. of `category`, at most `max`, into `rows`; how many the
 *  category ranks into `*out_ranked`. The rows' count, or -1 no answer. */
int
AccountClient_Hiscores(
    int category,
    int first,
    int max,
    struct AccountRow* rows,
    int* out_ranked);

#endif
