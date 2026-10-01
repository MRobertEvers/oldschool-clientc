#ifndef WORLD_ENTITY_PLAYER_H
#define WORLD_ENTITY_PLAYER_H

#include "entity_facets.h"

/* WorldEntity_Player.body.npc_id when the mounted model is not an npc's: built
 * from the appearance slots, or an empty model because the transmog's multinpc
 * selected nothing. Every other value is the npc type the body was built from. */
#define WORLD_PLAYER_BODY_FROM_SLOTS (-1)
#define WORLD_PLAYER_BODY_HIDDEN (-2)

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
    /** The npc type the appearance says this player is drawn as (the 239
     *  block's 0xffff first equipment entry, p_transmogrify); -1 = the
     *  player's own body. Reference ClientPlayer.transmog (LostCity
     *  ClientPlayer.ts setAppearance) / PlayerAppearance.npcTransformId. */
    int transmog_npc_id;
    /** The footprint a placement centres the player on: the transmog npc
     *  type's own `size` while transmogged and its config is resident, else 1.
     *  Reference Player.transformedSize() (deob Player.java), read only by
     *  Player.resetPath -- a teleport/placement puts the model at
     *  tile*128 + size*64; walking steps keep the 1-tile centre, since a
     *  player's Actor.size is never set. Kept by app_world_reconcile_player_body. */
    int transmog_size;
    /** What the scene element's model was built from: the appearance
     *  slots with any held-item override of the playing seq folded in
     *  (reference ClientPlayer.getSequencedModel: replaceheldleft/right swap
     *  slot 5 / slot 3), the colours and the gender. The body is DERIVED --
     *  the per-frame pass rebuilds it whenever the wanted key differs from
     *  this one and every part of the wanted body is resident, and until
     *  then the element keeps the last whole body. Content, not a counter:
     *  whatever changed the wanted body, the comparison sees it. */
    struct
    {
        int slots[12];
        int colors[5];
        int gender;
        /** The npc type the mounted model was built from when the body is a
         *  transmog (multinpc already resolved); -1 = built from the slots. */
        int npc_id;
    } body;

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
