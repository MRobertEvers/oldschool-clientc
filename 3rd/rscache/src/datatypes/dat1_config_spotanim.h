#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_SPOTANIM_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_SPOTANIM_H

/*
 * Spotanim (graphical effect) config from the dat1 config jagfile's
 * "spotanim.dat". Mirrors SpotType.decode in the LostCity JavaClient
 * (Client-TS config/SpotType.ts):
 *   code 1: model g2       code 2: anim/seq g2
 *   code 4: resizeh g2     code 5: resizev g2
 *   code 6: angle g2       code 7: ambient g1   code 8: contrast g1
 *   code 40..49: recol_s[code-40] g2   code 50..59: recol_d[code-50] g2
 *   code 0: terminator
 */
#include "dat2_config_spotanim.h"

struct RSCache_Dat1ConfigSpotanim
{
    int model;
    int anim; /* seq id, or -1 */
    int resizeh;
    int resizev;
    int angle;
    int ambient;
    int contrast;
    /* Six slots, though the opcode ranges above are ten wide — see
     * RSCACHE_SPOTANIM_COLOUR_SLOTS. */
    int recol_s[RSCACHE_SPOTANIM_COLOUR_SLOTS];
    int recol_d[RSCACHE_SPOTANIM_COLOUR_SLOTS];
};

/** Whole "spotanim.dat" table: entries are variable-length and only
 * sequentially addressable, so they decode as one list. */
struct RSCache_Dat1ConfigSpotanimList
{
    struct RSCache_Dat1ConfigSpotanim* spotanims;
    int spotanims_count;
};

struct RSCache_Dat1ConfigSpotanimList*
RSCache_Dat1ConfigSpotanimListNewDecode(
    char* data,
    int data_size);

void
RSCache_Dat1ConfigSpotanimListFree(struct RSCache_Dat1ConfigSpotanimList* list);

/** Returns the number of bytes this entry consumed. */
int
RSCache_Dat1ConfigSpotanimDecodeInplace(
    struct RSCache_Dat1ConfigSpotanim* spotanim,
    char* data,
    int data_size);

#endif
