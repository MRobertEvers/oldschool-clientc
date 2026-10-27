#ifndef SRC_ACCOUNTSERVER_ACCOUNT_SERVER_H
#define SRC_ACCOUNTSERVER_ACCOUNT_SERVER_H

/*
 * The account server: the one process that opens the account database
 * (account_db.h), and the only way anything else reaches it.
 *
 * The game server keeps player accounts -- a character's password and its
 * admin flag, `<saves>/accounts/` -- and nothing else about people. Everything
 * the site shows and the sign-in gate checks lives here: web logins, invites,
 * the hiscores, history and the adventure log. Each world SENDS its records
 * here and never waits for an answer (torirs_server_accountlink.h); io_server
 * and `torirsserver --account` ASK (account_client.h).
 *
 * HTTP on 127.0.0.1 only (`--port`, default ACCOUNT_SERVER_PORT): it checks
 * passwords and takes records on no credential of its own, so it is
 * unreachable from the network rather than trusting it. Every answer is lines
 * of tab-separated fields (account_proto.h).
 *
 *   POST /events?source=<world>   records, one per line -> acked <seq>
 *   POST /gate/login     name, password            -> result <auth result>
 *   POST /gate/create    name, password            -> result <create> <problem>
 *   POST /gate/revoke    name                      -> result <0|1>
 *   GET  /gate/list                                -> login <name> ...
 *   POST /invite/create  [name]                    -> code <code>
 *   GET  /invite?code=                             -> invite <0|1> <name>
 *   POST /invite/redeem  code, name, password      -> result <invite> <problem>
 *   POST /player/ranked  name, ranked              -> result 1
 *   GET  /view/player?key=&history=0|1             -> account_proto.h's view
 *   GET  /view/hiscores?category=&first=&max=      -> ranked <n>, row ...
 *
 * A body that is not what the route takes is a 400 with `error <why>`.
 */

#include "ioserver/http_server.h"

#define ACCOUNT_SERVER_PORT 8091
/* The most records one POST /events applies. */
#define ACCOUNT_SERVER_BATCH_MAX 512

/** The server's HttpHandler (`user` unused). Opens the database on first
 *  use. */
void
AccountServer_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

#endif
