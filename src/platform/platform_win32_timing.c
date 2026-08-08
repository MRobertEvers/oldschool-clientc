#include "platform/platform_win32_timing.h"

#include <windows.h>
#include <mmsystem.h>

#include <limits.h>
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

void
PlatformWin32Timing_SleepUntilMs(uint64_t deadline_ms)
{
    if( timing.references == 0 )
        PlatformWin32Timing_Init();
    request_fine_timer_period();
    for( ;; )
    {
        uint64_t now = PlatformWin32Timing_NowMs();
        uint64_t remaining;
        DWORD delay;

        if( now >= deadline_ms )
            return;
        remaining = deadline_ms - now;
        /* Leave the final millisecond to a zero-duration yield/recheck. Even
         * with timeBeginPeriod(1), Sleep(N) commonly returns around N+1 ms;
         * sleeping the full residual turns a 50 fps deadline into ~46 fps. */
        if( remaining > 1 )
        {
            remaining--;
            delay = remaining >= (uint64_t)INFINITE ? INFINITE - 1u : (DWORD)remaining;
        }
        else
        {
            delay = 0;
        }
        Sleep(delay);
        /* Sleep may return early. Re-read the monotonic clock rather than
         * letting a short wait make the client run above its 50 fps cap. */
    }
}
