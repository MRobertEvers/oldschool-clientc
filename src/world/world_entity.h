#ifndef WORLD_ENTITY_H
#define WORLD_ENTITY_H

#include "entity_npc.h"
#include "entity_objstack.h"
#include "entity_player.h"
#include "entity_pluginobj.h"
#include "entity_pool.h"
#include "entity_projectile.h"
#include "entity_scenery.h"
#include "entity_spotanim.h"
#include "entity_terrain.h"

struct World_EntityList
{
    struct World_EntityPool terrain;
    struct World_EntityPool scenery;
    struct World_EntityPool player;
    struct World_EntityPool npc;
    struct World_EntityPool projectile;
    struct World_EntityPool spotanim;
    struct World_EntityPool obj_stack;
    struct World_EntityPool plugin_object;
};

void
World_EntityListInit(struct World_EntityList* list);

void
World_EntityListFree(struct World_EntityList* list);

#endif
