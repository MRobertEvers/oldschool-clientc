#ifndef TORIRS_MODEL_FROM_RSCACHE_H
#define TORIRS_MODEL_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

/** Moves heap arrays out of src into a new ToriRS_Model; leaves src hollow. */
struct ToriRS_Model*
ToriRS_ModelFromRSCache(struct RSCache_Model* src);

#endif
