#ifndef SRC_PLATFORM_PLATFORM_WIN32_TIMING_H
#define SRC_PLATFORM_PLATFORM_WIN32_TIMING_H

#include <stdbool.h>
#include <stdint.h>

/* XP-safe high-resolution clock and frame wait used by both Windows lanes. */
bool
PlatformWin32Timing_Init(void);

void
PlatformWin32Timing_Shutdown(void);

uint64_t
PlatformWin32Timing_NowMs(void);

/** The same clock in microseconds, for measuring a frame rather than pacing it. */
uint64_t
PlatformWin32Timing_NowUs(void);

void
PlatformWin32Timing_SleepUntilMs(uint64_t deadline_ms);

/* The calling thread's processor time, microseconds (PlatformWindow_ThreadCpuUs):
 * QueryThreadCycleTime, which counts in time-stamp-counter ticks, scaled by
 * the counter's rate measured once against QueryPerformanceCounter. Where
 * QueryThreadCycleTime does not exist (before Vista: the XP lane) or the
 * counter cannot be read, it is the wall clock, and _ThreadCpuIsWall says 1. */
uint64_t
PlatformWin32Timing_ThreadCpuUs(void);

int
PlatformWin32Timing_ThreadCpuIsWall(void);

#endif
