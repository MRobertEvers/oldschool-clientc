#ifndef WORLD_ENTITY_NPC_H
#define WORLD_ENTITY_NPC_H

#include <stdbool.h>
#include <stdint.h>

#include "entity_facets.h"
#include "entity_tint.h"
#include "entity_freeze.h"
#include "entity_transparency.h"

/*
 * Rev-239 numbered graphic slots: an npc wears one attached graphic per slot,
 * and a SPOTANIM entry replaces only its own slot (a Tormented Demon's shield
 * loop in slot 4 plays under its claw arc in slot 2). The wire's slot is a
 * byte, every value its own (WorldEntityFacet_EntitySpotanims).
 */
enum
{
    WORLD_NPC_HEADICON_SLOTS = 8,
};

/* What the server set on the npc itself over its type's value (NAME_CHANGE,
 * LEVEL_CHANGE, BAS_CHANGE): a retype the CLIENT makes (a varp moving its
 * multinpc onto another rung) keeps them, as the reference keeps the
 * overrides on the npc, not the type. A server type change clears them. */
enum
{
    WORLD_NPC_OVERRIDE_NAME = 1 << 0,
    WORLD_NPC_OVERRIDE_LEVEL = 1 << 1,
    WORLD_NPC_OVERRIDE_STANCES = 1 << 2,
};

/*
 * One server customisation of an npc's model (deob class399, built by
 * NPC_INFO's BODY_CUSTOM / HEAD_CUSTOM blocks, Statics ~47300-47520 and
 * ~47775-47855). Every array is sized from the wire's count, as the reference
 * sizes it (`new int[count]`, the count a byte or the type's own length), and
 * owned by the row on ToriMem_Scene(), like the pool the row lives in; NULL
 * when its count is 0. World_NpcSetBodyCustom / World_NpcSetHeadCustom replace
 * it whole, World_NpcDespawn hands it to the removal's snapshot
 * (World_EventsClear frees it there) and World_Free frees what is left.
 */
struct WorldNpcOverride
{
    /** An override is set (the block's 0x1 clears it: class86.method2907). */
    bool active;
    /** Unique per block received (class399.field5098): the built model's
     *  cache key carries it, so an override never reads another's model. */
    uint32_t serial;
    /** 0x2: `models` replace the type's (even an empty list: an empty model).
     *  Without it the type's own models are drawn (Statics.method9224). */
    bool models_sent;
    int model_count;
    int* models;
    /** 0x4 / 0x8: replace the TYPE's destination colours / textures index
     *  for index, over the type's own source list (Statics.method9224).
     *  An index past the list keeps the type's. */
    int recolour_count;
    int* recolours;
    int retexture_count;
    int* retextures;
    /** The V4 form's recolour-all (osrs242: 0x4 then a 1 flag byte and a
     *  g2): every face of the body takes `recolour_all`, in place of the
     *  index-for-index recolours; 65535 (-1 as the reference's short) is none
     *  and the list follows instead (rev-242 deob class454.method10217:
     *  `if (var10 != -1) var9.method4694(var10)` else the per-colour list). */
    bool has_recolour_all;
    int recolour_all;
    /** 0x10: draw the local player instead (class399.field5094). */
    bool mirror;
    /** 0x20 (the counted forms): draw a player composition built from
     *  `identkits`, `body_type` and the first five recolours
     *  (class399.field5090, class86.method2906). */
    bool player;
    int body_type;
    int identkit_count;
    int* identkits;
};

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
    /** The map editor's spawn preview, not an npc of any game: the menu
     *  offers "Select spawn" and none of the type's ops. */
    bool editor_preview;
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
    /** WORLD_NPC_OVERRIDE_* bits. */
    uint8_t overrides;
    /** A varp its multinpc chain depends on changed since the rung it draws
     *  was resolved (counted in World.npc_retype_wanted). */
    bool retype_wanted;
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
    struct WorldEntityFacet_EntitySpotanims spotanims;
    /** Rev-239 NPC_INFO tinting (`npc_tint`, flag 0x100): the body drawn
     *  recoloured while active -- Akkha and his shadows while immune.
     *  Applied per frame by app_world_sync_npc_tints. */
    struct WorldEntityTint tint;
    /** Rev-241 NPC_INFO transparency (`npc_transparency`, flag 0x8000): the
     *  body faded in or out -- Ocellus teleporting. Applied per frame by
     *  app_world_sync_npc_transparency. */
    struct WorldEntityTransparency transparency;
    /** FREEZE windows and the frozen drawing (entity_freeze.h). */
    struct WorldEntityFreeze freeze;
    /**
     * Headicon customisation (rev-239 0x400000): while `headicon_custom`, the
     * overhead draws these eight (sprite group, frame) slots instead of the
     * type's headicon -- one type whose prayer the server sets (a Tormented
     * Demon). Group -1 is an empty slot. deob class112.field1578.
     */
    bool headicon_custom;
    int headicon_group[WORLD_NPC_HEADICON_SLOTS];
    int headicon_index[WORLD_NPC_HEADICON_SLOTS];
    /**
     * Body customisation (NPC_INFO BODY_CUSTOM: rev-239 0x800000 typed and
     * 0x2000000 counted) and chathead customisation (HEAD_CUSTOM, rev-239
     * 0x20000): the server's override of what the type draws (deob class399,
     * class86.field1246 and field1247). While `body_custom.active` the body is
     * built from it (app_world_build_npc_body); bumping `body_serial` asks the
     * app to remount the body (app_world_sync_npc_bodies) and
     * `body_mounted_serial` is the one it last mounted. A retype keeps both
     * (the override is the npc's, not the type's). See struct WorldNpcOverride.
     */
    struct WorldNpcOverride body_custom;
    struct WorldNpcOverride head_custom;
    int body_serial;
    int body_mounted_serial;
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
    /** Unique for the World's life (World.entity_spawn_serial): with
     *  server_slot it names this npc, never a later one in the same slot. */
    uint32_t spawn_serial;
};

#endif
