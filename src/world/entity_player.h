#ifndef WORLD_ENTITY_PLAYER_H
#define WORLD_ENTITY_PLAYER_H

#include "entity_facets.h"

struct WorldEntity_Player
{
    int element_id;
    struct WorldEntityFacet_GridPosition grid_position;
    struct WorldEntityFacet_DrawPosition draw_position;
    struct WorldEntityFacet_ViewPlacement view_placement;
    struct WorldEntityFacet_Orientation orientation;
    struct WorldEntityFacet_Pathing pathing;
    struct WorldEntityFacet_IdleAnimations idle_animations;
    struct WorldEntityFacet_Animation animation;
    struct WorldEntityFacet_Facing facing;
    struct WorldEntityFacet_Combat combat;
    struct WorldEntityFacet_Chat chat;
    struct WorldEntityFacet_Appearance appearance;
    struct WorldEntityFacet_ExactMove exact_move;
    struct WorldEntityFacet_EntitySpotanim spotanim;
    char name[32];
    int combat_level;
    int gender;
    int headicon;
    /** Team-cape id folded out of the worn equipment when the appearance was
     *  applied (reference ClientPlayer.team, ObjType.team opcode 115). 0 = no
     *  team. Read only by the "Attack" minimenu row. */
    int team;
    /** Server player slot (pid) this entity mirrors; -1 = local/unsynced. */
    int server_pid;
    /** Held-item override currently baked into the rendered appearance model
     * (reference ClientPlayer.getSequencedModel: a primary seq's
     * replaceheldleft/right swaps the left-hand/right-hand worn item). -1 =
     * base appearance is on the element (no active override); >= 0 = the model
     * was rebuilt with this canonical appearance slot (pkt_player_appearance.h)
     * in slot 5 (left) / slot 3 (right), where an empty one hides the held item
     * rather than swapping it. Tracked so
     * the model is rebuilt only when the effective override changes. */
    int held_left_applied;
    int held_right_applied;

    /** P_LOCMERGE / LOC_MERGE (ClientPlayer.locStartCycle/locStopCycle): while
     *  world->cycle is in [loc_start_cycle, loc_stop_cycle) the loc's model is
     *  meant to ride with this player. loc_merge_id is the base loc type; -1 =
     *  inactive. */
    int loc_start_cycle;
    int loc_stop_cycle;
    int loc_merge_id;
    int loc_merge_shape;
    int loc_merge_angle;
};

#endif
