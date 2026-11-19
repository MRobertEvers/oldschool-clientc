/*
 * The event vocabulary and ring both clients keep (rs_event_ring.h).
 */

#include "game/rs_event_ring.h"

#include <assert.h>
#include <string.h>

/* Lua-visible: tests await these strings. In enum RS_EventKind order. */
static char const* const RS_EVENT_NAMES[RS_EVENT_KIND_COUNT] = {
    "none",
    "sub_opened",
    "sub_mounted",
    "sub_closed",
    "chat_opened",
    "slot_mounted",
    "resume_answered",
    "varp_changed",
    "inv_changed",
    "obj_added",
    "obj_removed",
    "map_flag",
    "chat_message",
    "server_tick",
    "npc_spawn",
    "npc_despawn",
    "npc_retype",
    "inv_packet",
    "npc_seq",
    "npc_face",
};

char const*
RS_EventKindName(int kind)
{
    assert(kind >= 0);
    assert(kind < RS_EVENT_KIND_COUNT);
    return RS_EVENT_NAMES[kind];
}

int
RS_EventKindFromName(char const* name)
{
    assert(name);
    for( int kind = 0; kind < RS_EVENT_KIND_COUNT; kind++ )
        if( strcmp(name, RS_EVENT_NAMES[kind]) == 0 )
            return kind;
    return -1;
}

struct RS_Event const*
RS_EventRing_Push(
    struct RS_EventRing* ring,
    struct RS_Event* entries,
    int capacity,
    enum RS_EventRingFull full,
    struct RS_Event const* ev)
{
    struct RS_Event* slot;

    assert(ring);
    assert(entries);
    assert(capacity > 0);
    assert(ev);
    assert(ev->kind > RS_EVENT_NONE);
    assert(ev->kind < RS_EVENT_KIND_COUNT);
    assert(ring->write_index >= 0);
    assert(ring->write_index < capacity);

    slot = &entries[ring->write_index];
    if( ring->count == capacity )
    {
        /* The entry about to be overwritten is the oldest one. */
        if( full == RS_EVENT_RING_COUNT_DROPS )
            ring->dropped++;
        ring->oldest_serial = slot->serial + 1;
    }
    else
    {
        ring->count++;
    }

    *slot = *ev;
    slot->serial = ++ring->newest_serial;
    if( ring->oldest_serial == 0 )
        ring->oldest_serial = slot->serial;
    ring->write_index = (ring->write_index + 1) % capacity;
    return slot;
}

int
RS_EventRing_Read(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    int capacity,
    uint32_t after_serial,
    struct RS_Event* out,
    int cap,
    int* out_count,
    uint32_t* out_serial)
{
    int written = 0;
    int first;
    int skip = 0;
    uint32_t first_serial;

    assert(ring);
    assert(entries);
    assert(capacity > 0);
    assert(out);
    assert(cap > 0);
    assert(out_count);
    assert(out_serial);

    *out_count = 0;
    *out_serial = ring->newest_serial;
    if( ring->count == 0 )
        return 1;
    /* A cursor of 0 is "everything you still hold", how a reader that has
     * never polled starts. Any other cursor below the oldest surviving serial
     * has missed entries. */
    if( after_serial != 0 && after_serial + 1 < ring->oldest_serial )
        return 0;

    first = ring->write_index - ring->count;
    if( first < 0 )
        first += capacity;
    /* Serials are consecutive, so the entries at or below the cursor are a
     * prefix whose length is arithmetic: start past it rather than walk the
     * whole ring every pump to skip it. */
    first_serial = ring->newest_serial - (uint32_t)ring->count + 1u;
    if( after_serial >= first_serial )
        skip = (int)(after_serial - first_serial) + 1;
    for( int i = skip; i < ring->count && written < cap; i++ )
    {
        struct RS_Event const* entry = &entries[(first + i) % capacity];
        assert(entry->serial > after_serial);
        out[written++] = *entry;
    }
    *out_count = written;
    /* The batch's last serial, not the ring's newest: a truncated read must be
     * re-polled from where it stopped. */
    if( written > 0 )
        *out_serial = out[written - 1].serial;
    return 1;
}

struct RS_Event const*
RS_EventRing_At(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    int capacity,
    uint32_t serial)
{
    int back;
    int index;

    assert(ring);
    assert(entries);
    assert(capacity > 0);
    assert(ring->count > 0);
    assert(serial >= ring->oldest_serial);
    assert(serial <= ring->newest_serial);

    back = (int)(ring->newest_serial - serial);
    index = ring->write_index - 1 - back;
    if( index < 0 )
        index += capacity;
    assert(entries[index].serial == serial);
    return &entries[index];
}
