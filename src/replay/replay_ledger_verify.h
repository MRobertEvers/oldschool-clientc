#ifndef SRC_REPLAY_REPLAY_LEDGER_VERIFY_H
#define SRC_REPLAY_REPLAY_LEDGER_VERIFY_H

/*
 * The world ledger's equalities, for test-replay-ledger (a synthetic
 * recording) and replay-ledger-check (real ones). Each returns 1, or 0 with a
 * sentence in `why`.
 */

#include <stdint.h>

struct ReplayLedger;

/** Every op's interval: sorted by birth, died >= born, and every end has a
 *  cause at that frame (a rebuild, a zone clear covering the tile, or an op on
 *  the same tile and key). */
int
ReplayLedgerVerify_Intervals(
    struct ReplayLedger const* ledger,
    char* why,
    int why_cap);

/**
 * At each of `frames` (ascending): the stabbing query equals a scan of every
 * interval, at most one loc op is alive per (tile, layer), and the world a
 * client builds from the live ops alone -- applied in birth order to an empty
 * scene, by the executor's rules (locs by key, stacks by first match in
 * arrival order, FindExact by id and count) -- equals the one it builds from
 * every op up to that frame with the zone clears and rebuilds between them.
 */
int
ReplayLedgerVerify_Zones(
    struct ReplayLedger const* ledger,
    uint32_t const* frames,
    int count,
    char* why,
    int why_cap);

/** At each of `ticks` (ascending): the nearest key advanced to the tick equals
 *  one fold from tick 0. */
int
ReplayLedgerVerify_Entities(
    struct ReplayLedger const* ledger,
    int const* ticks,
    int count,
    char* why,
    int why_cap);

#endif
