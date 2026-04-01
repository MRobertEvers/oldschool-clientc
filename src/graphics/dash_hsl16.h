#ifndef DASH_HSL16_H
#define DASH_HSL16_H

#include <stdint.h>

typedef uint16_t DashHSL16;

/** Sentinels for channel C in lit face colors; lightness=127 is never produced by lighting. */
#define DASHHSL16_HIDDEN ((DashHSL16)0xFFFF)
#define DASHHSL16_FLAT   ((DashHSL16)0xFF7F)

#endif
