#ifndef SRC_PLAYERSERVER_PLAYER_LEASE_H
#define SRC_PLAYERSERVER_PLAYER_LEASE_H

/*
 * Which world owns which character's save: the player account server's lock
 * table (player_server.h). No sockets and no locking here -- the server holds
 * its mutex around every call.
 *
 * A row is a note: "world W has this character, and with it the only current
 * save, until it gives the save back." The world keeps the character's save
 * on its own disk while the character plays and sends it at the logout
 * (`/logout`), which is what clears the row. Nothing else does, short of the
 * world saying it holds nothing (DropWorld, its `/hello` after a restart) or
 * an operator taking the character back by hand (`--account release`). There
 * is no expiry: a world that crashes still has the saves on its disk, and the
 * characters wait for it to come back and return them -- a second world
 * logging one in from the older copy here would fork the character.
 *
 * A world asking for a character it already holds is granted it again with a
 * new token, and told it already held it -- its own save is the newer one.
 * That is a reloaded tab or a second login of the same name on the same
 * world, which the world's claim (torirs_server_claim.h) settles by evicting
 * the older session; the older session's token is dead from then on.
 *
 * ## It survives a restart of the server
 *
 * Every change of hands is a line appended to a journal before the answer
 * goes back (`<saves>/player_leases.journal`), and opening replays it. The
 * journal is rewritten from the table when it has grown to several times the
 * table's size, through ToriRSServer_SaveWriteText's temp-and-rename.
 */

#include "player_proto.h"

#include <stdio.h>

struct PlayerLease
{
    char key[PLAYER_KEY_MAX];
    /** The world that owns the character's save now, and which run of it. */
    char world[PLAYER_WORLD_MAX];
    char boot[PLAYER_BOOT_HEX + 1];
    char token[PLAYER_LEASE_TOKEN_HEX + 1];
};

struct PlayerLeases
{
    struct PlayerLease* rows;
    int count;
    int cap;
    /** "" keeps the table in memory only (a test). */
    char journal_path[1024];
    FILE* journal;
    int journal_lines;
};

/**
 * An empty table, then the journal at `journal_path` replayed into it ("" for
 * none). Aborts when the journal cannot be opened for appending: a table that
 * cannot write down what it grants would forget it at the next restart.
 */
void
PlayerLeases_Open(
    struct PlayerLeases* table,
    char const* journal_path);

/** Free the rows and close the journal. NULL is fine. */
void
PlayerLeases_Close(struct PlayerLeases* table);

/**
 * Give `key` to `world`: 1 with a fresh token in `out_token` when nobody
 * holds it or `world` already did (`*out_was_holder` says which); 0 with the
 * holder in `out_holder` when another world does.
 */
int
PlayerLeases_Acquire(
    struct PlayerLeases* table,
    char const* key,
    char const* world,
    char const* boot,
    char out_token[PLAYER_LEASE_TOKEN_HEX + 1],
    int* out_was_holder,
    char* out_holder,
    size_t holder_cap);

/**
 * 1 when `key`'s row is `world`'s under `token`: the only lease that may
 * return the character's save.
 */
int
PlayerLeases_Holds(
    struct PlayerLeases const* table,
    char const* key,
    char const* world,
    char const* token);

/**
 * `world` gives `key` back. 1 when that lease was the live one and is gone; 0
 * when it was not (already released, re-granted under another token, or
 * another world's), which changes nothing.
 */
int
PlayerLeases_Release(
    struct PlayerLeases* table,
    char const* key,
    char const* world,
    char const* token);

/** An operator takes `key` back from whichever world has it: 1 with that
 *  world in `out_world`, 0 when nobody held it. */
int
PlayerLeases_Revoke(
    struct PlayerLeases* table,
    char const* key,
    char* out_world,
    size_t world_cap);

/**
 * Every lease an earlier run of `world` holds -- any boot but `boot` -- gone,
 * except the `keep_count` keys in `keep`, the characters whose saves it still
 * has and is about to return. How many went; each one's key to `dropped`
 * (NULL for no report).
 */
int
PlayerLeases_DropWorld(
    struct PlayerLeases* table,
    char const* world,
    char const* boot,
    char const (*keep)[PLAYER_KEY_MAX],
    int keep_count,
    void (*dropped)(char const* key, void* ctx),
    void* ctx);

/** `key`'s lease, or NULL when nobody holds it. */
struct PlayerLease const*
PlayerLeases_Find(
    struct PlayerLeases const* table,
    char const* key);

#endif
