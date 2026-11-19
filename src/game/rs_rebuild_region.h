#ifndef SRC_GAME_RS_REBUILD_REGION_H
#define SRC_GAME_RS_REBUILD_REGION_H

/*
 * Which region the server last told the client to build.
 *
 * Two things, which are not always written at the same moment:
 *
 *   - The centre zone of the last REBUILD the client accepted (deob
 *     field1192/field474, Client-TS mapBuildCenterZoneX/Z). It is written when
 *     the REBUILD is accepted, and it is what a same-zone REBUILD is compared
 *     against for the early-out. -1 until the first REBUILD, and set back to -1
 *     by the caller when a build is abandoned so the next REBUILD naming that
 *     zone is not skipped. The scene's south-west tile is six zones off it.
 *
 *   - The dynamic-rebuild template grid of the scene that is SHOWN, kept after
 *     the world load has consumed the packet's copy: it is the only thing a
 *     client is told about an instance's layout. A blocking rebuild writes
 *     it just after the centre, before the scene loads; a background rebuild
 *     writes it when the job commits the scene, later than the centre moves.
 *     So it carries its own centre zone, and a reader never pairs one
 *     rebuild's grid with another's scene base.
 *     `[level*169 + dx*13 + dz]`, packed as struct PktMapRebuild documents,
 *     dx/dz counted from six zones south-west of that centre.
 *
 *   - Whether the next REBUILD must be honoured even if it names the zone the
 *     client already stands in. Raised when a session is re-established (that
 *     rebuild is the server's whole world state arriving again, and its
 *     acknowledgement releases the rest of the burst), after a cache reload
 *     the scene must be rebuilt from, and by a replay keyframe; taken by the
 *     next REBUILD, which clears it whether or not it was raised, and dropped
 *     by a replay keyframe that keeps the scene it finds.
 *
 * This does not know when a REBUILD is accepted, built, committed or
 * abandoned, whether the world is active, or what a packed template means
 * beyond 0 meaning none. The caller says when each half changes.
 */

#include <stdbool.h>
#include <stdint.h>

/** Template cells in a kept grid: 4 levels of 13x13 zones. */
#define RS_REBUILD_REGION_ZONES (4 * 13 * 13)

struct RS_RebuildRegion
{
    /** Last accepted REBUILD's centre zone; -1 until the first. */
    int zone_x;
    int zone_z;
    /** The shown scene's template grid, and the centre it is indexed from. */
    int32_t instance_zones[RS_REBUILD_REGION_ZONES];
    int instance_zone_x;
    int instance_zone_z;
    bool instance_active;
    /** The next REBUILD is honoured even for the same zone. */
    bool force_next;
};

/** No REBUILD accepted yet: the centre is -1,-1. The kept grid is untouched. */
void
RS_RebuildRegion_ForgetCentre(struct RS_RebuildRegion* region);

/** A REBUILD centred on this zone was accepted. */
void
RS_RebuildRegion_SetCentre(
    struct RS_RebuildRegion* region,
    int zone_x,
    int zone_z);

int
RS_RebuildRegion_CentreX(struct RS_RebuildRegion const* region);

int
RS_RebuildRegion_CentreZ(struct RS_RebuildRegion const* region);

/** Whether the accepted centre is this zone. */
bool
RS_RebuildRegion_IsCentre(
    struct RS_RebuildRegion const* region,
    int zone_x,
    int zone_z);

/** The scene's south-west tile, absolute: (centre - 6) * 8. Meaningful once a
 *  REBUILD has been accepted. */
int
RS_RebuildRegion_SceneBaseX(struct RS_RebuildRegion const* region);

int
RS_RebuildRegion_SceneBaseZ(struct RS_RebuildRegion const* region);

/**
 * The shown scene changed: keep its grid, or none. `zones` is
 * RS_REBUILD_REGION_ZONES cells, or NULL for a scene that is not an instance,
 * which leaves the previous cells in place but marks the grid inactive.
 */
void
RS_RebuildRegion_KeepInstance(
    struct RS_RebuildRegion* region,
    int32_t const* zones,
    int zone_x,
    int zone_z);

bool
RS_RebuildRegion_InstanceActive(struct RS_RebuildRegion const* region);

/** The kept cells, RS_REBUILD_REGION_ZONES of them. Meaningful only while the
 *  grid is active. */
int32_t const*
RS_RebuildRegion_InstanceGrid(struct RS_RebuildRegion const* region);

/**
 * The packed template for absolute zone (zone_x, zone_z) on `level`, or 0 when
 * there is no active grid, the level is not 0..3, or the zone is outside the
 * grid.
 */
int32_t
RS_RebuildRegion_InstanceTemplate(
    struct RS_RebuildRegion const* region,
    int level,
    int zone_x,
    int zone_z);

/** The next REBUILD must run even if it names the accepted centre. */
void
RS_RebuildRegion_Force(struct RS_RebuildRegion* region);

/** Whether the next REBUILD was forced, and the force cleared: once per
 *  REBUILD, raised or not. */
bool
RS_RebuildRegion_TakeForced(struct RS_RebuildRegion* region);

#endif /* SRC_GAME_RS_REBUILD_REGION_H */
