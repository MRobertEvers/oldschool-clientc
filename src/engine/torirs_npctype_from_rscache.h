#ifndef TORIRS_NPCTYPE_FROM_RSCACHE_H
#define TORIRS_NPCTYPE_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Npctype*
ToriRS_NpctypeFromRSCacheDat1(
    int npc_id,
    const struct RSCache_Dat1ConfigNpc* src);

struct ToriRS_Npctype*
ToriRS_NpctypeFromRSCacheDat2(
    int npc_id,
    const struct RSCache_Dat2ConfigNpc* src);

#endif
