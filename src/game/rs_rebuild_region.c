#include "rs_rebuild_region.h"

#include <assert.h>
#include <string.h>

void
RS_RebuildRegion_ForgetCentre(struct RS_RebuildRegion* region)
{
    assert(region);
    region->zone_x = -1;
    region->zone_z = -1;
}

void
RS_RebuildRegion_SetCentre(
    struct RS_RebuildRegion* region,
    int zone_x,
    int zone_z)
{
    assert(region);
    region->zone_x = zone_x;
    region->zone_z = zone_z;
}

int
RS_RebuildRegion_CentreX(struct RS_RebuildRegion const* region)
{
    assert(region);
    return region->zone_x;
}

int
RS_RebuildRegion_CentreZ(struct RS_RebuildRegion const* region)
{
    assert(region);
    return region->zone_z;
}

bool
RS_RebuildRegion_IsCentre(
    struct RS_RebuildRegion const* region,
    int zone_x,
    int zone_z)
{
    assert(region);
    return region->zone_x == zone_x && region->zone_z == zone_z;
}

int
RS_RebuildRegion_SceneBaseX(struct RS_RebuildRegion const* region)
{
    assert(region);
    return (region->zone_x - 6) * 8;
}

int
RS_RebuildRegion_SceneBaseZ(struct RS_RebuildRegion const* region)
{
    assert(region);
    return (region->zone_z - 6) * 8;
}

void
RS_RebuildRegion_KeepInstance(
    struct RS_RebuildRegion* region,
    int32_t const* zones,
    int zone_x,
    int zone_z)
{
    assert(region);
    region->instance_active = zones != NULL;
    region->instance_zone_x = zone_x;
    region->instance_zone_z = zone_z;
    if( zones )
        memcpy(region->instance_zones, zones, sizeof(region->instance_zones));
}

bool
RS_RebuildRegion_InstanceActive(struct RS_RebuildRegion const* region)
{
    assert(region);
    return region->instance_active;
}

int32_t const*
RS_RebuildRegion_InstanceGrid(struct RS_RebuildRegion const* region)
{
    assert(region);
    return region->instance_zones;
}

int32_t
RS_RebuildRegion_InstanceTemplate(
    struct RS_RebuildRegion const* region,
    int level,
    int zone_x,
    int zone_z)
{
    int dx;
    int dz;

    assert(region);
    if( !region->instance_active || level < 0 || level > 3 )
        return 0;
    dx = zone_x - (region->instance_zone_x - 6);
    dz = zone_z - (region->instance_zone_z - 6);
    if( dx < 0 || dx >= 13 || dz < 0 || dz >= 13 )
        return 0;
    return region->instance_zones[level * 169 + dx * 13 + dz];
}

void
RS_RebuildRegion_Force(struct RS_RebuildRegion* region)
{
    assert(region);
    region->force_next = true;
}

bool
RS_RebuildRegion_TakeForced(struct RS_RebuildRegion* region)
{
    bool forced;

    assert(region);
    forced = region->force_next;
    region->force_next = false;
    return forced;
}
