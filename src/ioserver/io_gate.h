#ifndef SRC_IOSERVER_IO_GATE_H
#define SRC_IOSERVER_IO_GATE_H

/*
 * The sign-in gate: io_server serving nothing but a sign-in page until the
 * visitor has signed in, for a world on the internet (`io_server --gate`).
 *
 *   GET  /login            the sign-in form
 *   POST /login            name + password -> the ticket cookie, then the game
 *   GET  /invite/<code>    the account form for an invite
 *   POST /invite/<code>    name + password twice -> the web login and the cookie
 *   POST /logout           drop the cookie
 *   POST /account/players  the player accounts this web login has linked
 *   POST /account/link     a player's name + password -> linked to this login,
 *                          and a one-time login for it
 *   POST /account/play     a linked player's name -> a one-time login
 *   anything else          the game's own routes, with a good ticket; without
 *                          one a GET goes to /login and the rest are 401
 *
 * The logins are web logins, not players: records in `<saves>/gate/`
 * (TORIRSSERVER_SAVES; torirs_server_auth.h) that come only from invites. Past
 * the page the game asks for a player account of its own. The ticket is the
 * one torirsserver checks on the game WebSocket, under the key file both
 * processes load, so a signed-in browser reaches the game's socket and nobody
 * else's does.
 *
 * A web login can also take player accounts UNDER it (torirs_server_auth.h
 * "Linked player accounts"): the /account routes, whose links live in
 * `<saves>/gate/links.sqlite` (io_gate_links.h).
 *
 * The pages are plain HTML with the styling inline, and nothing else is served
 * before sign-in: no script, no image, no font.
 */

#include "http_server.h"

#define IO_GATE_THROTTLE_SLOTS 64

struct IoGateThrottle
{
    char name[64];
    long long window_start;
    int failures;
};

struct IoGateLinks;

struct IoGate
{
    /** Where a signed-in visitor lands: the page URL with the manifest and
     *  `ws=` arguments the client boots from. */
    char landing[512];
    /** Failed sign-ins per account name, so a password cannot be guessed at
     *  the rate PBKDF2 allows. */
    struct IoGateThrottle throttle[IO_GATE_THROTTLE_SLOTS];
    /** `<saves>/gate/links.sqlite`. */
    struct IoGateLinks* links;
};

/** Load (or create) the gate key and the links database, and remember
 *  `landing`. */
void
IoGate_Init(
    struct IoGate* gate,
    char const* landing);

/**
 * 1 when the gate answered `req` itself and `res` is filled; 0 when the
 * request is signed in and goes on to io_server's own routes.
 */
int
IoGate_Handle(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res);

#endif
