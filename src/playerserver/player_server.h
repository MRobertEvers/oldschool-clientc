#ifndef SRC_PLAYERSERVER_PLAYER_SERVER_H
#define SRC_PLAYERSERVER_PLAYER_SERVER_H

/*
 * The player account server: the one owner of player accounts, and the one
 * place that knows which world each character is logged in to.
 *
 * A PLAYER account is a character's credential -- `<saves>/accounts/<name>.ini`,
 * its password, its admin and record flags, the web login it is linked to
 * (torirs_server_auth.h holds the record format and the rules). A USER account
 * is a web login, the account server's (accountserver/account_server.h): a
 * bucket that player accounts are linked into, and nothing a world logs in
 * with. The two servers do not talk to each other.
 *
 * It owns the character's save too (`<saves>/<name>.ini`) and the display
 * name registry (`<saves>/names/`). Everything that reads or writes any of
 * them asks this server through player_client.h -- the worlds at every login
 * and logout, io_server's gate, site and player editor, `torirsserver
 * --account` -- so each has one writer however many processes there are.
 *
 * ## The lock, and who has the save
 *
 * A world asks for a character at its login (`/login`, `/claim`) and gets a
 * LEASE with the server's copy of the save -- or is told which world has the
 * character. From then on the character's save is the WORLD's: it keeps it on
 * its own disk (ToriRSServer_SaveSetCharacterDir), saves there as often as it
 * likes, and nothing here changes. At the logout it hands the save back
 * (`/logout`, the save as the body, under the lease's token): the server
 * writes it, then clears the note. The lease is that note -- "world W has this
 * character's save" -- and it does not lapse: a world that crashes still has
 * the saves on its disk and returns them when it starts again (its `/hello`
 * names the ones it is returning; anything else it held is let go), and until
 * then the character cannot be logged in anywhere else, because the copy
 * here is older than the one on that world. An operator can take a character
 * back from a world that is not coming back (`/account/release`, `--account
 * release`), giving up that world's copy.
 *
 * A save handed back under a lease the world no longer holds is refused, so
 * a world whose character was taken back cannot overwrite the save of
 * whoever has it now.
 *
 * ## Where it runs
 *
 * Its own process (`player_server`, player_server_main.c), for several worlds
 * sharing one saves directory -- every world then names it with
 * TORIRS_PLAYER_SERVER. Or inside a world (PlayerServer_StartInProcess), for
 * the deployment that has one: the world calls the handler directly, and
 * the same handler serves the port for io_server and `--account`. Either way
 * it is HTTP on 127.0.0.1 only: it checks passwords and grants leases on no
 * credential of its own, so it is unreachable from the network rather than
 * trusting it.
 *
 *   POST /auth             name, password        -> auth <result> <admin> <record>
 *   POST /login            world, boot, name, password -> login <result> <admin> <record> <lease>
 *                                                   <holder> <had> <save bytes>, then the save
 *   POST /claim            world, boot, name     -> the same, with no password (a
 *                                                  one-time login the world redeemed,
 *                                                  or a reconnect)
 *   POST /logout?world=&name=&lease=   the save  -> result <0|1> <holder>
 *   POST /release          world, name, lease    -> result <0|1> (no save: the copy
 *                                                  here stands)
 *   POST /hello            world, boot, keep=<key>,...
 *                                                -> result <dropped>: what earlier
 *                                                  runs held, but what it keeps
 *   GET  /leases                                 -> lease <key> <world> ...
 *   GET  /save?name=                             -> save <exists> <world> <bytes>, then it
 *   POST /save?name=       the save              -> result <0|1> <world> (refused while
 *                                                  a world has the character)
 *   GET  /name?name=                             -> name <held> <account>
 *   POST /name/claim       name, account         -> result <0|1>
 *   POST /name/release     name, account         -> result 1
 *   GET  /account?name=                          -> account <key> <exists> <admin> <record>
 *                                                   <linked> <owner> <world>
 *   GET  /accounts[?q=]                          -> account ... per record
 *   GET  /staff?login=                           -> result <0|1>
 *   POST /account/create   name, password, admin -> result <create> <problem>
 *   POST /account/password name, password        -> result <0|1> <problem>
 *   POST /account/admin    name, on              -> result <0|1>
 *   POST /account/record   name, on              -> result <0|1>
 *   POST /account/link     name, password, user  -> link <result> <token>
 *   POST /account/assign   name, user            -> link <result> <token>
 *   POST /account/verify   name, user, token     -> result <0|1>
 *   POST /account/unlink   name                  -> result <0|1>
 *   POST /account/release  name                  -> result <0|1> <world it was taken from>
 *
 * `<had>` is 1 when the world asking already had the character: its own copy
 * of the save is the newer one, and the server's comes along only for a world
 * that has lost its own.
 *
 * `<result>` is an enum ToriRSServerAuthResult / CreateResult / LinkResult as
 * a number; `-` stands for an empty lease or holder. A body that is not what
 * the route takes is a 400 with `error <why>`.
 */

#include "ioserver/http_server.h"

/**
 * Open the lease table (`<saves>/player_leases.journal`). Once per process,
 * before the first request. 0 when another player account server already has
 * this saves directory (`<saves>/player_server.lock`, held while it runs):
 * two would each keep a table of their own -- and the second's open would
 * rewrite the first's journal under it -- so the second must not start.
 */
int
PlayerServer_Open(void);

void
PlayerServer_Close(void);

/**
 * The server's HttpHandler (`user` unused). Safe to call from several threads
 * at once: the in-process world's pool threads call it beside the port's
 * worker. One account's requests are serialised; different accounts' run in
 * parallel, so a password hash does not hold up every other login.
 */
void
PlayerServer_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

/**
 * Serve `port` on 127.0.0.1 from a thread of this process, for the processes
 * beside it. 1 serving; 0 when the port could not be had (another player
 * server is on it), with the reason printed. Needs PlayerServer_Open.
 */
int
PlayerServer_StartInProcess(int port);

/** Stop serving the port and wait for the thread. Nothing when it never
 *  started. */
void
PlayerServer_StopInProcess(void);

#endif
