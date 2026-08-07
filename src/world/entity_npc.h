#ifndef WORLD_ENTITY_NPC_H
#define WORLD_ENTITY_NPC_H

#include <stdbool.h>

#include "entity_facets.h"

struct WorldEntity_NPC
{
    int element_id;
    struct WorldEntityFacet_GridPosition grid_position;
    struct WorldEntityFacet_DrawPosition draw_position;
    struct WorldEntityFacet_Orientation orientation;
    struct WorldEntityFacet_Pathing pathing;
    int npc_id;
    int size;
    /** NpcType.alwaysontop (opcode 99). Draw-order tier: alwaysontop NPCs
     *  register with the painter before other players and normal NPCs, so
     *  they claim the tile in the one-entity-per-tile dedup (Client.ts
     *  addNpcs, called first with alwaysontop=true). */
    bool alwaysontop;
    int combat_level;
    char name[32];
    struct WorldEntityFacet_Action actions[5];
    /** Bit i controls whether action i is offered by the minimenu. */
    uint8_t visible_ops;
    uint32_t spawn_cycle;
    struct WorldEntityFacet_IdleAnimations idle_animations;
    struct WorldEntityFacet_Animation animation;
    struct WorldEntityFacet_Facing facing;
    struct WorldEntityFacet_Combat combat;
    struct WorldEntityFacet_Chat chat;
    struct WorldEntityFacet_EntitySpotanim spotanim;
    /** Exact-move window (Actor fields on the deob). Classic NPC_INFO has no
     * exact-move mask, but rebuild shifts these the same as players, and the
     * cycle update consumes them when set. */
    struct WorldEntityFacet_ExactMove exact_move;
    /** Server npc slot this entity mirrors; -1 = local/unsynced. */
    int server_slot;
};

#endif
