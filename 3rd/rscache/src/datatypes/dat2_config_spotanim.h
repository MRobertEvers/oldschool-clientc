#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_SPOTANIM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_SPOTANIM_H

/*
 * Spotanim (graphical effect) config from the dat2 config group (kind 13).
 * Mirrors Runelite's SpotAnimLoader / the OSRS SpotAnimType format, whose
 * core opcodes match the dat1 SpotType.decode:
 *   1: model g2        2: animation/seq g2
 *   4: resizeh g2      5: resizev g2      6: rotation g2
 *   7: ambient g1      8: contrast g1
 *   40..49: recolor_to_find[code-40] g2   50..59: recolor_to_replace[code-50] g2
 *   60..69: retexture_to_find[code-60] g2 70..79: retexture_to_replace[code-70] g2
 *   0: terminator
 */
struct RSCache_Dat2ConfigSpotanim
{
    int model;
    int anim; /* seq id, or -1 */
    int resizeh;
    int resizev;
    int angle;
    int ambient;
    int contrast;
    int recol_s[6];
    int recol_d[6];
    int retex_s[6];
    int retex_d[6];
};

struct RSCache_Dat2ConfigSpotanim*
RSCache_Dat2ConfigSpotanimNewDecode(int revision, char* data, int data_size);

void
RSCache_Dat2ConfigSpotanimFree(struct RSCache_Dat2ConfigSpotanim* spotanim);

#endif
