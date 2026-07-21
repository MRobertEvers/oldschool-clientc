/*
 * Local-player stat store backing the CS1 stat opcodes.
 */

#include "rs_player_stats.h"

#include <assert.h>
#include <math.h>
#include <string.h>

void
RS_PlayerStats_Init(struct RS_PlayerStats* stats)
{
    assert(stats);
    memset(stats, 0, sizeof(*stats));

    /* Classic level->xp curve: running sum of floor(l + 300 * 2^(l/7)), / 4.
     * level_xp[l - 1] is the xp needed to reach level l + 1. */
    int points = 0;
    for( int level = 1; level <= RS_PLAYER_STATS_LEVEL_MAX; level++ )
    {
        points += (int)floor((double)level + (300.0 * pow(2.0, (double)level / 7.0)));
        stats->level_xp[level - 1] = points / 4;
    }

    for( int i = 0; i < RS_PLAYER_STATS_SKILL_COUNT; i++ )
    {
        stats->base_level[i] = 1;
        stats->current_level[i] = 1;
        stats->xp[i] = 0;
    }

    stats->base_level[RS_SKILL_HITPOINTS] = 10;
    stats->current_level[RS_SKILL_HITPOINTS] = 10;
    stats->xp[RS_SKILL_HITPOINTS] = 1154;

    stats->run_energy = 100;
    stats->run_weight = 0;
    stats->coord_x = 0;
    stats->coord_z = 0;

    RS_PlayerStats_RecomputeCombatLevel(stats);
}

int
RS_PlayerStats_TotalLevel(struct RS_PlayerStats const* stats)
{
    assert(stats);

    int total = 0;
    for( int i = 0; i <= 17; i++ )
        total += stats->base_level[i];
    total += stats->base_level[RS_SKILL_RUNECRAFT];
    return total;
}

int
RS_PlayerStats_XpRemaining(
    struct RS_PlayerStats const* stats,
    int skill)
{
    assert(stats);

    if( skill < 0 || skill >= RS_PLAYER_STATS_SKILL_COUNT )
        return 0;

    int index = stats->base_level[skill] - 1;
    if( index < 0 )
        index = 0;
    if( index >= RS_PLAYER_STATS_LEVEL_MAX )
        index = RS_PLAYER_STATS_LEVEL_MAX - 1;
    return stats->level_xp[index];
}

void
RS_PlayerStats_RecomputeCombatLevel(struct RS_PlayerStats* stats)
{
    assert(stats);

    int base = stats->base_level[RS_SKILL_DEFENCE] + stats->base_level[RS_SKILL_HITPOINTS] +
               (stats->base_level[RS_SKILL_PRAYER] / 2);
    int melee = stats->base_level[RS_SKILL_ATTACK] + stats->base_level[RS_SKILL_STRENGTH];
    int ranged = stats->base_level[RS_SKILL_RANGED] * 3 / 2;
    int magic = stats->base_level[RS_SKILL_MAGIC] * 3 / 2;

    int best = melee;
    if( ranged > best )
        best = ranged;
    if( magic > best )
        best = magic;

    stats->combat_level = (base + best * 13 / 10) / 4;
}

void
RS_PlayerStats_SetXp(
    struct RS_PlayerStats* stats,
    int skill,
    int xp)
{
    assert(stats);

    if( skill < 0 || skill >= RS_PLAYER_STATS_SKILL_COUNT )
        return;

    stats->xp[skill] = xp;

    int level = 1;
    while( level < RS_PLAYER_STATS_LEVEL_MAX && xp >= stats->level_xp[level - 1] )
        level++;

    stats->base_level[skill] = level;
    if( stats->current_level[skill] < 1 )
        stats->current_level[skill] = level;
}
