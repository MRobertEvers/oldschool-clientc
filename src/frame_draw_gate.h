#ifndef TORIRS_FRAME_DRAW_GATE_H
#define TORIRS_FRAME_DRAW_GATE_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* Diagnostic draw-only deadline. The caller keeps ticking and pacing normally.
 * Rational deadlines avoid rounding 15 fps up to an 80 ms loop boundary. A late
 * frame starts a fresh interval; it never schedules a burst to repay old draws. */
struct ToriRS_DrawGate { uint64_t next_scaled; bool started; };
static inline bool ToriRS_DrawGateAllow(struct ToriRS_DrawGate* gate,
    uint64_t now_us, int fps)
{
    assert(gate);
    assert(fps > 0);
    assert(fps <= 1000);
    assert(now_us <= UINT64_MAX / (uint64_t)fps - 1000000);
    uint64_t const now_scaled = now_us * (uint64_t)fps;
    if( gate->started && now_scaled < gate->next_scaled ) return false;
    uint64_t const next = gate->next_scaled + 1000000;
    gate->next_scaled = gate->started && next > now_scaled ? next : now_scaled + 1000000;
    gate->started = true;
    return true;
}
#endif
