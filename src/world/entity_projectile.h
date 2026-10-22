#ifndef WORLD_ENTITY_PROJECTILE_H
#define WORLD_ENTITY_PROJECTILE_H

#include "entity_facets.h"

#include <stdbool.h>

/** No target entity — the projectile flies to its fixed destination tile
 * (reference ClientProj.target == 0). */
#define WORLD_PROJECTILE_TARGET_NONE 0

struct WorldEntity_Projectile
{
    int element_id;
    /** The source's plane: the one the projectile is painted on (rev-239
     *  method4319 adds it on field374, the source level). */
    int level;
    /** The destination's plane: the one the landing height is sampled on
     *  (rev-239 method8271 called with field357). */
    int dst_level;

    /* Launch params (world units / ticks). src_x/src_z/h1 are fixed at spawn
     * unless `source` names an actor, in which case the first launched cycle
     * re-reads them from that actor. */
    int src_x;
    int src_z;
    int h1;
    /** Height above the source ground the arc starts at (the wire start
     *  height, already x4). Kept so a source actor's ground can be re-read. */
    int start_height;
    int end_height;
    int t1;
    int t2;
    int angle;
    int startpos;

    /** Wire target-entity id (reference MAP_PROJANIM targetEntity, `g2b`):
     * `slot + 1` for an NPC, `-(slot) - 1` for a player (the local player
     * included, via its server pid), WORLD_PROJECTILE_TARGET_NONE for none.
     * Kept in the wire encoding rather than split into kind+slot so the
     * despawn/re-sync id space stays the server's. */
    int target;
    /** Wire source-actor id, same encoding as `target` (rev-239 MapProjAnimV2
     *  `source_index`, Statics.method8271's first argument);
     *  WORLD_PROJECTILE_TARGET_NONE when the packet names none. */
    int source;
    /** The destination as the packet stated it. rev-239 aims back here every
     *  cycle the target actor cannot be found. */
    int wire_dst_x;
    int wire_dst_z;

    /** The spotanimtype this projectile draws (MAP_PROJANIM's id), -1 for a
     *  model-only one (the debug hotkey). World never reads it; the quest
     *  driver does (DriveUi_Projectiles). Set by the spawner after
     *  World_ProjectileSpawn. */
    int spotanim_id;
    /** The spotanimtype's seq, still to be bound to the element; -1 once
     *  bound or for none. rev-239 class50.method940 steps a projectile's
     *  animation only on cycles >= t1, so frame 0 is the launch, not the
     *  packet: the app binds it on the first launched frame. World never
     *  reads it. */
    int unbound_seq;

    /* Dynamic state. dst_x/dst_z are re-aimed every cycle at the target
     * entity's live draw position while it stays synced (reference
     * addProjectiles); with no target they hold the spawn destination. */
    int dst_x;
    int dst_z;
    int cycle;
    bool launched;
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    double velocity;
    double ay;
    struct WorldEntityFacet_OrientationPYR orientation;

    /*
     * A rev-241 scripted projectile (SCRIPTEDPROJ_ADD, deob class56): no arc,
     * no velocity. Every cycle from t1 the position is the ORIGIN plus the
     * three curve clientscripts' answers on the progress, along the axes of
     * origin -> target (class56.method1439). The fields above that it shares
     * are the ends (src/dst, levels, heights, source/target ids), the times and
     * x/y/z/orientation; vx..ay go unused.
     */
    bool scripted;
    /** H (sideways), A (up) and T (along) curve clientscript ids. */
    int curve[3];
    /** Fine offsets added to each end after its actor or tile is found. */
    int src_offset_x;
    int src_offset_z;
    int dst_offset_x;
    int dst_offset_z;
    /** The fixed start, read once at the first launched cycle (the deob's
     *  field508/field517/field510). */
    float origin_x;
    float origin_y;
    float origin_z;
};

#endif
