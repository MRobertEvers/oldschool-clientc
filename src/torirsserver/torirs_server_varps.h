#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_VARPS_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_VARPS_H
/*
 * One player's varps, stored sparsely.
 *
 * The dense array this replaces was `int32_t[TORIRSSERVER_VARP_COUNT]`, 38,400
 * bytes on every player, and a real save sets very few of them: on 2026-10-28
 * the largest local saves held 60 and 36 non-zero perm varps, scattered over
 * 29 and 20 different 64-varp pages, so paging would have cost 7 KB where this
 * costs under 1 KB. Play adds a few dozen temp-scope ones (combat, dialogue).
 *
 * Open addressing with linear probing over two parallel arrays (`ids`,
 * `values`) in one allocation. The meaning is the dense array's exactly:
 *
 *   - an id with no entry reads 0;
 *   - writing 0 removes the entry (backward-shift delete, so there are no
 *     tombstones and a map that churns never fills with dead slots).
 *
 * So "is this varp set" and "is it non-zero" stay the same question, and a
 * zeroed struct is an empty map. Nothing is allocated until the first non-zero
 * write; ToriRSServer_VarpsFree returns the map to that state.
 */
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/** A free slot. Every real id is below it (static-asserted in torirs_server.h). */
#define TORIRSSERVER_VARPS_EMPTY 0xFFFFu
/** The first allocation's slot count. A power of two. */
#define TORIRSSERVER_VARPS_INITIAL 64u

struct ToriRSServerVarps
{
    /** `mask + 1` slots; TORIRSSERVER_VARPS_EMPTY where nothing is stored.
     *  NULL until the first non-zero write. */
    uint16_t* ids;
    /** Parallel to `ids`, in the same allocation. */
    int32_t* values;
    /** Live entries. */
    uint32_t count;
    /** Slot count - 1. 0 while `ids` is NULL. */
    uint32_t mask;
};

static inline uint32_t
torirs_server_varps_home(
    uint32_t id,
    uint32_t mask)
{
    /* Fibonacci hashing: neighbouring ids (a quest's block of varps) land far
     * apart, which is what keeps the probe runs short. */
    return ((id * 0x9E3779B1u) >> 16) & mask;
}

/** The slot holding `id`, or -1. */
static inline int
torirs_server_varps_find(
    const struct ToriRSServerVarps* varps,
    uint32_t id)
{
    uint32_t slot;

    if( !varps->ids )
        return -1;
    slot = torirs_server_varps_home(id, varps->mask);
    for( ;; )
    {
        uint32_t at = varps->ids[slot];

        if( at == id )
            return (int)slot;
        if( at == TORIRSSERVER_VARPS_EMPTY )
            return -1;
        slot = (slot + 1) & varps->mask;
    }
}

static inline int32_t
ToriRSServer_VarpsGet(
    const struct ToriRSServerVarps* varps,
    int id)
{
    int slot;

    assert(varps);
    assert(id >= 0);
    assert((uint32_t)id < TORIRSSERVER_VARPS_EMPTY);
    slot = torirs_server_varps_find(varps, (uint32_t)id);
    return slot < 0 ? 0 : varps->values[slot];
}

/** Every slot, as one block: `slots` ids then `slots` values. */
static inline void
torirs_server_varps_alloc(
    struct ToriRSServerVarps* varps,
    uint32_t slots)
{
    uint8_t* block = (uint8_t*)ToriMem_Alloc(
        ToriMem_ServerPlayers(), (size_t)slots * (sizeof(uint16_t) + sizeof(int32_t)));

    assert(block);
    /* Values first: the int32 half needs the stricter alignment. */
    varps->values = (int32_t*)block;
    varps->ids = (uint16_t*)(block + (size_t)slots * sizeof(int32_t));
    memset(varps->ids, 0xFF, (size_t)slots * sizeof(uint16_t));
    varps->mask = slots - 1;
    varps->count = 0;
}

static inline void
torirs_server_varps_insert_new(
    struct ToriRSServerVarps* varps,
    uint32_t id,
    int32_t value)
{
    uint32_t slot = torirs_server_varps_home(id, varps->mask);

    while( varps->ids[slot] != TORIRSSERVER_VARPS_EMPTY )
        slot = (slot + 1) & varps->mask;
    varps->ids[slot] = (uint16_t)id;
    varps->values[slot] = value;
    varps->count++;
}

static inline void
torirs_server_varps_grow(struct ToriRSServerVarps* varps)
{
    struct ToriRSServerVarps old = *varps;
    uint32_t slots = old.ids ? (old.mask + 1) * 2 : TORIRSSERVER_VARPS_INITIAL;

    torirs_server_varps_alloc(varps, slots);
    if( !old.ids )
        return;
    for( uint32_t i = 0; i <= old.mask; i++ )
        if( old.ids[i] != TORIRSSERVER_VARPS_EMPTY )
            torirs_server_varps_insert_new(varps, old.ids[i], old.values[i]);
    ToriMem_Free(ToriMem_ServerPlayers(), old.values);
}

/** Remove the entry at `slot`, shifting the rest of its probe run back. */
static inline void
torirs_server_varps_remove_at(
    struct ToriRSServerVarps* varps,
    uint32_t slot)
{
    uint32_t hole = slot;
    uint32_t next = slot;

    for( ;; )
    {
        uint32_t home;

        next = (next + 1) & varps->mask;
        if( varps->ids[next] == TORIRSSERVER_VARPS_EMPTY )
            break;
        home = torirs_server_varps_home(varps->ids[next], varps->mask);
        /* The entry at `next` may fill the hole only if the hole lies on its
         * probe path, i.e. between its home and where it sits now. */
        if( ((next - home) & varps->mask) >= ((next - hole) & varps->mask) )
        {
            varps->ids[hole] = varps->ids[next];
            varps->values[hole] = varps->values[next];
            hole = next;
        }
    }
    varps->ids[hole] = (uint16_t)TORIRSSERVER_VARPS_EMPTY;
    varps->count--;
}

/** Store `value` at `id`. 0 removes the entry. */
static inline void
ToriRSServer_VarpsPut(
    struct ToriRSServerVarps* varps,
    int id,
    int32_t value)
{
    int slot;

    assert(varps);
    assert(id >= 0);
    assert((uint32_t)id < TORIRSSERVER_VARPS_EMPTY);
    slot = torirs_server_varps_find(varps, (uint32_t)id);
    if( slot >= 0 )
    {
        if( value == 0 )
            torirs_server_varps_remove_at(varps, (uint32_t)slot);
        else
            varps->values[slot] = value;
        return;
    }
    if( value == 0 )
        return;
    /* Grow at 3/4 load, before the insert, so a probe always ends on a hole. */
    if( !varps->ids || (varps->count + 1) * 4 > (varps->mask + 1) * 3 )
        torirs_server_varps_grow(varps);
    torirs_server_varps_insert_new(varps, (uint32_t)id, value);
}

/** Release the storage; the map reads as all zeroes afterwards. An empty map
 *  (nothing ever stored) is fine to free. */
static inline void
ToriRSServer_VarpsFree(struct ToriRSServerVarps* varps)
{
    assert(varps);
    if( varps->ids )
        ToriMem_Free(ToriMem_ServerPlayers(), varps->values);
    memset(varps, 0, sizeof(*varps));
}

/** Make `dst` an independent copy of `src`. `dst`'s old storage is freed. */
static inline void
ToriRSServer_VarpsCopy(
    struct ToriRSServerVarps* dst,
    const struct ToriRSServerVarps* src)
{
    size_t bytes;

    assert(dst);
    assert(src);
    assert(dst != src);
    ToriRSServer_VarpsFree(dst);
    if( !src->ids )
        return;
    torirs_server_varps_alloc(dst, src->mask + 1);
    bytes = (size_t)(src->mask + 1) * (sizeof(uint16_t) + sizeof(int32_t));
    memcpy(dst->values, src->values, bytes);
    dst->count = src->count;
}

/** Bytes this map holds on the heap. */
static inline size_t
ToriRSServer_VarpsBytes(const struct ToriRSServerVarps* varps)
{
    assert(varps);
    return varps->ids ? (size_t)(varps->mask + 1) * (sizeof(uint16_t) + sizeof(int32_t)) : 0;
}

#endif
