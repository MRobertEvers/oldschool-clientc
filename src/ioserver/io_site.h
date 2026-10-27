#ifndef SRC_IOSERVER_IO_SITE_H
#define SRC_IOSERVER_IO_SITE_H

/*
 * The account site: what a signed-in visitor to the gate sees (io_gate.h).
 * Server-rendered HTML with the styling inline and nothing else loaded -- no
 * script, no image, no font -- like the gate's own pages.
 *
 *   GET  /                      home: Play Now (the gate's landing URL), this
 *                               login's linked accounts, the top players
 *   GET  /hiscores[?skill=N]    every player ranked overall or by one skill
 *   GET  /players?name=X        look a player up (a redirect to their page)
 *   GET  /players/<name>        a player's stats and ranks; for an account
 *                               linked to this login, its session history too
 *   GET  /account               the login's linked accounts and the link form
 *   POST /account/link-player   name + password -> linked (io_gate.c's rules)
 *   GET  /account/unlink?player=X   the confirmation
 *   POST /account/unlink        released: its own password signs it in again
 *
 * Stats come from the character saves (`<saves>/<name>.ini`, [stats] and the
 * quest-point varp) and history from torirs_server_history.h. Every player's
 * stats are public to anyone past the gate, as hiscores are; a session
 * history is shown only to the login the account is linked to.
 */

#include "http_server.h"

struct IoGate;

/** 1 when `req` is a site page and `res` is filled. `who` is the signed-in
 *  web login, as keyed. */
int
IoSite_Handle(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res);

#endif
