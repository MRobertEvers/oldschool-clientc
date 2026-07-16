#ifndef CLAMP_H
#define CLAMP_H

#include "dash_restrict.h"

static inline int
clamp(int value, int min, int max)
{
    if( value < min )
        return min;
    if( value > max )
        return max;
    return value;
}

#endif