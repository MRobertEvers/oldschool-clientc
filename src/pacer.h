#ifndef PACER_H
#define PACER_H

#include <stdint.h>

/*
 * The two ways this client can pace a frame, behind one interface.
 *
 * Both answer the same two questions once per loop iteration -- how many logic
 * ticks run before the draw, and how long the iteration waits afterwards -- and
 * they answer them differently enough that keeping both on the same build is
 * the only way to compare them.
 */
enum ToriRS_PacerKind
{
    /*
     * Jagex `GameShell.run()`, transcribed. A ten-iteration ring estimates the
     * achieved rate; that estimate (`ratio`) sets how many logic ticks run per
     * draw; and the wait is a DURATION with a floor of `mindel`.
     *
     * The floor is the part worth knowing about. On the Windows XP target the
     * Java client asks for its 1 ms floor on 100 % of in-world frames and the
     * OS charges it ~16 ms, because nothing there holds the timer period down
     * and the wait rounds up to a 15.625 ms clock tick -- 41 % of its frame,
     * measured 23.0 fps against 43.4 fps with the floor removed and the raster
     * work untouched. We do not inherit that even on this pacer, because
     * PlatformWin32Timing_SleepUntilMs requests timeBeginPeriod(1) itself: the
     * shape is faithful, the 16 ms is not.
     */
    TORIRS_PACER_GAMESHELL = 0,
    /*
     * Ours. Logic ticks come from the wall clock, the wait is to an ABSOLUTE
     * deadline, and a deadline already past costs nothing. Rounding the tick
     * count to nearest rather than flooring is what keeps a 19.6 ms frame from
     * beating against the 20 ms tick; see the comment in App_RunOnce.
     */
    TORIRS_PACER_DEADLINE = 1
};

enum
{
    /* GameShell's ring length. Its rate estimate therefore lags ten frames. */
    TORIRS_PACER_OTIM_COUNT = 10,

    /*
     * The draw budget to fall back to when the base one cannot be met: 33 ms,
     * or ~30 fps. Chosen because it is the rate the reference client settles
     * at on hardware like this, and because it leaves real slack -- a 24 ms
     * frame sleeps 9 ms of every 33 instead of nothing at all.
     */
    TORIRS_PACER_FALLBACK_PERIOD_MS = 33,

    /*
     * Consecutive frames agreeing before the budget moves. Long enough that a
     * single expensive frame -- a region load, a model bake -- does not drop
     * the client to 30 fps for the rest of the session, short enough that
     * walking into a heavy scene is answered within a second.
     */
    TORIRS_PACER_ADAPT_FRAMES = 40
};

struct ToriRS_Pacer
{
    enum ToriRS_PacerKind kind;
    int period_ms; /* GameShell's `deltime` */
    int mindel_ms; /* GameShell's `mindel`; the deadline pacer has no floor */

    /*
     * The budget the SCREEN is paced to. Equal to period_ms until the machine
     * proves it cannot hold that, then TORIRS_PACER_FALLBACK_PERIOD_MS.
     *
     * Separate from period_ms because the simulation must not follow it. A
     * client drawing at 30 fps still ticks the world at 50 Hz; the two rates
     * are independent and conflating them would slow the game down instead of
     * the picture.
     */
    int draw_period_ms;
    /*
     * The CONFIGURED screen cap, in ms per drawn frame; 0 is none. Where the
     * adaptive budget above is what the machine can do, this is what the
     * profile or the player asked for -- revconfig's `[frame] cap_fps`, or the
     * cache's own Limit Framerate row when that profile hands the cap to CS2.
     * One number feeds both: ToriRS_Pacer_DrawPeriodMs answers the longer of
     * the two, and every draw decision reads that. A second cap path beside
     * the pacer is how the screen ended up gated at 15 fps while the loop,
     * the pacer trace and the FPS readout all said 50.
     */
    int cap_period_ms;
    /* TORIRS_PACER_ADAPT=0 pins the draw budget to period_ms. */
    int adapt;
    int adapt_behind; /* consecutive frames that did not fit */
    int adapt_ahead;  /* consecutive frames with room to spare */

    /* GameShell state. Unused by TORIRS_PACER_DEADLINE. */
    uint64_t otim[TORIRS_PACER_OTIM_COUNT];
    int opos;
    int ratio;
    int del_ms;
    int count;
    uint64_t logic_ms;
    int seeded;

    int last_logic_ticks;

    /* ToriRS_Pacer_LockedDeadline's schedule: when the last frame was due to
     * end. 0 before the first locked frame. */
    uint64_t locked_due_ms;

    /*
     * TORIRS_PACER_TRACE=1. Answers the one question fps cannot: WHICH REGIME
     * the pacer is in. A frame that fits its budget and a frame that is behind
     * but waiting only `mindel` produce nearly the same frame rate, and only
     * one of them is the shape that collapses the Java client.
     */
    int trace;
    uint64_t trace_start_us;
    uint64_t trace_prev_us;
    uint64_t trace_wait_us;
    uint64_t trace_period_us;
    uint64_t trace_del_us;
    uint64_t trace_ratio_sum;
    uint64_t trace_frames;
    uint64_t trace_at_mindel;
};

/*
 * `period_ms` is the draw budget (20 for 50 fps). `mindel_ms` is the wait floor
 * and is GameShell-only; 0 reproduces the "no floor" arm.
 */
void ToriRS_Pacer_Init(
    struct ToriRS_Pacer* pacer, enum ToriRS_PacerKind kind, int period_ms, int mindel_ms);

/*
 * Name <-> kind, for `--pacer` and TORIRS_PACER. `*ok` is 0 on an unknown name,
 * and the returned kind is then meaningless -- the caller decides whether an
 * unknown name is a usage error or a fallback, because only the caller knows
 * whether it came from a flag or the environment.
 */
enum ToriRS_PacerKind ToriRS_Pacer_KindFromName(char const* name, int* ok);
char const* ToriRS_Pacer_KindName(enum ToriRS_PacerKind kind);

/*
 * Top of an iteration, before any frame work. Returns the clock to hand to
 * App_RunOnce, which derives its logic tick count from it.
 *
 * The deadline pacer returns `now_ms` untouched -- logic follows the wall clock
 * directly. The GameShell pacer returns its own clock, advanced by exactly the
 * number of ticks `ratio` says this iteration owes, so App_RunOnce runs that
 * many. Only App_RunOnce may be given this clock; everything else in the frame
 * wants real time.
 */
uint64_t ToriRS_Pacer_BeginFrame(struct ToriRS_Pacer* pacer, uint64_t now_ms);

/*
 * End of an iteration. Returns the absolute millisecond to wait until; a value
 * at or before `now_ms` means "do not wait".
 *
 * GameShell sleeps at the TOP of its loop and we wait at the bottom. The cycle
 * is the same either way, and the rate estimate is sampled at the same point
 * -- the top of the iteration -- in both.
 */
uint64_t ToriRS_Pacer_WaitDeadline(
    struct ToriRS_Pacer const* pacer, uint64_t frame_start_ms, uint64_t now_ms);

/*
 * End of an iteration of a FRAME-LOCKED client (App_FrameLocked: every frame
 * pays one logic cycle, whatever either pacer above asks for). Returns the
 * absolute millisecond to wait until.
 *
 * Frames are due on a fixed schedule, period_ms apart, so a frame's world time
 * keeps step with the wall: a sleep that overshoots or a frame a little over
 * budget is made up by the next frames not waiting, and no frame ever runs
 * ahead of its slot. A frame more than a whole period past its slot -- a
 * party boundary blocked on a member, a load -- re-anchors the schedule at
 * now instead of racing to make the time up, so two lock-stepped clients
 * settle on one phase rather than one stalling on the other every tick.
 */
uint64_t ToriRS_Pacer_LockedDeadline(struct ToriRS_Pacer* pacer, uint64_t now_ms);

/* Logic ticks the last ToriRS_Pacer_BeginFrame asked for. */
int ToriRS_Pacer_LastLogicTicks(struct ToriRS_Pacer const* pacer);

/*
 * The budget the SCREEN is currently paced to, in ms. Equal to period_ms
 * unless the machine could not hold that rate; see draw_period_ms.
 */
int ToriRS_Pacer_DrawPeriodMs(struct ToriRS_Pacer const* pacer);

/**
 * State the configured screen cap: `fps` frames drawn per second at most, 0
 * for none. Idempotent and cheap, so the loop restates it every frame from
 * whatever owns the number (App_FrameCapFps). The world's tick rate is not
 * touched: a capped screen still simulates at period_ms.
 */
void ToriRS_Pacer_SetCapFps(struct ToriRS_Pacer* pacer, int fps);

/*
 * Close one iteration for the trace: `now_us` is the clock after the wait, and
 * `wait_us` is what the wait actually cost. No-op unless TORIRS_PACER_TRACE=1.
 *
 * `wait_us` is measured, not assumed. The whole Java finding was a requested
 * sleep costing sixteen times what it asked for, and a trace that prints the
 * request back to itself would have missed it.
 */
void ToriRS_Pacer_NoteFrame(struct ToriRS_Pacer* pacer, uint64_t now_us, uint64_t wait_us);

/*
 * What a native frame loop does between one App_RunOnce and the next
 * (main.c frame_loop_step): the loop's half of the frame contract (app.h,
 * beside the runners). Every lane that runs the shared loop -- macos, linux,
 * win32, win64, android -- decides here; the browser has no wait of its own
 * (its pacing is the event loop's).
 *
 *   NONE   straight into the next frame. A mode with no cap to keep
 *          (--uncapped, a replay, a content test), or work queued that can
 *          run now (the boot asked for its next frame): sleeping through it
 *          would pace the work instead of the screen.
 *   SLEEP  the ordinary wait: the background queue is given the slack before
 *          the deadline (App_RunBackground), then the loop sleeps out what
 *          is left.
 *   NAP    work is queued that only WAITS -- a read the platform has not
 *          answered, a join on one, another queue's state -- or the main
 *          queue's head waits on a read (a packet). Nothing can move
 *          it but the answer, so going straight on would spin the parallel
 *          queue against reads still on the wire; sleeping out the whole wait
 *          would leave a landed answer untaken until the next frame. The
 *          background queue is given the slack, then the wait is slept in
 *          naps of TORIRS_LOOP_NAP_MS with a pump between them (App_PumpAsync,
 *          what the browser's torirs_web_io_pump does when a batch lands),
 *          and the wait ends the moment a pump moves anything.
 *
 * Every frame, whichever it is, has already given the background queue its
 * floor slice inside App_RunOnce, so a mode with no wait still feeds it.
 */
enum ToriRS_LoopWait
{
    TORIRS_LOOP_WAIT_NONE = 0,
    TORIRS_LOOP_WAIT_SLEEP,
    TORIRS_LOOP_WAIT_NAP,
};

/* One nap of a NAP wait, in ms: the most a landed answer waits to be taken
 * up between frames. The pre-loop JS5 and launch waits in main.c nap the
 * same 1 ms. */
#define TORIRS_LOOP_NAP_MS 1u

struct ToriRS_LoopWaitInput
{
    /** --uncapped: no artificial wait at all. */
    int uncapped;
    /** An RSProx or command-bus replay, which paces itself. */
    int replay;
    /** A content test (ContentTest_Enabled), which runs as fast as it can. */
    int content_test;
    /** The last pump left work queued (App_AsyncPending). */
    int async_pending;
    /** ...some of which can run at once (App_AsyncRunnable). */
    int async_runnable;
    /** The main queue's head waits on a read (App_MainQueueWaitsOnRead): a
     *  packet the next frame's pump resumes the moment its answer lands. */
    int main_waits_on_read;
};

enum ToriRS_LoopWait ToriRS_Pacer_LoopWait(struct ToriRS_LoopWaitInput const* in);
char const* ToriRS_Pacer_LoopWaitName(enum ToriRS_LoopWait wait);

/*
 * THE DISPLAY'S PERIOD, where the platform paces the loop to it and does not
 * say what it is: the browser's requestAnimationFrame. The period between two
 * callbacks is one vsync (16.7 ms at 60 Hz, 8.3 ms at 120 Hz) or a whole
 * number of them when a frame overran, so the shortest period of a window of
 * frames is the display's own. A window rather than all time, so one flukey
 * pair of callbacks does not shrink the estimate for the rest of the session
 * and a display that changes rate (a laptop dropping to 60 Hz on battery) is
 * followed within a window. 0 until the first window has closed.
 */
enum
{
    TORIRS_DISPLAY_PERIOD_WINDOW = 32,
    /* Shorter than any display this client meets (240 Hz is 4.2 ms): a period
     * under it is two callbacks the browser ran back to back, not a vsync. */
    TORIRS_DISPLAY_PERIOD_MIN_US = 4000,
};

struct ToriRS_DisplayPeriod
{
    uint64_t estimate_us;
    uint64_t window_min_us;
    int window_count;
};

/* One period between two display-paced frame starts. */
void ToriRS_DisplayPeriod_Note(struct ToriRS_DisplayPeriod* display, uint64_t period_us);

/*
 * Where a loop the platform paces (the browser's) may give the background
 * queue its slack until, or 0 when there is no slack left this frame.
 *
 * The slack is what is left before the next frame is due: the draw period
 * from the frame's start, and, when the loop is paced to a display whose
 * period is known (`display_period_us` > 0), no later than that period's
 * start less a quarter of it -- the browser composites after the callback
 * returns, and a callback still running at the next vsync costs the next
 * frame (at 60 Hz a 20 ms draw period runs 3 ms past it, every frame). A frame
 * that has overrun has none: 0, and the background queue has had its floor in
 * App_RunOnce, which steps every task once whatever the clock says.
 */
uint64_t ToriRS_Pacer_SlackEndUs(
    uint64_t frame_start_us, uint64_t draw_period_us, uint64_t display_period_us, uint64_t now_us);

#endif /* PACER_H */
