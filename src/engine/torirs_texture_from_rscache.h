#ifndef TORIRS_TEXTURE_FROM_RSCACHE_H
#define TORIRS_TEXTURE_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Texture*
ToriRS_TextureFromRSCache(
    int texture_id,
    const struct RSCache_Dat2Texture* src);

#endif
