#ifndef WORLD_ENTITY_FACETS_H
#define WORLD_ENTITY_FACETS_H

#include <stdbool.h>
#include <stdint.h>

struct WorldEntityFacet_IdleAnimations
{
    int readyanim;
    int walkanim;
    int turnanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
};

struct WorldEntityFacet_AnimationStep
{
    uint16_t anim_id; /* (uint16_t)-1 = none */
    uint16_t frame;   /* dat1 seqs run up to 512 frames */
    uint16_t cycle;   /* per-frame tick accumulator (durations exceed 255) */
    uint8_t delay;    /* primary start-delay countdown (reference primaryAnimDelay) */
    uint8_t loop;     /* primary loop counter vs seq maxloops */
};

struct WorldEntityFacet_Animation
{
    struct WorldEntityFacet_AnimationStep primary;
    struct WorldEntityFacet_AnimationStep secondary;
    /* Route length at the moment the primary seq was applied (reference
     * preanimRouteLength; drives the PreanimMove.DELAYANIM gate). */
    uint8_t preanim_route_length;
    /* Cycles the route has been held still by a DELAYMOVE primary (reference
     * ClientEntity.animDelayMove). Catch-up afterwards forces moveSpeed 8. */
    uint8_t anim_delay_move;
    /* Set each cycle from the active primary seq's `stretches` flag (reference
     * ClientEntity.needsForwardDrawPadding). When set, the painter extends the
     * entity's tile footprint one tile forward along its yaw so a stretching
     * action does not draw in front of a wall ahead of it. */
    uint8_t needs_forward_draw_padding;
};

/* Server-forced interpolated move (reference exactMove1/exactMove2).
 * Tiles are scene-local; cycle stamps are absolute world cycles.
 * move_end == 0 && move_start == 0 => inactive. */
struct WorldEntityFacet_ExactMove
{
    uint8_t start_x;
    uint8_t start_z;
    uint8_t end_x;
    uint8_t end_z;
    int move_start;
    int move_end;
    uint16_t facing;
    bool facing_is_yaw;
};

/* Entity-attached graphic (reference spotanimId/Frame/Cycle/LastCycle). */
struct WorldEntityFacet_EntitySpotanim
{
    int id;         /* -1 = none */
    int height;
    int last_cycle; /* absolute world cycle the graphic starts at */
    int frame;      /* -1 while delayed */
    int cycle;
};

/**
 * Tile route queue for player/NPC movement.
 *
 * route[0] holds the entity's authoritative tile; new steps are prepended at 0
 * and older waypoints shift toward higher indices. route_length is the number
 * of queued tiles (0 = idle). Each tick the draw position moves toward
 * route[route_length - 1]; when it arrives, route_length is decremented.
 *
 * route_run[i] is 1 for a run step, 0 for walk. Queue depth is capped at 9.
 * Coordinates are world tiles; draw position uses 128 sub-tile units per tile.
 */
struct WorldEntityFacet_Pathing
{
    uint8_t route_length;
    uint8_t route_x[10];
    uint8_t route_z[10];
    uint8_t route_run[10];
};

/* Bit-packed tile coords: 9 bits x, 9 bits z, 4 bits level. */
struct WorldEntityFacet_GridPosition
{
    int x : 9;
    int z : 9;
    int level : 4;
};

/*
 * Sub-tile position (128 units per tile).
 *
 * `x`/`z` are the integer position every consumer reads -- rev-239
 * class105.field1474 / field1475. `fx`/`fz` are the same position carried at
 * full precision (field1517 / field1465), and they are what the per-FRAME
 * mover integrates: a render frame is not a whole 20ms client cycle, so the
 * distance walked in one is fractional. Rounding that away per frame is what
 * an integer-only mover does, and it is why the old cycle-quantised port
 * lurched -- see World_MoversAdvance.
 *
 * Invariant: x == (uint32_t)fx and z == (uint32_t)fz. Anything that moves an
 * entity outright (spawn, teleport, scene rebase) must write both, which is
 * what World_DrawPositionSet is for.
 */
struct WorldEntityFacet_DrawPosition
{
    uint32_t x;
    uint32_t z;
    uint32_t y;
    float fx;
    float fz;
};

/** Set both halves of a draw position at once (see the invariant above). */
static inline void
World_DrawPositionSet(
    struct WorldEntityFacet_DrawPosition* draw_position,
    int x,
    int z)
{
    draw_position->x = (uint32_t)x;
    draw_position->z = (uint32_t)z;
    draw_position->fx = (float)x;
    draw_position->fz = (float)z;
}

struct WorldEntityFacet_OrientationPYR
{
    uint16_t pitch;
    uint16_t yaw;
    uint16_t roll;
};

struct WorldEntityFacet_Orientation
{
    uint16_t yaw;
    uint16_t dst_yaw;
};

struct WorldEntityFacet_Action
{
    uint16_t code;
    char name[32];
};

/* Server-driven facing (reference ClientEntity.faceEntity / faceSquareX/Z).
 * The two are independent, not alternatives: an entity can be locked onto a
 * target *and* carry a pending square, and the reference applies both in that
 * order every tick (Client.ts entityFace, 3932). */
#define WORLD_FACING_ENTITY_NONE (-1)
/* faceEntity values >= this are player slots (value - 32768); below it, npc
 * slots (reference Client.ts:3937/3949). */
#define WORLD_FACING_PLAYER_BASE 32768

struct WorldEntityFacet_Facing
{
    /** Server slot of the locked-on entity, WORLD_FACING_ENTITY_NONE if any. */
    int entity_id;
    /** Pending face-coord, as the wire sends it: absolute half-tiles
     *  ((tile << 1) + 1). 0,0 = none — the reference's sentinel, and the
     *  reason this is stored raw rather than converted to a scene tile
     *  (scene tile 0 is a legitimate target). Cleared once consumed. */
    int square_x;
    int square_z;
    /** Angle used while an entity target is temporarily absent, or the direct
     * Face.Angle target. -1 means no fallback/direct angle is pending. */
    int fallback_angle;
    /** One-shot revision-239 Face.Angle request. It waits for route idle in
     * movement mode 0 and is consumed immediately in mode 1. */
    int direct_angle;
    /** Face immediately instead of stepping by turn_speed. */
    bool instant;
    /** Revision-239 Face header low bits. Mode 1 allows a loc/direct facing
     * request to take effect while a route is active; mode 0 waits for idle. */
    bool face_during_movement;
    /** NpcType.turnspeed (players: 32). 0 = the entity never turns and
     *  entityFace returns immediately. */
    int turn_speed;
};

#define WORLD_ENTITY_DAMAGE_SLOTS 4

struct WorldEntityFacet_Combat
{
    uint8_t damage_values[WORLD_ENTITY_DAMAGE_SLOTS];
    uint8_t damage_types[WORLD_ENTITY_DAMAGE_SLOTS];
    int damage_start_cycles[WORLD_ENTITY_DAMAGE_SLOTS];
    int damage_cycles[WORLD_ENTITY_DAMAGE_SLOTS];
    int combat_cycle;
    int health;
    int total_health;
    /*
     * The overhead health bar, when the server sent one (reference
     * HealthBarUpdate). Distinct from health/total_health above, which are the
     * gameplay hitpoints a legacy dat1 hitsplat block carries: a HEADBAR block
     * carries no hitpoints at all, only a fill fraction of the healthbar
     * type's own `width`, and how that becomes a pixel span is the type's
     * business. See src/game/rs_healthbar.h.
     *
     * The reference keeps up to four bars per entity, each with up to four
     * queued updates, sorted by the type's draw order. One is kept here
     * because one is what the protocol sends: both encoders write a single
     * bar, and the extras only ever appear in the block's count.
     *
     * `healthbar_type < 0` means no bar, and is the state a dat1 session never
     * leaves -- there the legacy fields above drive the old 30-wide rectangle.
     */
    int healthbar_type;
    /** loopCycle + startDelay: when the fill starts moving. */
    int healthbar_start_cycle;
    /** Cycles the fill takes to travel; 0 = it is already at end_fill. */
    int healthbar_duration;
    int healthbar_start_fill;
    int healthbar_end_fill;
    /** start_cycle + duration + the type's persist window. */
    int healthbar_end_cycle;
};

struct WorldEntityFacet_Appearance
{
    /* Effective equipment/kit layer used for rendering. */
    int slots[12];
    /* Revision-239's body-underneath layer. IF_SETPLAYERMODEL_SELF(false)
     * copies this over slots, exactly as PlayerComposition does. Classic
     * appearances populate it with the same values as slots. */
    int identkit[12];
    int colors[5];
};

/* Overhead chat text (reference ClientEntity.chatMessage/chatColour/chatEffect/
 * chatTimer). timer counts down from 150 each cycle; at 0 the message is gone
 * and nothing is drawn. colour/effect select the render style (see
 * drawEntities, Client.ts:4958). message is UTF-8, NUL-terminated. */
#define WORLD_ENTITY_CHAT_MAXLEN 100

struct WorldEntityFacet_Chat
{
    char message[WORLD_ENTITY_CHAT_MAXLEN];
    int timer;
    int colour;
    int effect;
};

#endif
