#ifndef DASH_VERTEXINT_H
#define DASH_VERTEXINT_H

#include <stdint.h>

typedef int16_t vertexint_t;

/** Used by dash.c to select projection implementation; keep in sync with vertexint_t width. */
#define VERTEXINT_BITS 16

#endif
