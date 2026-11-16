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
 *   POST /logout           drop the cookie (and a staff sign-in-as's own)
 *   POST /account/players  the player accounts this web login has linked
 *   POST /account/link     a player's name + password -> linked to this login,
 *                          and a one-time login for it
 *   POST /account/play     a linked player's name -> a one-time login
 *   anything else          the game's own routes, with a good ticket; without
 *                          one a GET goes to /login and the rest are 401
 *
 * The logins are web logins, not players: the account server's
 * (accountserver/account_server.h, asked through account_client.h), made only
 * from invites and by the operator. Past
 * the page the game asks for a player account of its own. The ticket is the
 * one torirsserver checks on the game WebSocket, under the key file both
 * processes load, so a signed-in browser reaches the game's socket and nobody
 * else's does.
 *
 * A web login can also take player accounts UNDER it (torirs_server_auth.h
 * "Linked player accounts"): the /account routes, whose links live in
 * `<saves>/gate/links.sqlite` (io_gate_links.h).
 *
 * Signed in, the visitor lands on the account site (io_site.h): home with
 * Play Now, hiscores, player pages and linked-account management. The game
 * itself stays at the landing URL `--gate` names.
 *
 * The pages are plain HTML with the styling inline, and nothing else is served
 * before sign-in: no script, no image, no font.
 */

#include "http_server.h"

#include <stddef.h>

#define IO_GATE_THROTTLE_SLOTS 64
/* Staff signed in as another web login (IoGate_SignInAs) keep their own
 * ticket in this cookie, so "Return" needs no password. The game's socket
 * never reads it: it checks TORIRSSERVER_GATE_COOKIE, the login being
 * played. */
#define IO_GATE_STAFF_COOKIE "torirs_gate_staff"
/* How long a sign-in-as lasts before the browser falls back to /login. */
#define IO_GATE_SIGN_IN_AS_SECONDS (12LL * 60 * 60)

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

/*
 * For the account site (io_site.c), which runs behind the gate and shares its
 * forms and its link rules.
 */

/** One decoded field of the POST body / of the query string; 0 when absent,
 *  too long for `out`, or holding a control byte. */
int
IoGate_FormField(
    struct HttpRequest const* req,
    char const* key,
    char* out,
    size_t cap);

int
IoGate_QueryField(
    struct HttpRequest const* req,
    char const* key,
    char* out,
    size_t cap);

/** Does the path, up to its query, equal `want`? */
int
IoGate_PathIs(
    char const* path,
    char const* want);

enum IoGateLinkResult
{
    IO_GATE_LINK_LINKED,
    /** The visitor's mistake (no such account, wrong password, another
     *  owner, too many tries); the message says which. */
    IO_GATE_LINK_REFUSED,
    IO_GATE_LINK_FAILED,
};

/**
 * Link the player `username` to the web login `who` with the player's own
 * `password` (wiped here): the record, the links database, and every refresh
 * token the account had revoked. `*out_message` is a sentence for the visitor
 * when it is not LINKED. The game client's /account/link and the site's form
 * both come here.
 */
enum IoGateLinkResult
IoGate_LinkPlayer(
    struct IoGate* gate,
    char const* who,
    char const* username,
    char* password,
    size_t password_cap,
    char const** out_message);

/** Release `player` from `who`, its owner: its own password signs it in again.
 *  0 when `who` does not own it. */
int
IoGate_UnlinkPlayer(
    struct IoGate* gate,
    char const* who,
    char const* player);

/**
 * Make the player account `username`, `admin` 1 for staff, and link it to the
 * web login `owner` at once: it plays from that login's Play Now. No password
 * is asked: the record gets a random one nobody sees, since a linked account
 * never signs in with one. Never over an existing account or an existing
 * character save. `*out_message` as IoGate_LinkPlayer's. Who may name which
 * `owner`, and ask for `admin`, is the caller's (io_site.c: a login for
 * itself; staff for any login, admin too).
 */
enum IoGateLinkResult
IoGate_CreatePlayer(
    struct IoGate* gate,
    char const* owner,
    char const* username,
    int admin,
    char const** out_message);

/**
 * Staff linking the existing player account keyed `player_key` to the web
 * login `login` with no password, moving it from any login that has it now
 * (ToriRSServer_AuthAssign); its refresh tokens are revoked as at any link.
 * The Play Now card keeps the spelling the account was last linked under,
 * else the key. `*out_message` as IoGate_LinkPlayer's. The caller has
 * checked it is staff asking and that `login` exists.
 */
enum IoGateLinkResult
IoGate_AssignPlayer(
    struct IoGate* gate,
    char const* login,
    char const* player_key,
    char const** out_message);

/** 1 when `req` carries a good IO_GATE_STAFF_COOKIE ticket: staff signed in
 *  as another login. `out_staff` gets the staff login. Whether that login is
 *  still staff is the caller's to check. */
int
IoGate_StaffBehind(
    struct HttpRequest const* req,
    char* out_staff,
    size_t cap);

/** Sign the browser in as the web login `login` for
 *  IO_GATE_SIGN_IN_AS_SECONDS, keeping `staff`'s own ticket in the staff
 *  cookie, and 303 to /account. The caller has checked `staff` is staff and
 *  `login` exists. */
void
IoGate_SignInAs(
    struct HttpResponse* res,
    char const* staff,
    char const* login);

/** Back from IoGate_SignInAs: `staff`'s own ticket again, the staff cookie
 *  dropped, 303 to /admin/users. */
void
IoGate_SignInBack(
    struct HttpResponse* res,
    char const* staff,
    char const* login);

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
