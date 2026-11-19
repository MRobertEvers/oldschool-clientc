#include "perf/frame_clock.h"

#include <assert.h>
#include <string.h>

void
FrameClock_Reset(struct FrameClock* clock)
{
    assert(clock);
    memset(clock, 0, sizeof(*clock));
}

void
FrameClockStep_Skip(
    struct FrameClockStep* step,
    uint64_t now_ms)
{
    assert(step);
    step->last_ms = now_ms;
}

int
FrameClockStep_Pay(
    struct FrameClockStep* step,
    uint64_t now_ms,
    int tick_ms,
    int max_ticks)
{
    uint64_t elapsed_ms;
    int ticks = 0;

    assert(step);
    assert(tick_ms > 0);
    assert(max_ticks > 0);

    if( step->last_ms == 0 )
        step->last_ms = now_ms;
    elapsed_ms = now_ms > step->last_ms ? now_ms - step->last_ms : 0;
    step->last_ms = now_ms;
    /* One clamp, shared: a stall costs the movers and the cycles the same. */
    if( elapsed_ms > (uint64_t)max_ticks * tick_ms )
    {
        elapsed_ms = (uint64_t)max_ticks * tick_ms;
        /* Dropped rather than owed: catching up a long stall at full speed
         * is the lurch the bound exists to prevent. */
        step->accum_ms = 0.0;
    }
    step->accum_ms += (double)elapsed_ms;
    while( step->accum_ms >= (double)tick_ms && ticks < max_ticks )
    {
        step->accum_ms -= (double)tick_ms;
        ticks++;
    }
    step->frame_ms = elapsed_ms;
    return ticks;
}

int
FrameClockStep_PayLocked(
    struct FrameClockStep* step,
    int ticks,
    int tick_ms)
{
    assert(step);
    assert(tick_ms > 0);
    ticks += step->cycles_owed;
    step->cycles_owed = 0;
    step->accum_ms = 0.0;
    step->frame_ms = (uint64_t)ticks * tick_ms;
    return ticks;
}

void
FrameClockStep_Owe(
    struct FrameClockStep* step,
    int cycles)
{
    assert(step);
    step->cycles_owed = cycles;
}

void
FrameClockStep_Refund(
    struct FrameClockStep* step,
    int cycles,
    int tick_ms)
{
    assert(step);
    assert(tick_ms > 0);
    step->accum_ms += (double)cycles * (double)tick_ms;
}

uint64_t
FrameClockStep_FrameMs(struct FrameClockStep const* step)
{
    assert(step);
    return step->frame_ms;
}

void
FrameClockFps_NoteDrawn(
    struct FrameClockFps* fps,
    uint64_t now_ms)
{
    assert(fps);
    fps->drawn++;
    fps->window_frames++;
    if( now_ms - fps->window_start_ms >= 1000 )
    {
        fps->fps = fps->window_frames;
        fps->window_frames = 0;
        fps->window_start_ms = now_ms;
    }
}

int
FrameClockFps_Fps(struct FrameClockFps const* fps)
{
    assert(fps);
    return fps->fps;
}

int
FrameClockIdle_Note(
    struct FrameClockIdle* idle,
    int had_input)
{
    assert(idle);
    if( had_input )
    {
        idle->frames = 0;
        idle->sent = 0;
        return 0;
    }
    if( ++idle->frames > FRAME_CLOCK_IDLE_TIMER_FRAMES && !idle->sent )
    {
        idle->sent = 1;
        return 1;
    }
    return 0;
}

void
FrameClockSerial_NoteLogicCycle(struct FrameClockSerial* serial)
{
    assert(serial);
    serial->logic_cycle++;
}

void
FrameClockSerial_NoteRunOnce(struct FrameClockSerial* serial)
{
    assert(serial);
    serial->run_once++;
}

void
FrameClockSerial_NoteFrameAt(
    struct FrameClockSerial* serial,
    uint64_t now_ms)
{
    assert(serial);
    serial->last_frame_ms = now_ms;
}
