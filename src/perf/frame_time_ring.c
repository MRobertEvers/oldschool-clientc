#include "perf/frame_time_ring.h"

#include <assert.h>
#include <string.h>

void
FrameTimeRing_Reset(struct FrameTimeRing* ring)
{
    assert(ring);
    memset(ring, 0, sizeof(*ring));
}

void
FrameTimeRing_Add(
    struct FrameTimeRing* ring,
    uint64_t frame_us)
{
    assert(ring);

    ring->samples_us[ring->head] = frame_us > UINT32_MAX ? UINT32_MAX : (uint32_t)frame_us;
    ring->head = (ring->head + 1) % FRAME_TIME_RING_SAMPLES;
    if( ring->count < FRAME_TIME_RING_SAMPLES )
        ring->count++;
}

uint32_t
FrameTimeRing_NewestUs(struct FrameTimeRing const* ring)
{
    assert(ring);

    /* head is where the NEXT sample lands, so the newest is the slot before
     * it, wrapping.
     *
     * No emptiness test. Reset zeroes the ring, so an unwritten slot already
     * reads 0 -- which is the answer an emptiness test would give, and a test
     * that cannot change an answer is one more thing to keep true. The mean
     * below does need its own, because it divides by the count. */
    return ring->samples_us[(ring->head + FRAME_TIME_RING_SAMPLES - 1) % FRAME_TIME_RING_SAMPLES];
}

uint32_t
FrameTimeRing_MeanUs(struct FrameTimeRing const* ring)
{
    uint64_t total = 0;

    assert(ring);
    if( ring->count <= 0 )
        return 0;
    for( int i = 0; i < ring->count; i++ )
        total += ring->samples_us[i];
    return (uint32_t)(total / (uint64_t)ring->count);
}
