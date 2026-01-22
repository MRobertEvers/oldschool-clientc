#ifndef TRIANGLE_CONTAINS_U_C
#define TRIANGLE_CONTAINS_U_C

#include <stdbool.h>

static inline bool
triangle_contains_point(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    int denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if( denominator != 0 )
    {
        float a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / (float)denominator;
        float b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / (float)denominator;
        float c = 1 - a - b;
        return (a >= 0 && b >= 0 && c >= 0);
    }
    return false;
}

#endif