#ifndef SRC_REPLAY_REPLAY_CHASE_H
#define SRC_REPLAY_REPLAY_CHASE_H

/*
 * A replay seek's chase: the tile the recording puts the local player on, and
 * the clock cycles each chase tick charges.
 *
 * During a seek the replay transport withholds the world packets and sends a
 * REPLAY_COORD control frame per recorded tick instead. While a chase is
 * stated, the local player's tile is the one the last frame named, not the
 * world's; it stays stated until it is cleared, by the seek's world keyframe
 * or by the session ending.
 *
 * The clock: each frame names the cycle a chase of 30 cycles a tick executes
 * that tick in, were its pipeline never busy. That chase also runs every cycle
 * this client ran since the previous chase tick, so the charge is the gap from
 * the previous chase cycle plus the cycles run, to the named cycle; a named
 * cycle that the cycles run have already passed is moved up to meet them and
 * charges nothing. The first tick of a chase charges its named cycle whole.
 *
 * This does not read a world, a host or a frame, and it does not print. The
 * caller decodes the frame, passes its coord, cycle and first flag with the
 * logic cycle, adds the charge to its client clock, decides when a stated tile
 * should start replacing the world's, and clears the chase when the world
 * comes back.
 */

#include <stdbool.h>
#include <stdint.h>

struct ReplayChase
{
    /** A chase tick has stated the tile and nothing has cleared it since. */
    int coord_valid;
    /** The stated tile, packed as the client's `coord`. Meaningless while
     *  coord_valid is 0. */
    int coord;
    /** The cycle of a chase of 30 cycles a tick in which the last chase tick
     *  was executed. */
    int64_t cycle;
    /** The logic cycle this client executed that tick in. */
    uint64_t logic_cycle;
};

/** Whether a chase tick has stated the tile and nothing has cleared it. */
bool
ReplayChase_HasCoord(struct ReplayChase const* chase);

/** The stated tile. Only meaningful while ReplayChase_HasCoord. */
int
ReplayChase_Coord(struct ReplayChase const* chase);

/** A chase tick states `coord` as the local player's tile. */
void
ReplayChase_StateCoord(
    struct ReplayChase* chase,
    int coord);

/** The stated tile gives way to the world's. The clock is left as it was. */
void
ReplayChase_ClearCoord(struct ReplayChase* chase);

/**
 * A chase tick executed at `logic_cycle`, naming `cycle`: returns the client
 * cycles to charge and records the tick for the next one.
 *
 * On a `first` tick the charge is `cycle` itself. Otherwise `ran` is
 * `logic_cycle` less the logic cycle of the previous tick, taken as a signed
 * 64-bit value; `cycle` is raised to the previous chase cycle plus `ran` when
 * it is below that, and the charge is `cycle` less both. The raised cycle and
 * `logic_cycle` are what the next tick measures from.
 */
int64_t
ReplayChase_Charge(
    struct ReplayChase* chase,
    int64_t cycle,
    bool first,
    uint64_t logic_cycle);

#endif /* SRC_REPLAY_REPLAY_CHASE_H */
