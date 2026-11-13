/*
 * The keyframe mailbox (replay_world.h). No App: the transport and its tests
 * link this without a client.
 */
#include "replay/replay_world.h"

#include <assert.h>
#include <stdlib.h>

static struct ReplayWorldDelivery* s_posted;
static uint32_t s_serial;

uint32_t
ReplayWorld_Post(struct ReplayWorldDelivery* d)
{
    assert(d);
    /* The scrubber waits for the slot (ReplayWorld_Pending). */
    assert(!s_posted);
    s_posted = d;
    s_serial++;
    if( s_serial == 0 )
        s_serial = 1;
    return s_serial;
}

struct ReplayWorldDelivery*
ReplayWorld_Take(uint32_t serial)
{
    struct ReplayWorldDelivery* d;

    if( serial != s_serial || !s_posted )
        return NULL;
    d = s_posted;
    s_posted = NULL;
    return d;
}

void
ReplayWorld_Withdraw(uint32_t serial)
{
    assert(serial == s_serial);
    (void)serial;
    assert(s_posted);
    ReplayWorldDelivery_Free(s_posted);
    s_posted = NULL;
}

int
ReplayWorld_Pending(void)
{
    return s_posted != NULL;
}

void
ReplayWorldDelivery_Free(struct ReplayWorldDelivery* d)
{
    if( !d )
        return;
    ReplayWorldKeyframe_Free(&d->kf);
    free(d->scene_bytes);
    free(d);
}
