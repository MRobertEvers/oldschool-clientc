#include "world.h"

#include <stdlib.h>
#include <string.h>

struct World*
world_new(void)
{
    struct World* world = (struct World*)malloc(sizeof(struct World));
    memset(world, 0, sizeof(struct World));

    world->entity_list = (struct Entity*)malloc(sizeof(struct Entity));
    memset(world->entity_list, 0, sizeof(struct Entity));
    world->entity_list->type = ENTITY_TYPE_NULL;

    return world;
}

void
world_free(struct World* world)
{
    free(world);
}

struct Entity*
world_player_entity_new_add(struct World* world, int x, int z, int level, struct SceneModel* model)
{
    struct Entity* entity = (struct Entity*)malloc(sizeof(struct Entity));
    memset(entity, 0, sizeof(struct Entity));

    struct Entity* current = world->entity_list;
    while( current->next != NULL )
        current = current->next;
    current->next = entity;

    entity->type = ENTITY_TYPE_PLAYER;
    entity->x = x;
    entity->z = z;
    entity->level = level;
    entity->model = model;

    return entity;
}

void
world_player_entity_free(struct World* world, struct Entity* entity)
{}

void
world_begin_scene_frame(struct World* world, struct Scene* scene)
{
    struct Entity* entity = world->entity_list->next;
    while( entity )
    {
        switch( entity->type )
        {
        case ENTITY_TYPE_PLAYER:
            scene_add_player_entity(scene, entity->x, entity->z, entity->level, entity->model);
            break;
        default:
            break;
        }

        entity = entity->next;
    }
}

void
world_end_scene_frame(struct World* world, struct Scene* scene)
{
    scene_clear_entities(scene);
}