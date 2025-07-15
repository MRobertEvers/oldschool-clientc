#ifndef OSRS_SCENE2_H
#define OSRS_SCENE2_H

#include "scene2_floor.h"
#include "scene_cache.h"
#include "world.h"

struct Scene2Model
{
    struct CacheModel** models;
    int* model_ids;
    int model_count;
};

struct Scene2
{
    struct World* world;

    // Loaded models and floors
    struct Scene2Floor* terrain;
    struct Scene2Model* models;

    struct ModelCache* _model_cache;
};

struct Scene2* scene2_new_from_world(struct World* world, struct Cache* cache);
void scene2_free(struct Scene2* scene2);

#endif // OSRS_SCENE2_H