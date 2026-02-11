#ifndef PLAYER_STATS_H
#define PLAYER_STATS_H

#define PLAYER_STAT_COUNT 23
#define PLAYER_LEVEL_MAX 99

/** XP required for each level (1..99). levelExperience[0] = xp for level 2, etc. */
extern int g_player_level_experience[98];

/** Initialize level experience table. Call once at startup. */
void
player_stats_init(void);

/** Compute base level (1-99) from XP using level experience table. */
int
player_stats_xp_to_level(int xp);

#endif
