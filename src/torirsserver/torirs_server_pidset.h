#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_PIDSET_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_PIDSET_H
/*
 * A set of player pool slots, one bit each.
 *
 * What an npc remembers about players ("who damaged me", "who is owed my
 * drop", "who was warned") used to be `unsigned char[TORIRSSERVER_PLAYER_MAX]`
 * per fact, which at a 2000-player cap is 2 KB per fact per npc and, over
 * 4096 npcs and five facts, 41 MB of mostly zeroes. As bits it is 250 bytes.
 *
 * Iteration (ToriRSServer_PidSetNext) visits members in rising pid order, the
 * order the byte-array loops it replaces walked, so anything that picks "the
 * first" keeps picking the same player.
 *
 * Included by torirs_server.h after TORIRSSERVER_PLAYER_MAX is defined.
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TORIRSSERVER_PIDSET_WORDS ((TORIRSSERVER_PLAYER_MAX + 63) / 64)

struct ToriRSServerPidSet
{
    uint64_t words[TORIRSSERVER_PIDSET_WORDS];
};

static inline int
ToriRSServer_PidSetHas(
    const struct ToriRSServerPidSet* set,
    int pid)
{
    assert(set);
    assert(pid >= 0);
    assert(pid < TORIRSSERVER_PLAYER_MAX);
    return (int)((set->words[pid >> 6] >> (pid & 63)) & 1u);
}

static inline void
ToriRSServer_PidSetAdd(
    struct ToriRSServerPidSet* set,
    int pid)
{
    assert(set);
    assert(pid >= 0);
    assert(pid < TORIRSSERVER_PLAYER_MAX);
    set->words[pid >> 6] |= (uint64_t)1 << (pid & 63);
}

static inline void
ToriRSServer_PidSetRemove(
    struct ToriRSServerPidSet* set,
    int pid)
{
    assert(set);
    assert(pid >= 0);
    assert(pid < TORIRSSERVER_PLAYER_MAX);
    set->words[pid >> 6] &= ~((uint64_t)1 << (pid & 63));
}

static inline void
ToriRSServer_PidSetClear(struct ToriRSServerPidSet* set)
{
    assert(set);
    memset(set, 0, sizeof(*set));
}

/** The lowest member >= `from`, or -1. Walk a set with
 *  `for( pid = Next(s, 0); pid >= 0; pid = Next(s, pid + 1) )`. */
static inline int
ToriRSServer_PidSetNext(
    const struct ToriRSServerPidSet* set,
    int from)
{
    assert(set);
    assert(from >= 0);
    for( int word = from >> 6; word < TORIRSSERVER_PIDSET_WORDS; word++ )
    {
        uint64_t bits = set->words[word];

        if( word == from >> 6 )
            bits &= ~(uint64_t)0 << (from & 63);
        if( bits )
        {
            int const pid = (word << 6) + __builtin_ctzll(bits);

            return pid < TORIRSSERVER_PLAYER_MAX ? pid : -1;
        }
    }
    return -1;
}

#endif
