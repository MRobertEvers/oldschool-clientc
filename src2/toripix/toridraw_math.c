#include "toridraw_math.h"

#include <math.h>

int g_cos_table[2048];
int g_sin_table[2048];
int g_reciprocal16[4096];

void
toridraw_init_math(void)
{
    for( int i = 0; i < 2048; i++ )
    {
        g_cos_table[i] = (int)(cos((double)i * 0.0030679615) * (1 << 16));
        g_sin_table[i] = (int)(sin((double)i * 0.0030679615) * (1 << 16));
    }

    for( int i = 1; i < 4096; i++ )
    {
        g_reciprocal16[i] = ((1 << 16) / i);
    }
}