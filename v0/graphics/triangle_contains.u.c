#ifndef TRIANGLE_CONTAINS_U_C
#define TRIANGLE_CONTAINS_U_C

#include <stdbool.h>

static inline bool
triangle_contains_point_float(
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    float x,
    float y)
{
    float denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if( denominator != 0.f )
    {
        float a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / denominator;
        float b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / denominator;
        float c = 1.f - a - b;
        return (a >= 0.f && b >= 0.f && c >= 0.f);
    }
    return false;
}

static inline bool
triangle_contains_point_int(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    int den = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if( den == 0 )
    {
        return false;
    }
    int a_num = (y2 - y3) * (x - x3) + (x3 - x2) * (y - y3);
    int b_num = (y3 - y1) * (x - x3) + (x1 - x3) * (y - y3);
    int c_num = den - a_num - b_num;
    if( den > 0 )
    {
        return a_num >= 0 && b_num >= 0 && c_num >= 0;
    }
    return a_num <= 0 && b_num <= 0 && c_num <= 0;
}

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
    return triangle_contains_point_int(x1, y1, x2, y2, x3, y3, x, y);
}

#endif
