#include "cs1vm_level.h"

#include <math.h>

static int g_cs1vm_level_experience[98];
static int g_cs1vm_level_experience_ready;

void
cs1vm_level_experience_init(void)
{
    if( g_cs1vm_level_experience_ready )
        return;

    int acc = 0;
    for( int i = 0; i < 98; i++ )
    {
        int level = i + 1;
        int delta = (int)(level + pow(2.0, level / 7.0) * 300.0);
        acc += delta;
        g_cs1vm_level_experience[i] = acc / 4;
    }
    g_cs1vm_level_experience_ready = 1;
}

int
cs1vm_level_experience_for_base_level(int base_level)
{
    cs1vm_level_experience_init();
    if( base_level <= 1 )
        return 0;
    int idx = base_level - 2;
    if( idx < 0 )
        return 0;
    if( idx >= 98 )
        idx = 97;
    return g_cs1vm_level_experience[idx];
}
