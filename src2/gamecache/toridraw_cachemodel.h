#ifndef TORIDRAW_CACHEMODEL_H
#define TORIDRAW_CACHEMODEL_H

#include "toridraw/toridraw_types.h"

struct CacheModel;

struct ToriDraw_Model*
toridraw_model_new_from_cache_model(struct CacheModel* model);

#endif
