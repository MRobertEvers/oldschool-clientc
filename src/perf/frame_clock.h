#ifndef SRC_PERF_FRAME_CLOCK_H
#define SRC_PERF_FRAME_CLOCK_H

/*
 * Five things the frame loop counts off the wall clock or off its own
 * frames, each its own struct because each is its own decision:
 *
 * The step: how many fixed logic cycles a frame runs. The frame's elapsed
 * time goes into an accumulator; whole cycles are paid out of it and the
 * remainder is carried, so the rate is exactly one cycle per cycle length
 * however fast or slow frames arrive. A stall is clamped to the catch-up
 * bound and what it lost is dropped rather than owed. The clamped elapsed
 * time is kept as the frame's length: the movers interpolate it, the logic
 * steps whole cycles out of it, and the ink and afterimage advance by it. A
 * frame-locked run pays a set number of cycles instead and empties the
 * accumulator; cycles paid and not run are handed back -- to the accumulator,
 * or, on a frame-locked run, owed to the next frame-locked frame, which pays
 * them on top of its own.
 *
 * The serials: logic cycles run since boot (the reference loopCycle every
 * blink, rotation and debounce reads), frames run since boot (what "this
 * frame" means to the pacing), and the time of the last frame that reached
 * the link watch. Each only counts up or is set by its one writer.
 *
 * The fps window: frames drawn in the last whole second, the figure the ping
 * reply reports. The count closes on the first drawn frame a second or more
 * after the window opened. Beside it, every frame drawn since boot: what a
 * frame-rate readout differences.
 *
 * The idle timer: frames since the last input. It says once when the count
 * passes FRAME_CLOCK_IDLE_TIMER_FRAMES, and not again until input has reset
 * it.
 *
 * The frame times: the last few frames' work, in microseconds, and their
 * mean (perf/frame_time_ring.h). Written every frame whether or not anything
 * shows it: the newest is the last frame's work, and the mean is what the
 * frame-time panel reads, already settled the frame it is shown.
 *
 * Nothing here reads a clock or input, runs a cycle or sends a packet. The
 * caller passes the time in milliseconds, the cycle length and the catch-up
 * bound, says whether the frame had input, runs the cycles it is paid and
 * notes each one it ran, says when a frame starts, and sends the idle timer
 * when told to.
 */

#include "perf/frame_time_ring.h"

#include <stdint.h>

/** Frames of no input before the idle timer is due: about 90 seconds at
 *  the 50 Hz the loop runs at. */
#define FRAME_CLOCK_IDLE_TIMER_FRAMES 4500

struct FrameClockStep
{
    /** The time the last frame was paid at; 0 before the first. */
    uint64_t last_ms;
    /** Unspent time, carried between frames. */
    double accum_ms;
    /** The clamped elapsed time of the frame just paid. */
    uint64_t frame_ms;
    /** Cycles a frame-locked frame was paid and did not run, paid by the
     *  next frame-locked frame. Always 0 when each frame pays one cycle. */
    int cycles_owed;
};

struct FrameClockSerial
{
    /** Logic cycles run since boot (reference loopCycle). */
    uint64_t logic_cycle;
    /** Frames run since boot: what "this frame" means to the pacing. */
    uint64_t run_once;
    /** Wall clock at the last frame that reached the link watch; 0 before
     *  the first. */
    uint64_t last_frame_ms;
};

struct FrameClockFps
{
    /** Frames drawn in the last closed window. */
    int fps;
    /** Frames drawn since the open window started. */
    int window_frames;
    uint64_t window_start_ms;
    /** Frames drawn since boot. */
    uint64_t drawn;
};

struct FrameClockIdle
{
    /** Frames since the last input. */
    long frames;
    /** 1: the timer was due since the last input, and said so. */
    int sent;
};

struct FrameClock
{
    struct FrameClockStep step;
    struct FrameClockSerial serial;
    struct FrameClockFps fps;
    struct FrameClockIdle idle;
    struct FrameTimeRing frame_times;
};

/** Nothing paid, nothing counted, no input missed. */
void
FrameClock_Reset(struct FrameClock* clock);

/** A frame that runs no logic (the loading screen): its time is not owed to
 *  the next frame that does. */
void
FrameClockStep_Skip(
    struct FrameClockStep* step,
    uint64_t now_ms);

/**
 * Pay this frame: the time since the last paid frame (none on the first),
 * clamped to `max_ticks` cycles, goes into the accumulator, which pays out
 * whole cycles of `tick_ms`, at most `max_ticks`. A clamped stall empties the
 * accumulator first. Returns the cycles paid; the clamped elapsed time is the
 * frame's length (FrameClockStep_FrameMs).
 */
int
FrameClockStep_Pay(
    struct FrameClockStep* step,
    uint64_t now_ms,
    int tick_ms,
    int max_ticks);

/** A frame-locked frame runs `ticks` cycles whatever the wall clock says,
 *  plus any cycles owed by the last one: the debt is cleared, the
 *  accumulator is emptied and the frame is that many cycles long. Returns
 *  the cycles paid. */
int
FrameClockStep_PayLocked(
    struct FrameClockStep* step,
    int ticks,
    int tick_ms);

/** A frame-locked frame was paid `cycles` more than it ran: the next
 *  frame-locked frame pays them (FrameClockStep_PayLocked). Replaces any
 *  debt not yet paid. */
void
FrameClockStep_Owe(
    struct FrameClockStep* step,
    int cycles);

/** Cycles owed to the next frame-locked frame. */
static inline int
FrameClockStep_CyclesOwed(struct FrameClockStep const* step)
{
    return step->cycles_owed;
}

/** `cycles` paid out and not run: back into the accumulator. */
void
FrameClockStep_Refund(
    struct FrameClockStep* step,
    int cycles,
    int tick_ms);

/** The length of the frame just paid, in milliseconds. */
uint64_t
FrameClockStep_FrameMs(struct FrameClockStep const* step);

/** A frame was drawn at `now_ms`: count it, since boot and in the window,
 *  and close the window when it is a second or more old. */
void
FrameClockFps_NoteDrawn(
    struct FrameClockFps* fps,
    uint64_t now_ms);

/** Frames drawn in the last closed window; 0 before the first closes. */
int
FrameClockFps_Fps(struct FrameClockFps const* fps);

/** Frames drawn since boot. */
static inline uint64_t
FrameClockFps_Drawn(struct FrameClockFps const* fps)
{
    return fps->drawn;
}

/** A logic cycle is about to run: the logic cycle counts up by one. */
void
FrameClockSerial_NoteLogicCycle(struct FrameClockSerial* serial);

/** A frame starts: the frame serial counts up by one. */
void
FrameClockSerial_NoteRunOnce(struct FrameClockSerial* serial);

/** The frame at `now_ms` reached the link watch. */
void
FrameClockSerial_NoteFrameAt(
    struct FrameClockSerial* serial,
    uint64_t now_ms);

/** Logic cycles run since boot; 0 before the first. */
static inline uint64_t
FrameClockSerial_LogicCycle(struct FrameClockSerial const* serial)
{
    return serial->logic_cycle;
}

/** Frames run since boot. */
static inline uint64_t
FrameClockSerial_RunOnce(struct FrameClockSerial const* serial)
{
    return serial->run_once;
}

/** Wall clock of the last frame that reached the link watch; 0 before the
 *  first. */
static inline uint64_t
FrameClockSerial_LastFrameMs(struct FrameClockSerial const* serial)
{
    return serial->last_frame_ms;
}

/**
 * One frame: `had_input` resets the count. Returns 1 on the one frame the
 * idle timer is due -- the count past FRAME_CLOCK_IDLE_TIMER_FRAMES and not
 * yet said since the last input -- and 0 on every other.
 */
int
FrameClockIdle_Note(
    struct FrameClockIdle* idle,
    int had_input);

#endif /* SRC_PERF_FRAME_CLOCK_H */
