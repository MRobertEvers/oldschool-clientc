#ifndef TORIRS_FRAME_TIMES_H
#define TORIRS_FRAME_TIMES_H
#include <stdint.h>
/* Diagnostic only; main-loop work and completed EGL swap cadence. */
void ToriRS_FrameTimes_Begin(uint64_t start_us);
void ToriRS_FrameTimes_Present(uint64_t before_us, uint64_t after_us);
void ToriRS_FrameTimes_End(uint64_t end_us);
#endif
