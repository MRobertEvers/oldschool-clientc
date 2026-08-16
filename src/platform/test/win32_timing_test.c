#include "platform/platform_win32_timing.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SAMPLE_COUNT 64

static int
compare_u64(void const* a, void const* b)
{
    uint64_t left = *(uint64_t const*)a;
    uint64_t right = *(uint64_t const*)b;
    return left < right ? -1 : left > right;
}

int
main(void)
{
    uint64_t samples[SAMPLE_COUNT];
    uint64_t sorted[SAMPLE_COUNT];
    uint64_t previous;
    uint64_t total = 0;
    uint64_t median;
    uint64_t p95;

    if( !PlatformWin32Timing_Init() )
        return 1;

    previous = PlatformWin32Timing_NowMs();
    for( int i = 0; i < 10000; i++ )
    {
        uint64_t now = PlatformWin32Timing_NowMs();
        if( now < previous )
        {
            fputs("win32_timing_test: clock moved backwards\n", stderr);
            return 1;
        }
        previous = now;
    }

    for( int i = 0; i < SAMPLE_COUNT; i++ )
    {
        uint64_t start = PlatformWin32Timing_NowMs();
        PlatformWin32Timing_SleepUntilMs(start + 20);
        samples[i] = PlatformWin32Timing_NowMs() - start;
        sorted[i] = samples[i];
        total += samples[i];
    }
    qsort(sorted, SAMPLE_COUNT, sizeof(sorted[0]), compare_u64);
    median = sorted[SAMPLE_COUNT / 2];
    p95 = sorted[(SAMPLE_COUNT * 95) / 100];
    printf(
        "win32_timing_test: median=%llums p95=%llums mean=%.2fms\n",
        (unsigned long long)median,
        (unsigned long long)p95,
        (double)total / SAMPLE_COUNT);

    /* The broken default Windows quantum produced ~31.25 ms waits. Assert the
     * distribution rather than the mean: one unrelated scheduler stall must
     * not make this regression flaky, while repeated 25--31 ms waits must not
     * slip through just because some samples happened to be short. */
    if( median < 18 || median > 24 || p95 > 30 )
    {
        fputs("win32_timing_test: 20ms cadence is outside the guardrail\n", stderr);
        return 1;
    }

    PlatformWin32Timing_Shutdown();
    if( !PlatformWin32Timing_Init() )
        return 1;
    previous = PlatformWin32Timing_NowMs();
    PlatformWin32Timing_SleepUntilMs(previous + 2);
    if( PlatformWin32Timing_NowMs() < previous + 2 )
    {
        fputs("win32_timing_test: deadline wait returned early after reinit\n", stderr);
        return 1;
    }
    PlatformWin32Timing_Shutdown();
    puts("win32_timing_test: ok (QPC deadline + balanced timer lifecycle)");
    return 0;
}
