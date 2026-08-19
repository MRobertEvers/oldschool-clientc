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
    /** Type named by the server. For a multiNpc this remains the wrapper while
     *  npc_id is the child selected by this client's local varp/varbit state.
     *  Keeping both is what lets two players see different quest forms and
     *  lets a later varp update remorph an NPC without another NPC_INFO add. */
    int base_npc_id;
    int npc_id;
    /** The local multiNpc table selected -1. The entity remains registered so
     *  subsequent NPC_INFO masks and varp-driven reappearance still work, but
     *  every player-facing renderer/menu path treats it as absent. */
    bool multinpc_hidden;
    int size;
    /** NpcType.alwaysontop (opcode 99). Draw-order tier: alwaysontop NPCs
     *  register with the painter before other players and normal NPCs, so
     *  they claim the tile in the one-entity-per-tile dedup (Client.ts
     *  addNpcs, called first with alwaysontop=true). */
    bool alwaysontop;
    /** NpcType.minimap (opcode 93). false = no minimap dot for this npc
     *  (Client.ts minimapDraw skips it). Defaults true at spawn so an npc
     *  whose type never resolved still shows, which is the old behaviour. */
    bool minimap_visible;
    /** NpcType.interactable (opcode 107). The minimap gate is BOTH this and
     *  `minimap_visible` — see the reference quoted on ToriRS_Npctype. Same
     *  default-true rule and for the same reason. */
    bool interactable;
    int combat_level;
    /* 64, matching ToriRS_Npctype.name (TORIRS_NAME_MAX) -- col-tagged names
     * like "<col=00ffff>Ancestral Glyph</col>" don't fit in 32. */
    char name[64];
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
