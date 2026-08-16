#ifndef TORIRS_IDK_FROM_RSCACHE_H
#define TORIRS_IDK_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Idk*
ToriRS_IdkFromRSCacheDat1(
    int idk_id,
    const struct RSCache_Dat1ConfigIdk* src);

struct ToriRS_Idk*
ToriRS_IdkFromRSCacheDat2(
    int idk_id,
    const struct RSCache_Dat2ConfigIdk* src);

#endif
