#ifndef WORLD_H
#define WORLD_H

#include "scene.h"

enum EntityType
{
    ENTITY_TYPE_NULL,
    ENTITY_TYPE_PLAYER,
    ENTITY_TYPE_OBJECT,
};

struct Entity
{
    enum EntityType type;
    int x;
    int z;
    int level;

    struct SceneModel* model;

    struct Entity* next;
};

struct World
{
    struct Entity* entity_list;
};

struct World* world_new(void);
void world_free(struct World* world);

struct Entity*
world_player_entity_new_add(struct World* world, int x, int z, int level, struct SceneModel* model);

void world_player_entity_free(struct World* world, struct Entity* entity);

void world_begin_scene_frame(struct World* world, struct Scene* scene);

void world_end_scene_frame(struct World* world, struct Scene* scene);

#endif