#include "platform/platform_win32_timing.h"
#include "platform/sleep_plan.h"

#include <windows.h>
#include <mmsystem.h>
#include <intrin.h>

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Win32TimingState
{
    unsigned int references;
    int qpc_valid;
    LARGE_INTEGER qpc_frequency;

    int period_active;
    UINT period_ms;

    int fallback_seen;
    DWORD fallback_last;
    uint64_t fallback_epoch;

    int now_seen;
    uint64_t last_now_ms;

    int now_us_seen;
    uint64_t last_now_us;
};

static struct Win32TimingState timing;

bool
PlatformWin32Timing_Init(void)
{
    LARGE_INTEGER frequency;

    if( timing.references++ > 0 )
        return true;

    memset(&timing, 0, sizeof(timing));
    timing.references = 1;
    if( QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 )
    {
        timing.qpc_valid = 1;
        timing.qpc_frequency = frequency;
    }
    return true;
}

void
PlatformWin32Timing_Shutdown(void)
{
    if( timing.references == 0 || --timing.references > 0 )
        return;

    if( timing.period_active )
        timeEndPeriod(timing.period_ms);
    memset(&timing, 0, sizeof(timing));
}

static uint64_t
qpc_now_ms(void)
{
    LARGE_INTEGER counter;
    uint64_t whole;
    uint64_t remainder;
    uint64_t frequency = (uint64_t)timing.qpc_frequency.QuadPart;

    if( !QueryPerformanceCounter(&counter) || counter.QuadPart < 0 )
        return UINT64_MAX;

    /* Split before multiplying. A direct counter*1000 can overflow after a
     * long uptime even though the final millisecond result still fits. */
    whole = (uint64_t)counter.QuadPart / frequency;
    remainder = (uint64_t)counter.QuadPart % frequency;
    return whole * 1000u + remainder * 1000u / frequency;
}

static uint64_t
qpc_now_us(void)
{
    LARGE_INTEGER counter;
    uint64_t whole;
    uint64_t remainder;
    uint64_t frequency = (uint64_t)timing.qpc_frequency.QuadPart;

    if( !QueryPerformanceCounter(&counter) || counter.QuadPart < 0 )
        return UINT64_MAX;

    /* Split before multiplying, for the reason qpc_now_ms gives — with six more
     * digits of scale the direct form overflows that much sooner. */
    whole = (uint64_t)counter.QuadPart / frequency;
    remainder = (uint64_t)counter.QuadPart % frequency;
    return whole * 1000000u + remainder * 1000000u / frequency;
}

static uint64_t
fallback_now_ms(void)
{
    DWORD current = GetTickCount();

    /* GetTickCount is XP-safe but wraps every 49.7 days. Extend it to 64 bits
     * so a long-running client does not interpret the wrap as a huge frame. */
    if( timing.fallback_seen && current < timing.fallback_last )
        timing.fallback_epoch += UINT64_C(1) << 32;
    timing.fallback_seen = 1;
    timing.fallback_last = current;
    return timing.fallback_epoch + (uint64_t)current;
}

uint64_t
PlatformWin32Timing_NowMs(void)
{
    uint64_t now;

    if( timing.references == 0 )
        PlatformWin32Timing_Init();
    if( timing.qpc_valid )
    {
        now = qpc_now_ms();
        if( now == UINT64_MAX )
            timing.qpc_valid = 0;
    }
    if( !timing.qpc_valid )
        now = fallback_now_ms();

    /* Some XP-era HAL/TSC combinations could expose a backwards QPC sample
     * after CPU migration. This clock is consumed by the single UI thread, so
     * clamping here is sufficient to preserve the deadline contract. */
    if( timing.now_seen && now < timing.last_now_ms )
        return timing.last_now_ms;
    timing.now_seen = 1;
    timing.last_now_ms = now;
    return now;
}

uint64_t
PlatformWin32Timing_NowUs(void)
{
    uint64_t now;

    if( timing.references == 0 )
        PlatformWin32Timing_Init();
    if( timing.qpc_valid )
    {
        now = qpc_now_us();
        if( now == UINT64_MAX )
            timing.qpc_valid = 0;
    }
    /* GetTickCount is the XP fallback and has nothing finer to offer; scaling
     * it keeps the unit contract, not the resolution. */
    if( !timing.qpc_valid )
        now = fallback_now_ms() * 1000u;

    if( timing.now_us_seen && now < timing.last_now_us )
        return timing.last_now_us;
    timing.now_us_seen = 1;
    timing.last_now_us = now;
    return now;
}

static void
request_fine_timer_period(void)
{
    TIMECAPS caps;
    UINT period;

    if( timing.period_active )
        return;

    period = 1;
    if( timeGetDevCaps(&caps, sizeof(caps)) == TIMERR_NOERROR )
    {
        period = caps.wPeriodMin;
        if( period < 1 )
            period = 1;
        if( period > caps.wPeriodMax )
            period = caps.wPeriodMax;
    }
    if( timeBeginPeriod(period) == TIMERR_NOERROR )
    {
        timing.period_active = 1;
        timing.period_ms = period;
    }
}

/*
 * The high-resolution waitable timer (platform/sleep_plan.h): resolved at run
 * time, because CreateWaitableTimerExW is Vista and later and its
 * high-resolution flag Windows 10 1803 and later -- an older system refuses
 * the flag and the lane keeps the Sleep plan. One timer, made on the first
 * sleep and kept for the process (the loop sleeps every frame).
 */
typedef HANDLE(WINAPI* Win32CreateWaitableTimerExWFn)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
#define WIN32_TIMING_TIMER_HIGH_RESOLUTION 0x00000002u
#define WIN32_TIMING_TIMER_ALL_ACCESS 0x001F0003u

static struct
{
    int resolved;
    HANDLE timer;
} precise;

static void
precise_timer_resolve(void)
{
    HMODULE kernel;
    Win32CreateWaitableTimerExWFn create;
    char const* knob;

    precise.resolved = 1;
    /* TORIRS_WIN32_PRECISE_TIMER=0 keeps the Sleep plan, the one XP and
     * Windows 10 before 1803 take, so it can be measured on a newer system;
     * any value set says on stderr which plan this run sleeps on. */
    knob = getenv("TORIRS_WIN32_PRECISE_TIMER");
    if( !(knob && knob[0] == '0') )
    {
        kernel = GetModuleHandleA("kernel32.dll");
        create = kernel ? (Win32CreateWaitableTimerExWFn)(void (*)(void))GetProcAddress(kernel, "CreateWaitableTimerExW")
                        : NULL;
        if( create )
            precise.timer = create(NULL, NULL, WIN32_TIMING_TIMER_HIGH_RESOLUTION, WIN32_TIMING_TIMER_ALL_ACCESS);
    }
    if( knob )
        fprintf(stderr, "win32 timing: %s (TORIRS_WIN32_PRECISE_TIMER=%s)\n",
                precise.timer ? "naps and frame waits block on the high-resolution waitable timer"
                              : "the Sleep plan: Sleep(ms - 1), then Sleep(0) rechecks for the last millisecond",
                knob);
}

/* Wait `us` microseconds on the precise timer. False if the wait could not
 * be armed (the caller falls back to the Sleep plan for good). */
static int
precise_timer_wait(uint64_t us)
{
    LARGE_INTEGER due;

    /* Negative: relative, in 100 ns units. */
    due.QuadPart = -(LONGLONG)(us * 10u);
    if( !SetWaitableTimer(precise.timer, &due, 0, NULL, NULL, FALSE) )
        return 0;
    return WaitForSingleObject(precise.timer, INFINITE) == WAIT_OBJECT_0;
}

void
PlatformWin32Timing_SleepUntilMs(uint64_t deadline_ms)
{
    if( timing.references == 0 )
        PlatformWin32Timing_Init();
    request_fine_timer_period();
    if( !precise.resolved )
        precise_timer_resolve();
    for( ;; )
    {
        struct ToriRS_SleepStep const step =
            ToriRS_SleepPlan_Next(PlatformWin32Timing_NowUs(), deadline_ms, precise.timer != NULL);

        if( step.kind == TORIRS_SLEEP_STEP_DONE )
            return;
        if( step.kind == TORIRS_SLEEP_STEP_TIMER_US )
        {
            if( precise_timer_wait(step.amount) )
                continue;
            CloseHandle(precise.timer);
            precise.timer = NULL;
            continue;
        }
        /* Sleep may return early. Re-read the monotonic clock rather than
         * letting a short wait make the client run above its 50 fps cap. */
        Sleep(step.kind == TORIRS_SLEEP_STEP_SLEEP_MS
                  ? (step.amount >= (uint64_t)INFINITE ? INFINITE - 1u : (DWORD)step.amount)
                  : 0u);
    }
}

/* --- the thread's processor time (PlatformWindow_ThreadCpuUs) -------------
 *
 * QueryThreadCycleTime is exact (no 15.6 ms scheduler granularity, which
 * GetThreadTimes has and which would make a 4 ms check all-or-nothing), but
 * it counts cycles. On every x86 Windows that has it, those are
 * time-stamp-counter ticks, so the counter's rate converts them: measured
 * once, as rdtsc against QueryPerformanceCounter over a 10 ms Sleep (both
 * clocks run while the thread sleeps, so the ratio is the counter's rate and
 * the sleep's own jitter does not matter). Resolved through GetProcAddress so
 * the XP lane, which has no such call, still loads; there, and if the rate
 * cannot be measured, the wall clock stands in. */
typedef BOOL(WINAPI* Win32QueryThreadCycleTimeFn)(HANDLE, PULONG64);

static struct
{
    int resolved;
    Win32QueryThreadCycleTimeFn query;
    double cycles_per_us;
} cpu_clock;

static void
cpu_clock_resolve(void)
{
    HMODULE kernel;

    cpu_clock.resolved = 1;
    if( timing.references == 0 )
        PlatformWin32Timing_Init();
    if( !timing.qpc_valid )
        return;
    kernel = GetModuleHandleA("kernel32.dll");
    if( !kernel )
        return;
    cpu_clock.query = (Win32QueryThreadCycleTimeFn)(void (*)(void))GetProcAddress(kernel, "QueryThreadCycleTime");
    if( !cpu_clock.query )
        return;
    {
        LARGE_INTEGER q0;
        LARGE_INTEGER q1;
        unsigned long long t0;
        unsigned long long t1;
        double us;

        QueryPerformanceCounter(&q0);
        t0 = __rdtsc();
        Sleep(10);
        QueryPerformanceCounter(&q1);
        t1 = __rdtsc();
        us = (double)(q1.QuadPart - q0.QuadPart) * 1000000.0 / (double)timing.qpc_frequency.QuadPart;
        if( us > 0.0 && t1 > t0 )
            cpu_clock.cycles_per_us = (double)(t1 - t0) / us;
        else
            cpu_clock.query = NULL;
    }
}

int
PlatformWin32Timing_ThreadCpuIsWall(void)
{
    if( !cpu_clock.resolved )
        cpu_clock_resolve();
    return cpu_clock.query == NULL;
}

uint64_t
PlatformWin32Timing_ThreadCpuUs(void)
{
    ULONG64 cycles = 0;
    BOOL ok;

    if( !cpu_clock.resolved )
        cpu_clock_resolve();
    if( !cpu_clock.query )
        return PlatformWin32Timing_NowUs();
    /* The current thread's pseudo-handle always has the access it needs;
     * a failure here would mix two clocks in one difference. */
    ok = cpu_clock.query(GetCurrentThread(), &cycles);
    assert(ok);
    (void)ok;
    return (uint64_t)((double)cycles / cpu_clock.cycles_per_us);
}
