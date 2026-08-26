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
    TORIRS_PACER_OTIM_COUNT = 10
};

struct ToriRS_Pacer
{
    enum ToriRS_PacerKind kind;
    int period_ms; /* GameShell's `deltime` */
    int mindel_ms; /* GameShell's `mindel`; the deadline pacer has no floor */

    /* GameShell state. Unused by TORIRS_PACER_DEADLINE. */
    uint64_t otim[TORIRS_PACER_OTIM_COUNT];
    int opos;
    int ratio;
    int del_ms;
    int count;
    uint64_t logic_ms;
    int seeded;

    int last_logic_ticks;
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

/* Logic ticks the last ToriRS_Pacer_BeginFrame asked for. */
int ToriRS_Pacer_LastLogicTicks(struct ToriRS_Pacer const* pacer);

#endif /* PACER_H */
