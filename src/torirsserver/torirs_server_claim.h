/*
 * Which slot a login's character goes into.
 *
 * Both hosts (torirs_server_host.c, torirs_server_embed.c) ask this before they
 * seat a player, because two questions have one answer for both:
 *
 *   - A login for a character that is already in the world. Seated as it
 *     arrived, it became a SECOND player of the same name, loading a stale
 *     save -- or none, for a character that had never logged out -- beside the
 *     old one still standing there. A reloaded browser tab routinely beats the
 *     old socket's close, so this was the common case, not a race.
 *
 *   - A GAMERECONNECT. It presents the cipher seed of the session it wants
 *     back instead of a password, and RECONNECT_OK states no index, so the
 *     client keeps the slot that session was told. The character has to come
 *     back in that slot, and only to a session holding that seed.
 *
 * Identity is the save path, because that is the character: two names that
 * sanitise to the same file are the same character on disk.
 */
#ifndef TORIRS_SERVER_CLAIM_H
#define TORIRS_SERVER_CLAIM_H

#include <stdint.h>

struct ToriRSServer;
struct ToriRSServerPlayer;
struct ToriRSServerSession;

/** Seat the login in any free slot (ToriRSServer_WorldAddPlayer). */
#define TORIRSSERVER_CLAIM_ANY_SLOT (-1)
/** Refuse the login: close its connection. The client reads that as a failed
 *  reconnect and logs in afresh with its password. */
#define TORIRSSERVER_CLAIM_REFUSE (-2)

/** How many recently ended sessions are kept for a reconnect to reclaim. */
#define TORIRSSERVER_CLAIM_DEPARTED_MAX 32

/** How long one stays reclaimable: a page reload on a slow phone plus a cold
 *  boot, with room to spare. */
#define TORIRSSERVER_CLAIM_WINDOW_MS (5L * 60L * 1000L)

/*
 * Sessions that ended recently.
 *
 * A client that loses its link closes the old socket before it redials, and a
 * reloaded tab has lost its socket outright -- so when a reconnect arrives the
 * character has usually already been removed and saved. What it needs back is
 * the slot, and the right to it is the seed that session played on. One per
 * host, zero-initialised.
 */
struct ToriRSServerClaims
{
    struct ToriRSServerClaimDeparted
    {
        char save_path[1024];
        int32_t seed[4];
        int pid;
        long left_ms;
    } departed[TORIRSSERVER_CLAIM_DEPARTED_MAX];
    int next;
};

/**
 * A session with a player is ending. Call before ToriRSServer_WorldRemovePlayer,
 * while the player still names itself.
 */
void
ToriRSServer_ClaimNoteDeparted(
    struct ToriRSServerClaims* claims,
    const struct ToriRSServerPlayer* player,
    long now_ms);

/**
 * Decide where `session`'s login is seated.
 *
 * Returns a pid (seat with ToriRSServer_WorldAddPlayerAt),
 * TORIRSSERVER_CLAIM_ANY_SLOT, or TORIRSSERVER_CLAIM_REFUSE.
 *
 * When the character is online in another session, `*out_evict` is that
 * player: the host must kill its connection and call
 * ToriRSServer_WorldRemovePlayer on it -- which writes the save the new login
 * then loads -- BEFORE seating. It is NULL otherwise, including on a refusal.
 */
int
ToriRSServer_ClaimSlot(
    struct ToriRSServerClaims* claims,
    const struct ToriRSServer* srv,
    const struct ToriRSServerSession* session,
    long now_ms,
    struct ToriRSServerPlayer** out_evict);

#endif
