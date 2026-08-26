#include "pacer.h"

#include "log/torirs_log.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
ToriRS_Pacer_Init(
    struct ToriRS_Pacer* pacer, enum ToriRS_PacerKind kind, int period_ms, int mindel_ms)
{
    assert(pacer);
    assert(period_ms > 0);
    assert(mindel_ms >= 0);

    memset(pacer, 0, sizeof(*pacer));
    pacer->kind = kind;
    pacer->period_ms = period_ms;
    pacer->mindel_ms = mindel_ms;
    /* GameShell's own initialisers: ratio 256 is "exactly on budget" and del 1
     * is the value the loop falls back to whenever it is behind. */
    pacer->ratio = 256;
    pacer->del_ms = mindel_ms < 1 ? 0 : 1;
    pacer->count = 0;
    pacer->last_logic_ticks = 0;
    pacer->trace = getenv("TORIRS_PACER_TRACE") &&
                   atoi(getenv("TORIRS_PACER_TRACE")) != 0;
}

void
ToriRS_Pacer_NoteFrame(struct ToriRS_Pacer* pacer, uint64_t now_us, uint64_t wait_us)
{
    assert(pacer);

    if( !pacer->trace )
        return;

    if( pacer->trace_prev_us != 0 )
        pacer->trace_period_us += now_us - pacer->trace_prev_us;
    pacer->trace_prev_us = now_us;
    if( pacer->trace_start_us == 0 )
    {
        pacer->trace_start_us = now_us;
        return;
    }

    pacer->trace_wait_us += wait_us;
    pacer->trace_del_us += (uint64_t)(pacer->del_ms > 0 ? pacer->del_ms : 0) * 1000u;
    pacer->trace_ratio_sum += (uint64_t)pacer->ratio;
    pacer->trace_frames++;
    if( pacer->del_ms <= pacer->mindel_ms )
        pacer->trace_at_mindel++;

    if( now_us - pacer->trace_start_us >= 2000000u && pacer->trace_frames > 0 )
    {
        double n = (double)pacer->trace_frames;
        double win_ms = (double)(now_us - pacer->trace_start_us) / 1000.0;
        double period = pacer->trace_period_us / 1000.0 / n;
        double wait = pacer->trace_wait_us / 1000.0 / n;
        double req = pacer->trace_del_us / 1000.0 / n;

        /*
         * `work` is what is left of the period once the wait is taken out, and
         * it is the number that says which regime this is. Work under the
         * budget with a wait filling the rest means the cap is being met; work
         * at or over the budget with the wait pinned to mindel is the shape
         * that costs the Java client 41% of its frame.
         */
        if( pacer->kind == TORIRS_PACER_GAMESHELL )
            TORIRS_REPORT(
                "[pacer] gameshell fps=%.2f period=%.2f work=%.2f wait=%.2f "
                "(req %.2f) ratio=%.0f atmin=%.0f%% budget=%dms\n",
                n * 1000.0 / win_ms,
                period,
                period - wait,
                wait,
                req,
                (double)pacer->trace_ratio_sum / n,
                100.0 * (double)pacer->trace_at_mindel / n,
                pacer->period_ms);
        else
            /* No del and no ratio on this pacer -- it waits to a deadline and
             * lets App_RunOnce count the ticks -- so printing either would be
             * printing an Init value back. */
            TORIRS_REPORT(
                "[pacer] deadline fps=%.2f period=%.2f work=%.2f wait=%.2f "
                "budget=%dms\n",
                n * 1000.0 / win_ms,
                period,
                period - wait,
                wait,
                pacer->period_ms);

        pacer->trace_start_us = now_us;
        pacer->trace_wait_us = 0;
        pacer->trace_period_us = 0;
        pacer->trace_del_us = 0;
        pacer->trace_ratio_sum = 0;
        pacer->trace_frames = 0;
        pacer->trace_at_mindel = 0;
    }
}

enum ToriRS_PacerKind
ToriRS_Pacer_KindFromName(char const* name, int* ok)
{
    assert(name);
    assert(ok);

    if( strcmp(name, "gameshell") == 0 )
    {
        *ok = 1;
        return TORIRS_PACER_GAMESHELL;
    }
    if( strcmp(name, "deadline") == 0 )
    {
        *ok = 1;
        return TORIRS_PACER_DEADLINE;
    }
    *ok = 0;
    return TORIRS_PACER_GAMESHELL;
}

char const*
ToriRS_Pacer_KindName(enum ToriRS_PacerKind kind)
{
    return kind == TORIRS_PACER_DEADLINE ? "deadline" : "gameshell";
}

int
ToriRS_Pacer_LastLogicTicks(struct ToriRS_Pacer const* pacer)
{
    assert(pacer);
    return pacer->last_logic_ticks;
}

/*
 * `GameShell.run()`'s rate estimator, transcribed.
 *
 * `otim` holds the last ten iteration timestamps. The one at `opos` is the
 * oldest, so `now - otim[opos]` is how long the last ten iterations took, and
 * `deltime * 2560 / that` is 256 when the client is exactly on its budget.
 * Everything below -- including the odd-looking bump of the whole ring by the
 * pending wait -- is the reference's, kept shape-for-shape so this can be
 * compared against the Java client rather than merely resemble it.
 */
static void
gameshell_update_rate(struct ToriRS_Pacer* pacer, uint64_t now_ms)
{
    int prev_ratio = pacer->ratio;
    int prev_del = pacer->del_ms;
    uint64_t oldest;

    assert(pacer);

    pacer->ratio = 300;
    /*
     * The value a frame that is BEHIND ends up waiting, because nothing below
     * reassigns it outside the on-budget branch.
     *
     * The reference hard-codes 1 here, and its `mindel` clamp at the bottom can
     * only ever raise that -- so a faithful transcription has no way to express
     * "do not wait at all", and 1 ms is exactly the floor that costs the Java
     * client 41 % of its frame on the XP box. Letting a `mindel_ms` of 0 reach
     * through to here is this pacer's one deliberate divergence, and it is the
     * arm that measured what the floor is worth (23.0 fps -> 43.4 fps).
     */
    pacer->del_ms = pacer->mindel_ms < 1 ? 0 : 1;

    oldest = pacer->otim[pacer->opos];
    if( oldest == 0 )
    {
        /* No sample yet at this slot: keep the previous estimate rather than
         * inventing one from a zero timestamp. */
        pacer->ratio = prev_ratio;
        pacer->del_ms = prev_del;
    }
    else if( now_ms > oldest )
    {
        /* Widen before multiplying, and clamp before narrowing: TORIRS_FRAME_MS
         * is caller-supplied, and a large period over a one-millisecond window
         * overflows both the multiply and the int it lands in. The clamp below
         * would then be reading a wrapped value. */
        uint64_t scaled = (uint64_t)pacer->period_ms * 2560u / (now_ms - oldest);
        pacer->ratio = scaled > 256 ? 257 : (int)scaled;
    }

    if( pacer->ratio < 25 )
        pacer->ratio = 25;
    if( pacer->ratio > 256 )
    {
        /* On or under budget: cap the catch-up and spend the slack waiting.
         * `oldest` is deliberately still the pre-update value here, exactly as
         * in the reference -- the ring is written below. */
        pacer->ratio = 256;
        pacer->del_ms = (int)((int64_t)pacer->period_ms - (int64_t)((now_ms - oldest) / 10));
    }
    if( pacer->del_ms > pacer->period_ms )
        pacer->del_ms = pacer->period_ms;

    pacer->otim[pacer->opos] = now_ms;
    pacer->opos = (pacer->opos + 1) % TORIRS_PACER_OTIM_COUNT;

    /*
     * Push the whole history forward by the wait we are about to take, so the
     * next estimate measures work and not work-plus-wait. A del of 1 or less is
     * not worth the pass, and a negative del -- which the branch above can
     * produce when the last ten iterations overran -- must never walk the ring
     * backwards; `> 1` covers both.
     */
    if( pacer->del_ms > 1 )
    {
        for( int i = 0; i < TORIRS_PACER_OTIM_COUNT; i++ )
            if( pacer->otim[i] != 0 )
                pacer->otim[i] += (uint64_t)pacer->del_ms;
    }

    if( pacer->del_ms < pacer->mindel_ms )
        pacer->del_ms = pacer->mindel_ms;
}

uint64_t
ToriRS_Pacer_BeginFrame(struct ToriRS_Pacer* pacer, uint64_t now_ms)
{
    int ticks = 0;

    assert(pacer);
    /* A pacer nobody called Init on is all-zero, which reads as a valid
     * GameShell pacer with a zero-millisecond budget -- a frame rate of
     * infinity and a logic clock that never advances. Catch it on frame one. */
    assert(pacer->period_ms > 0);

    if( pacer->kind == TORIRS_PACER_DEADLINE )
    {
        /* Logic follows the wall clock; App_RunOnce does the tick arithmetic. */
        pacer->last_logic_ticks = 0;
        return now_ms;
    }

    if( !pacer->seeded )
    {
        /* The reference fills the ring with `now` before entering the loop, so
         * the first iteration measures a zero-length window and takes the
         * on-budget branch. Seeding the logic clock to real time is ours: it is
         * what App_RunOnce would have started from anyway. */
        for( int i = 0; i < TORIRS_PACER_OTIM_COUNT; i++ )
            pacer->otim[i] = now_ms;
        pacer->logic_ms = now_ms;
        pacer->seeded = 1;
    }

    gameshell_update_rate(pacer, now_ms);

    /*
     * `ratio` is 256ths of a logic tick per iteration, so this runs one tick
     * for every 256 accumulated. At ratio 256 that is one tick per draw; at the
     * ratio 25 floor it is eleven, which is the reference's catch-up ceiling.
     */
    assert(pacer->ratio > 0);
    while( pacer->count < 256 )
    {
        pacer->count += pacer->ratio;
        ticks++;
    }
    pacer->count &= 0xFF;

    pacer->last_logic_ticks = ticks;
    pacer->logic_ms += (uint64_t)ticks * (uint64_t)pacer->period_ms;
    return pacer->logic_ms;
}

uint64_t
ToriRS_Pacer_WaitDeadline(
    struct ToriRS_Pacer const* pacer, uint64_t frame_start_ms, uint64_t now_ms)
{
    assert(pacer);
    assert(pacer->period_ms > 0);

    if( pacer->kind == TORIRS_PACER_DEADLINE )
        return frame_start_ms + (uint64_t)pacer->period_ms;

    /* A duration, not a deadline: the reference sleeps `del` regardless of how
     * long the iteration took, which is precisely why it cannot recover the
     * time an overrun cost it. */
    if( pacer->del_ms <= 0 )
        return now_ms;
    return now_ms + (uint64_t)pacer->del_ms;
}
