/* See torirs_server_interface_state.h. */

#include "torirs_server_interface_state.h"
#include <assert.h>

#include <rsareabuf.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int
u16_or_minus_one(int value)
{
    return value >= -1 && value <= 65535;
}

static int
event_compare(const void* a_, const void* b_)
{
    const struct ToriRSServerIfEventRange* a = a_;
    const struct ToriRSServerIfEventRange* b = b_;

    if( a->component_uid != b->component_uid )
        return a->component_uid < b->component_uid ? -1 : 1;
    if( a->start != b->start )
        return a->start < b->start ? -1 : 1;
    if( a->end != b->end )
        return a->end < b->end ? -1 : 1;
    return 0;
}

void
ToriRSServer_IfStateInit(struct ToriRSServerInterfaceState* state)
{
    assert(state);
    memset(state, 0, sizeof(*state));
    state->root_interface = -1;
}

void
ToriRSServer_IfStateOpenTop(
    struct ToriRSServerInterfaceState* state,
    int interface_id)
{
    assert(state);
    state->root_interface = interface_id;
    state->mount_count = 0;
    /* Event overrides are server authority, not widget-instance state. A real
     * gameframe mode switch replaces the root and its wire mount targets but
     * must keep every login/content IF_SETEVENTS permission armed; the V2
     * resync immediately following the remount is what reapplies them to the
     * golden client's freshly loaded widgets. CloseSub removes a closed
     * group's records explicitly. */
}

static int
mount_index(
    const struct ToriRSServerInterfaceState* state,
    int target_uid)
{
    assert(state);
    for( int i = 0; i < state->mount_count; i++ )
        if( state->mounts[i].target_uid == target_uid )
            return i;
    return -1;
}

static int
int_in_list(
    const int* values,
    int count,
    int value)
{
    for( int i = 0; i < count; i++ )
        if( values[i] == value )
            return 1;
    return 0;
}

int
ToriRSServer_IfStateCloseSub(
    struct ToriRSServerInterfaceState* state,
    int target_uid)
{
    int closed_groups[TORIRSSERVER_IF_MOUNT_MAX];
    int closed_count = 0;
    int index;
    int changed;

    assert(state);
    index = mount_index(state, target_uid);
    if( index < 0 )
        return 0;

    closed_groups[closed_count++] = state->mounts[index].interface_id;
    memmove(&state->mounts[index], &state->mounts[index + 1],
            (size_t)(state->mount_count - index - 1) * sizeof(state->mounts[0]));
    state->mount_count--;

    /* A mounted child is addressed by a component uid whose high word is the
     * group it is mounted inside. Closing that group recursively removes it. */
    do
    {
        changed = 0;
        for( int i = 0; i < state->mount_count; )
        {
            int parent = (int)((uint32_t)state->mounts[i].target_uid >> 16);

            if( !int_in_list(closed_groups, closed_count, parent) )
            {
                i++;
                continue;
            }
            if( closed_count < TORIRSSERVER_IF_MOUNT_MAX &&
                !int_in_list(closed_groups, closed_count, state->mounts[i].interface_id) )
                closed_groups[closed_count++] = state->mounts[i].interface_id;
            memmove(&state->mounts[i], &state->mounts[i + 1],
                    (size_t)(state->mount_count - i - 1) * sizeof(state->mounts[0]));
            state->mount_count--;
            changed = 1;
        }
    } while( changed );

    /* The golden client removes widget-event records for the unloaded group
     * from its field6271 table (Statics.method12095). */
    for( int i = 0; i < state->event_count; )
    {
        int group = (int)((uint32_t)state->events[i].component_uid >> 16);

        if( !int_in_list(closed_groups, closed_count, group) )
        {
            i++;
            continue;
        }
        memmove(&state->events[i], &state->events[i + 1],
                (size_t)(state->event_count - i - 1) * sizeof(state->events[0]));
        state->event_count--;
    }
    return 1;
}

int
ToriRSServer_IfStateOpenSub(
    struct ToriRSServerInterfaceState* state,
    int target_uid,
    int interface_id,
    int type)
{
    if( !u16_or_minus_one(interface_id) || type < 0 || type > 255 )
        return 0;
    assert(state);
    if( mount_index(state, target_uid) >= 0 )
        ToriRSServer_IfStateCloseSub(state, target_uid);
    if( state->mount_count >= TORIRSSERVER_IF_MOUNT_MAX )
        return 0;
    state->mounts[state->mount_count].target_uid = target_uid;
    state->mounts[state->mount_count].interface_id = interface_id;
    state->mounts[state->mount_count].type = type;
    state->mount_count++;
    return 1;
}

int
ToriRSServer_IfStateMoveSub(
    struct ToriRSServerInterfaceState* state,
    int source_uid,
    int dest_uid)
{
    struct ToriRSServerIfMount moving;
    int source;

    assert(state);
    if( source_uid == dest_uid )
        return mount_index(state, source_uid) >= 0;
    source = mount_index(state, source_uid);
    if( source < 0 )
    {
        /* The client still retires an occupied destination even when the
         * source is absent. Mirror that observable state transition. */
        ToriRSServer_IfStateCloseSub(state, dest_uid);
        return 0;
    }
    moving = state->mounts[source];
    memmove(&state->mounts[source], &state->mounts[source + 1],
            (size_t)(state->mount_count - source - 1) * sizeof(state->mounts[0]));
    state->mount_count--;
    ToriRSServer_IfStateCloseSub(state, dest_uid);
    moving.target_uid = dest_uid;
    if( state->mount_count >= TORIRSSERVER_IF_MOUNT_MAX )
        return 0;
    state->mounts[state->mount_count++] = moving;
    return 1;
}

int
ToriRSServer_IfStateSeteventsV2(
    struct ToriRSServerInterfaceState* state,
    int component_uid,
    int start,
    int end,
    uint32_t events1,
    uint32_t events2)
{
    struct ToriRSServerIfEventRange next[TORIRSSERVER_IF_EVENT_RANGE_MAX];
    int count = 0;

    if( !u16_or_minus_one(start) || !u16_or_minus_one(end) || start > end )
        return 0;
    assert(state);

    for( int i = 0; i < state->event_count; i++ )
    {
        const struct ToriRSServerIfEventRange* old = &state->events[i];

        if( old->component_uid != component_uid || old->end < start || old->start > end )
        {
            if( count >= TORIRSSERVER_IF_EVENT_RANGE_MAX )
                return 0;
            next[count++] = *old;
            continue;
        }
        if( old->start < start )
        {
            if( count >= TORIRSSERVER_IF_EVENT_RANGE_MAX )
                return 0;
            next[count] = *old;
            next[count].end = start - 1;
            count++;
        }
        if( old->end > end )
        {
            if( count >= TORIRSSERVER_IF_EVENT_RANGE_MAX )
                return 0;
            next[count] = *old;
            next[count].start = end + 1;
            count++;
        }
    }
    if( count >= TORIRSSERVER_IF_EVENT_RANGE_MAX )
        return 0;
    next[count].component_uid = component_uid;
    next[count].start = start;
    next[count].end = end;
    next[count].events1 = events1;
    next[count].events2 = events2;
    count++;

    qsort(next, (size_t)count, sizeof(next[0]), event_compare);
    memcpy(state->events, next, (size_t)count * sizeof(next[0]));
    state->event_count = count;
    return 1;
}

void
ToriRSServer_IfStateSplitClassicEvents(
    uint32_t classic,
    uint32_t* events1,
    uint32_t* events2)
{
    if( events1 )
        *events1 = classic & ~UINT32_C(0x7fe);
    if( events2 )
        *events2 = (classic >> 1) & UINT32_C(0x3ff);
}

void
ToriRSServer_IfStateEncodeSeteventsV2(
    struct RSAreaBuf* buf,
    int component_uid,
    int start,
    int end,
    uint32_t events1,
    uint32_t events2)
{
    assert(buf);
    /* RSProt revision-239 IfSetEventsV2Encoder. */
    rsab_p2_alt2(buf, end);
    rsab_p4(buf, (int32_t)events2);
    rsab_p4_alt2(buf, (int32_t)events1);
    rsab_p2(buf, start);
    rsab_p4_alt3(buf, component_uid);
}

void
ToriRSServer_IfStateEncodeClearinv(
    struct RSAreaBuf* buf,
    int component_uid)
{
    assert(buf);
    /* RSProt revision-239 IfClearInvEncoder: pCombinedIdAlt2. */
    rsab_p4_alt2(buf, component_uid);
}

size_t
ToriRSServer_IfStateResyncPayloadSize(const struct ToriRSServerInterfaceState* state)
{
    if( !state || state->mount_count < 0 || state->mount_count > TORIRSSERVER_IF_MOUNT_MAX ||
        state->event_count < 0 || state->event_count > TORIRSSERVER_IF_EVENT_RANGE_MAX )
        return 0;
    return 4u + (size_t)state->mount_count * 7u + (size_t)state->event_count * 16u;
}

void
ToriRSServer_IfStateEncodeResyncV2(
    struct RSAreaBuf* buf,
    const struct ToriRSServerInterfaceState* state)
{
    assert(state);
    if( ToriRSServer_IfStateResyncPayloadSize(state) == 0 )
        return;
    assert(buf);

    /* RSProt revision-239 IfResyncV2Encoder. There is deliberately no events
     * count: after the stated sub-interface count, all remaining 16-byte
     * records are events. The golden deob uses packet-end as that loop bound. */
    rsab_p2(buf, state->root_interface);
    rsab_p2(buf, state->mount_count);
    for( int i = 0; i < state->mount_count; i++ )
    {
        rsab_p4(buf, state->mounts[i].target_uid);
        rsab_p2(buf, state->mounts[i].interface_id);
        rsab_p1(buf, state->mounts[i].type);
    }
    for( int i = 0; i < state->event_count; i++ )
    {
        rsab_p4(buf, state->events[i].component_uid);
        rsab_p2(buf, state->events[i].start);
        rsab_p2(buf, state->events[i].end);
        rsab_p4(buf, (int32_t)state->events[i].events1);
        rsab_p4(buf, (int32_t)state->events[i].events2);
    }
}
