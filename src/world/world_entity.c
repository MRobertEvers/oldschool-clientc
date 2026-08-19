#include "world_entity.h"

#include "entity_scenery.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Loc-inspection gating (entity_scenery.h). Process-wide rather than per-World
 * because both consumers — the pick classifier and the minimenu builder — are
 * handed a World but no tool state, and the tools are a property of the client
 * session, not of a scene. */
static int g_scenery_debug_env = -1;
static bool g_scenery_tools_active = false;

bool
WorldEntity_SceneryDebugEnabled(void)
{
    if( g_scenery_debug_env < 0 )
        g_scenery_debug_env = getenv("TORIRS_LOC_DEBUG") != NULL;
    return g_scenery_debug_env != 0;
}

bool
WorldEntity_SceneryPickInactive(void)
{
    return WorldEntity_SceneryDebugEnabled() || g_scenery_tools_active;
}

void
WorldEntity_SceneryDebugSetTools(bool active)
{
    g_scenery_tools_active = active;
}

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
    World_EntityPoolInit(&list->obj_stack, (int)sizeof(struct WorldEntity_ObjStack));
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
    World_EntityPoolFree(&list->obj_stack);
}
