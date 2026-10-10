#ifndef WORLD_ENTITY_NPC_H
#define WORLD_ENTITY_NPC_H

#include <stdbool.h>

#include "entity_facets.h"

struct WorldEntity_NPC
{
    int element_id;
    struct WorldEntityFacet_GridPosition grid_position;
    struct WorldEntityFacet_DrawPosition draw_position;
    struct WorldEntityFacet_ViewPlacement view_placement;
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
    /**
     * The newest SEQUENCE and SPOTANIM the server SENT this npc, and the world
     * cycle each arrived on -- stamped by World_NpcSetPrimaryAnimation and
     * World_NpcSetSpotanim before anything decides whether to play it.
     *
     * Not `animation.primary.anim_id` / `spotanim.id`: those are what the npc
     * is DRAWING, and world_apply_primary_animation does not restart a seq
     * re-sent while it is already playing (unless the seq's replay mode says
     * so) and refuses a lower-priority one, so a boss repeating one attack
     * seq is invisible to "did anim_id change". A raid test anchors "the boss
     * attacked on tick T" on the op's arrival, which is this. -1 / 0 until
     * the first op. The quest driver reads them (DriveNpcRow.seq_tick).
     */
    int seq_sent_id;
    int seq_sent_cycle;
    int spotanim_sent_id;
    int spotanim_sent_cycle;
    /**
     * The graphic the newest SPOTANIM op set, a clear (-1) included: what
     * scriptrun's npc row reports as spotanim_id, where `spotanim.id` is the
     * graphic still DRAWING, cleared only once its seq has loaded and run out
     * (world_cycle.c), so a first-seen graphic read late and a cleared one
     * lingered (live lane audit, 2026-10-10).
     */
    int spotanim_packet_id;
    /**
     * The newest FACE_COORD op the server sent this npc (`npc_facesquare`),
     * as the wire sent it -- absolute half-tiles, (tile << 1) + size -- and
     * the world cycle it arrived on. Stamped in task_exec_entity_info.c where
     * the op is applied.
     *
     * Not `facing.square_x/z`: those are the PENDING turn and world_cycle.c
     * clears them the cycle the turn is consumed, so by the time a reader
     * looks they are 0 again. A raid test asks "which square did the boss
     * turn to, and on which tick" (Xarpus P3 turns to the quadrant he was hit
     * from, tob_xarpus.rs2), which is this. 0,0 = none, the same sentinel
     * facing.square_x uses (and the zero a fresh entity starts with). The
     * quest driver reads them (DriveNpcRow.face_x/face_z/face_tick).
     */
    int face_sent_x;
    int face_sent_z;
    int face_sent_cycle;
    /** Exact-move window (Actor fields on the deob). Classic NPC_INFO has no
     * exact-move mask, but rebuild shifts these the same as players, and the
     * cycle update consumes them when set. */
    struct WorldEntityFacet_ExactMove exact_move;
    /** Server npc slot this entity mirrors; -1 = local/unsynced. */
    int server_slot;
};

#endif
