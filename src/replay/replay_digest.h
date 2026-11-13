#ifndef SRC_REPLAY_REPLAY_DIGEST_H
#define SRC_REPLAY_REPLAY_DIGEST_H

/*
 * Read-only digests of the client's authoritative state, one 64-bit value per
 * domain: what the replay scrubber design promises a seek leaves equal to
 * linear play ("What 'the same as playing there' means"). Compared by
 * `make -C src replay-seek-check` (--replay-digest, main.c).
 *
 * Each domain hashes what the server made true and leaves out what the
 * client derives for drawing (layout, interpolation, animation phase, scene
 * handles). Collections whose storage order is allocation order (inventory
 * containers, interface nodes, registered entities, loc changes, ground
 * items) are combined order-independently; the npc list, whose order is
 * state, is hashed in order.
 */

#include <stdint.h>

struct App;

enum ReplayDigestDomain
{
    REPLAY_DIGEST_VARPS = 0,
    REPLAY_DIGEST_INVS,
    REPLAY_DIGEST_STATS,
    REPLAY_DIGEST_UI,
    REPLAY_DIGEST_CS2,
    REPLAY_DIGEST_PLAYERS,
    REPLAY_DIGEST_PLAYER_TABLE,
    REPLAY_DIGEST_NPCS,
    REPLAY_DIGEST_ZONES,
    REPLAY_DIGEST_SCENE,
    REPLAY_DIGEST_COUNT
};

struct ReplayDigest
{
    uint64_t v[REPLAY_DIGEST_COUNT];
    /** Printed, not compared: the client's clocks (clientclock, logic
     *  cycles), which a seek is not promised to match (replay_digest.c). */
    int client_clock;
    unsigned long long logic_cycle;
};

void
ReplayDigest_Take(
    struct App const* app,
    struct ReplayDigest* out);

/** As ReplayDigest_Take, writing the interface and clientscript-variable
 *  items it hashed to `path`, one a line, for a diff of two runs
 *  (TORIRS_REPLAY_DIGEST_DUMP). */
void
ReplayDigest_TakeDumping(
    struct App const* app,
    struct ReplayDigest* out,
    char const* path);

/** "varps", "invs", "stats", "ui", "cs2", "players", "ptable", "npcs",
 *  "zones", "scene". */
char const*
ReplayDigest_DomainName(int domain);

/** One `replay: digest tick=<T> <domain>=<hex> ...` line on stdout and the
 *  report stream. */
void
ReplayDigest_Print(
    struct ReplayDigest const* d,
    int tick);

#endif
