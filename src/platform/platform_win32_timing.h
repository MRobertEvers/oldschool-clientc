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

#endif
