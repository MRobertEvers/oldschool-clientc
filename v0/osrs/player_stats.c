#include "player_stats.h"

#include <math.h>

int g_player_level_experience[98];

void
player_stats_init(void)
{
    int acc = 0;
    for( int i = 0; i < 98; i++ )
    {
        int level = i + 1;
        int delta = (int)(level + pow(2.0, level / 7.0) * 300.0);
        acc += delta;
        g_player_level_experience[i] = acc / 4;
    }
}

int
player_stats_xp_to_level(int xp)
{
    int base = 1;
    for( int i = 0; i < 98; i++ )
    {
        if( xp >= g_player_level_experience[i] )
            base = i + 2;
    }
    return base;
}
