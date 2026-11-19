#ifndef SRC_PLAYERSERVER_PLAYER_PROTO_H
#define SRC_PLAYERSERVER_PLAYER_PROTO_H

/*
 * What the player account server (player_server.h) and its clients
 * (player_client.h) agree on.
 *
 * HTTP, form-encoded bodies in, lines of tab-separated fields out, the first
 * field naming the line -- the account server's shape (account_proto.h), so
 * both read with curl. A text field never holds a tab or a newline: the
 * writer turns any into a space.
 *
 * These are two different servers on purpose. A USER account is a web login:
 * what the sign-in gate and the account site know someone by, a bucket that
 * player accounts are linked into (accountserver/account_server.h). A PLAYER
 * account is a character's credential -- its password, its admin and record
 * flags, which web login it is linked to -- and, since there may be more than
 * one world, the one place that says which world a character is logged in to.
 */

#include <stddef.h>

/** Loopback only; the account server is 8091. */
#define PLAYER_SERVER_PORT 8092

/**
 * A lease: one world's hold on one character, and with it the character's
 * save. Granted at the login's check; held, with the save on the world's own
 * disk, until the world hands the save back at the logout (`/logout`). It does
 * not lapse -- see player_lease.h.
 */
/** A lease's token: 16 random bytes as hex. */
#define PLAYER_LEASE_TOKEN_HEX 32
/** A world's name: TORIRSSERVER_WORLD, or "<host>:<port>". */
#define PLAYER_WORLD_MAX 64
/** A world's boot: 8 random bytes as hex, new each time it starts, so its
 *  `/hello` -- which may arrive late, after a retry -- lets go of only what an
 *  EARLIER run of it held. */
#define PLAYER_BOOT_HEX 16
/** A character as the save is keyed (ToriRSServer_SaveFileName). */
#define PLAYER_KEY_MAX 64
/** The largest save the server takes: a save is a few KB; a full bank, a
 *  house and every varp stay well under this. */
#define PLAYER_SAVE_MAX (4 * 1024 * 1024)

/** One player account, as GET /account and GET /accounts answer it. */
struct PlayerAccountInfo
{
    char key[PLAYER_KEY_MAX];
    int admin;
    int record;
    /** ToriRSServer_AuthManagedBy's: 1 linked to `owner`, 0 not, -1 the
     *  record could not be read. */
    int linked;
    char owner[PLAYER_KEY_MAX];
    /** The world that has the character, and its save, now; "" when none. */
    char world[PLAYER_WORLD_MAX];
};

/** `text`, with tabs and newlines made spaces, into `out`. */
void
PlayerProto_Clean(
    char const* text,
    char* out,
    size_t cap);

/** Split `line` in place at tabs into at most `max` fields. The count. */
int
PlayerProto_Fields(
    char* line,
    char** fields,
    int max);

/** `&name=value` (or `name=value` first) onto `out`, form-encoded. */
void
PlayerProto_FormAdd(
    char* out,
    size_t cap,
    char const* name,
    char const* value);

/** `name`'s value in the form-encoded `data` (`len` bytes), decoded into
 *  `out`. 1 when present and it fits. */
int
PlayerProto_FormGet(
    char const* data,
    size_t len,
    char const* name,
    char* out,
    size_t cap);

/** Split `text` in place into lines (the newline dropped). The count. */
int
PlayerProto_Lines(
    char* text,
    char** lines,
    int max);

#endif
