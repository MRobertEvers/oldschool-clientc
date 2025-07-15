#include "scene2.h"

#include "scene2_floor.h"

struct Scene2*
scene2_new_from_world(struct World* world, struct Cache* cache)
{
    struct WorldModel* wmodel = NULL;
    struct CacheModel* cmodel = NULL;
    struct Scene2Model* scene2_model = NULL;
    struct Scene2* scene2 = malloc(sizeof(struct Scene2));
    memset(scene2, 0x00, sizeof(struct Scene2));
    scene2->world = world;

    scene2->models = malloc(sizeof(struct Scene2Model) * world->world_model_count);
    memset(scene2->models, 0x00, sizeof(struct Scene2Model) * world->world_model_count);

    scene2->terrain = scene2_floor_new_from_map_terrain(world->floors, cache);

    scene2->_model_cache = model_cache_new();

    for( int i = 0; i < world->world_model_count; i++ )
    {
        wmodel = &world->world_models[i];
        scene2_model = &scene2->models[i];

        for( int j = 0; j < wmodel->model_count; j++ )
        {
            int model_id = wmodel->models[j];
            cmodel = model_cache_checkout(scene2->_model_cache, cache, model_id);
            // TODO: Handle this better.
            if( !cmodel )
                continue;

            scene2_model->models[scene2_model->model_count] = cmodel;
            scene2_model->model_ids[scene2_model->model_count] = model_id;
            scene2_model->model_count++;
        }
    }

    return scene2;
}