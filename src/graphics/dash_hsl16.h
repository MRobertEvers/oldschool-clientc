#ifndef DASH_HSL16_H
#define DASH_HSL16_H

#include <stdint.h>

typedef uint16_t hsl16_t;

/** Sentinels for channel C in lit face colors; lightness=127 is never produced by lighting. */
#define DASHHSL16_HIDDEN ((hsl16_t)0xFFFF)
#define DASHHSL16_FLAT ((hsl16_t)0xFF7F)

#endif
