#ifndef SRC_NET_NET_ONCE_H
#define SRC_NET_NET_ONCE_H

/*
 * The network events this client acts on once per run rather than once per
 * session: the first packet the executor runs, which the boot report marks,
 * and the harness's boot cheat, sent the first time it is allowed to be.
 *
 * Neither latch is cleared when a session ends or a new one starts. A
 * reconnect, a world hop or a second login after a logout does not mark a
 * second first packet and does not send the boot cheat again. The one way
 * the cheat goes out again is a repeat cadence: with `every` above 0 it also
 * fires on every logic cycle that is a multiple of `every`.
 *
 * This does not know what the boot report is, what the cheat says, whether
 * one is configured, when a session is in the game stream or the world is
 * loaded, or what the logic cycle is. The caller asks only when there is a
 * packet to mark or a configured cheat that could go now, passes the cycle
 * and the cadence, and does the marking and the sending itself.
 */

#include <stdbool.h>
#include <stdint.h>

struct NetOnce
{
    /** The first packet of the run has been marked. */
    int first_packet_marked;
    /** The boot cheat has gone out at least once. */
    int cheat_sent;
};

/** Nothing marked, nothing sent. */
void
NetOnce_Reset(struct NetOnce* once);

/** True for the first packet of the run, false for every one after it. */
bool
NetOnce_TakeFirstPacket(struct NetOnce* once);

/**
 * Whether a configured cheat goes out on logic cycle `logic_cycle`: always the
 * first time, and after that only when `every` is above 0 and the cycle is a
 * multiple of it. A true answer counts as sent.
 */
bool
NetOnce_CheatFires(
    struct NetOnce* once,
    uint64_t logic_cycle,
    int every);

#endif /* SRC_NET_NET_ONCE_H */
