#ifndef SRC_PLAYERSERVER_PLAYER_CLIENT_H
#define SRC_PLAYERSERVER_PLAYER_CLIENT_H

/*
 * Asking the player account server (player_server.h): a world at its logins
 * and logouts, io_server's gate and site, `torirsserver --account`.
 *
 * The calls mirror the record functions in torirs_server_auth.h, which only
 * the server calls now, plus the lease verbs. Every one BLOCKS until the
 * answer: call them where a wait is fine -- a world's pool thread, a page's
 * handler, an operator's command -- and never from a world's loop thread.
 *
 * Where the server is:
 *
 *   PlayerClient_SetLocal    in this process (a world hosting it): the call
 *                            is the handler, no socket
 *   TORIRS_PLAYER_SERVER     `host:port` of a player_server process
 *   neither                  127.0.0.1:PLAYER_SERVER_PORT, where a world that
 *                            hosts it serves it for the processes beside it
 *
 * An unanswered call says so -- an UNAVAILABLE result, or -1 -- so a world
 * refuses the login with "login server offline" and a page says the service
 * is not answering, instead of "wrong password" or "no such player". The
 * calls documented as 0-or-1 count no answer as 0 and print why.
 */

#include "ioserver/http_server.h"
#include "player_proto.h"
#include "torirsserver/torirs_server_auth.h"

#include <stddef.h>

/** As AccountClientExchange (accountserver/account_client.h): how a process
 *  that must not open a blocking socket routes the request (io_server, through
 *  HttpServer_Exchange). `body` NULL is a GET. */
typedef char* (*PlayerClientExchange)(
    char const* host,
    int port,
    char const* route,
    char const* body,
    int* out_size,
    int* out_status);

void
PlayerClient_SetExchange(PlayerClientExchange exchange);

/** The server is in this process: every call is `handler` (PlayerServer_Handle),
 *  directly. NULL goes back to the socket. */
void
PlayerClient_SetLocal(HttpHandler handler);

/** 1 when TORIRS_PLAYER_SERVER names a server (and so a world should not host
 *  one of its own). */
int
PlayerClient_RemoteNamed(void);

/** What a world's login is told. */
struct PlayerLogin
{
    /** enum ToriRSServerAuthResult: OK and CLAIMED hold `lease`; ONLINE means
     *  `holder` has the character; UNAVAILABLE means nothing answered. */
    int result;
    int admin;
    int record;
    char lease[PLAYER_LEASE_TOKEN_HEX + 1];
    char holder[PLAYER_WORLD_MAX];
    /** 1 when this world had the character already: its own copy of the save
     *  is the newer one. */
    int had;
    /** The server's copy of the save (granted only): `save_len` bytes,
     *  NET-allocated, NULL for a character never saved. Free with
     *  PlayerClient_LoginFree. */
    char* save;
    long save_len;
};

/**
 * A world's login of `name`: the password checked (`password` NULL: no check,
 * for a one-time login the world redeemed or a reconnect), the character
 * leased to `world`, and the server's copy of its save.
 */
void
PlayerClient_Login(
    char const* world,
    char const* boot,
    char const* name,
    char const* password,
    struct PlayerLogin* out);

/** The save a login carried. */
void
PlayerClient_LoginFree(struct PlayerLogin* login);

/** A password check with no lease (the gate's OAuth password grant). */
enum ToriRSServerAuthResult
PlayerClient_Auth(
    char const* name,
    char const* password);

/**
 * `world` hands `name` back with its save (`save`, `len` bytes of text). 1
 * taken and the lease gone; 0 refused, the lease not `world`'s under `lease`
 * any more (`out_holder` says who has the character, "" nobody); -1 no
 * answer, so the world keeps the save and asks again.
 */
int
PlayerClient_Logout(
    char const* world,
    char const* name,
    char const* lease,
    char const* save,
    char* out_holder,
    size_t holder_cap);

/** `world` lets go of `name` with no save (the server's copy stands). 1
 *  released, 0 it was not the live lease, -1 no answer. */
int
PlayerClient_Release(
    char const* world,
    char const* name,
    char const* lease);

/**
 * `world` has started (this run is `boot`), still holding the saves of the
 * `count` characters in `keep` (as saves are keyed), which it returns next;
 * every other character an earlier run held is let go. How many were, or -1
 * no answer.
 */
int
PlayerClient_Hello(
    char const* world,
    char const* boot,
    char (*keep)[PLAYER_KEY_MAX],
    int count);

/** An operator takes `name` back from whichever world has it. 1 with that
 *  world in `out_world`; 0 nobody had it; -1 no answer. */
int
PlayerClient_Revoke(
    char const* name,
    char* out_world,
    size_t world_cap);

/**
 * The server's copy of `name`'s save: 1 with `*out_save` (NET-allocated,
 * NUL-terminated, free with ToriMem_Net) and `out_world` the world that has
 * the character now ("" none; while one does, its copy is the newer); 0 no
 * save; -1 no answer.
 */
int
PlayerClient_SaveGet(
    char const* name,
    char** out_save,
    char* out_world,
    size_t world_cap);

/** Replace the server's copy of `name`'s save with `save` (text): 1 written;
 *  0 refused because `out_world` has the character; -1 no answer. */
int
PlayerClient_SavePut(
    char const* name,
    char const* save,
    char* out_world,
    size_t world_cap);

/** The display name registry (torirs_server_savefile.h's ToriRSServer
 *  DisplayNames). Owner: 1 held (`owner` the account), 0 free, -1 no answer.
 *  Claim: 1 the account's, 0 another's or no answer. */
int
PlayerClient_NameOwner(
    char const* name,
    char* owner,
    size_t cap);

int
PlayerClient_NameClaim(
    char const* name,
    char const* account);

void
PlayerClient_NameRelease(
    char const* name,
    char const* account);

/** `name`'s account into `out`. 1 it has a record, 0 it has none, -1 no
 *  answer. */
int
PlayerClient_Account(
    char const* name,
    struct PlayerAccountInfo* out);

/** Every player account whose key holds `filter` ("" every one), to `each`.
 *  How many, or -1 no answer. */
int
PlayerClient_Each(
    char const* filter,
    void (*each)(struct PlayerAccountInfo const* account, void* ctx),
    void* ctx);

/** ToriRSServer_AuthIsAdmin: 0 also for no answer. */
int
PlayerClient_IsAdmin(char const* name);

/** ToriRSServer_AuthIsRecorded: 0 also for no answer. */
int
PlayerClient_IsRecorded(char const* name);

/** ToriRSServer_AuthManagedBy: 1 linked (`out_user` its login), 0 not, -1
 *  unreadable OR no answer -- either way a refusal. */
int
PlayerClient_ManagedBy(
    char const* name,
    char* out_user,
    size_t cap);

/** ToriRSServer_AuthLoginIsStaff: 0 also for no answer. */
int
PlayerClient_LoginIsStaff(char const* login);

/** ToriRSServer_AuthLinkVerify as 1 verified, 0 not, -1 no answer: for a
 *  caller that would act on a "not" (the gate's links list drops the row). */
int
PlayerClient_LinkCheck(
    char const* name,
    char const* user,
    char const* link_token);

/** ToriRSServer_AuthLinkVerify: 0 also for no answer. */
int
PlayerClient_LinkVerify(
    char const* name,
    char const* user,
    char const* link_token);

/** ToriRSServer_AuthLink; LINK_UNAVAILABLE for no answer. */
enum ToriRSServerLinkResult
PlayerClient_Link(
    char const* name,
    char const* password,
    char const* user,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1]);

/** ToriRSServer_AuthAssign; LINK_UNAVAILABLE for no answer. */
enum ToriRSServerLinkResult
PlayerClient_Assign(
    char const* name,
    char const* user,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1]);

/** ToriRSServer_AuthUnlink: 0 also for no answer. */
int
PlayerClient_Unlink(char const* name);

/** ToriRSServer_AuthCreate; CREATE_UNAVAILABLE for no answer. `*out_problem`
 *  is static storage until the next call: not for two threads at once. */
enum ToriRSServerCreateResult
PlayerClient_Create(
    char const* name,
    char const* password,
    int admin,
    char const** out_problem);

/** ToriRSServer_AuthSetPassword: 0 with `*out_problem` (static, as above)
 *  also for no answer. */
int
PlayerClient_SetPassword(
    char const* name,
    char const* password,
    char const** out_problem);

/** ToriRSServer_AuthSetAdmin: 0 also for no answer. */
int
PlayerClient_SetAdmin(
    char const* name,
    int admin);

/** ToriRSServer_AuthSetRecorded: 0 also for no answer. */
int
PlayerClient_SetRecorded(
    char const* name,
    int record);

#endif
