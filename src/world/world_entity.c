#include "world_entity.h"

#include <assert.h>
#include <string.h>

void
World_EntityListInit(struct World_EntityList* list)
{
    assert(list);
    memset(list, 0, sizeof(*list));
    World_EntityPoolInit(&list->terrain, (int)sizeof(struct WorldEntity_Terrain));
    World_EntityPoolInit(&list->scenery, (int)sizeof(struct WorldEntity_Scenery));
    World_EntityPoolInit(&list->player, (int)sizeof(struct WorldEntity_Player));
    World_EntityPoolInit(&list->npc, (int)sizeof(struct WorldEntity_NPC));
    World_EntityPoolInit(&list->projectile, (int)sizeof(struct WorldEntity_Projectile));
    World_EntityPoolInit(&list->spotanim, (int)sizeof(struct WorldEntity_Spotanim));
}

void
World_EntityListFree(struct World_EntityList* list)
{
    if( !list )
        return;
    World_EntityPoolFree(&list->terrain);
    World_EntityPoolFree(&list->scenery);
    World_EntityPoolFree(&list->player);
    World_EntityPoolFree(&list->npc);
    World_EntityPoolFree(&list->projectile);
    World_EntityPoolFree(&list->spotanim);
}
