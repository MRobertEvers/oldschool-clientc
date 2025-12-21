#ifndef DASHLIB_H
#define DASHLIB_H

#include "graphics/dash.h"
#include "osrs/tables/model.h"

struct DashModel* dashmodel_new_from_cache_model(struct CacheModel* model);

#endif