#ifndef RS_PLAYER_STATS_H
#define RS_PLAYER_STATS_H

/*
 * Local-player state that CS1 value scripts read: skill levels/xp, run energy,
 * weight, combat level, and world position. There is no server sync yet, so
 * RS_PlayerStats_Init seeds the values a fresh account would have; scripts then
 * evaluate correctly by construction once a sync path fills this in.
 */

#define RS_PLAYER_STATS_SKILL_COUNT 25
#define RS_PLAYER_STATS_LEVEL_MAX 99

/** Skill ids used by the combat/total-level formulas (IF1 opcode order). */
#define RS_SKILL_ATTACK 0
#define RS_SKILL_DEFENCE 1
#define RS_SKILL_STRENGTH 2
#define RS_SKILL_HITPOINTS 3
#define RS_SKILL_RANGED 4
#define RS_SKILL_PRAYER 5
#define RS_SKILL_MAGIC 6
/** Total level sums skills 0..17 plus this one — matches the reference client. */
#define RS_SKILL_RUNECRAFT 20

struct RS_PlayerStats
{
    int base_level[RS_PLAYER_STATS_SKILL_COUNT];
    int current_level[RS_PLAYER_STATS_SKILL_COUNT];
    int xp[RS_PLAYER_STATS_SKILL_COUNT];

    int run_energy; /* 0..100 */
    int run_weight; /* kg */
    int combat_level;

    int coord_x; /* absolute world tile */
    int coord_z;

    /** level_xp[n] = xp required to reach level n + 2. */
    int level_xp[RS_PLAYER_STATS_LEVEL_MAX];
};

void
RS_PlayerStats_Init(struct RS_PlayerStats* stats);

/** Sum of base levels for skills 0..17 plus runecraft. */
int
RS_PlayerStats_TotalLevel(struct RS_PlayerStats const* stats);

/** XP threshold for the skill's current base level (IF1 opcode 6). */
int
RS_PlayerStats_XpRemaining(
    struct RS_PlayerStats const* stats,
    int skill);

/** Recomputes combat_level from the current base levels. */
void
RS_PlayerStats_RecomputeCombatLevel(struct RS_PlayerStats* stats);

/** Sets a skill's xp and derives its base level from the xp table. */
void
RS_PlayerStats_SetXp(
    struct RS_PlayerStats* stats,
    int skill,
    int xp);

#endif /* RS_PLAYER_STATS_H */
