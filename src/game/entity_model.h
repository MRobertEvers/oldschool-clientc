#ifndef ENTITY_MODEL_H
#define ENTITY_MODEL_H

#include "../osrs/cache.h"
#include "../osrs/tables/config_sequence.h"
#include "../osrs/tables/frame.h"
#include "../osrs/tables/framemap.h"
#include "../osrs/tables/model.h"

struct EntityModel
{
    struct CacheModel* model;
    struct CacheModelBones* bones_nullable;

    struct CacheFrame** frames_nullable;
    struct CacheFramemap* framemap_nullable;
    struct CacheConfigSequence* sequence_nullable;

    int frame_tick;
};

struct EntityModel* entity_model_new_from_cache(struct Cache* cache, int model_id);

#endif