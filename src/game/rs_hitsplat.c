#include "game/rs_hitsplat.h"
#include <assert.h>

#include "world/entity_pathing.h"

#include <stdlib.h>
#include <string.h>

void
RS_Hitsplats_Init(struct RS_Hitsplats* hitsplats)
{
    memset(hitsplats, 0, sizeof(*hitsplats));
}

void
RS_Hitsplats_Free(struct RS_Hitsplats* hitsplats)
{
    free(hitsplats->sprite_ids);
    free(hitsplats->durations);
    free(hitsplats->slot_policies);
    hitsplats->sprite_ids = NULL;
    hitsplats->durations = NULL;
    hitsplats->slot_policies = NULL;
    hitsplats->count = 0;
}

int
RS_Hitsplats_SetTypes(
    struct RS_Hitsplats* hitsplats,
    int* sprite_ids,
    int* durations,
    int* slot_policies,
    int count)
{
    if( count <= 0 )
        return 0;
    assert(sprite_ids);
    free(hitsplats->sprite_ids);
    free(hitsplats->durations);
    free(hitsplats->slot_policies);
    hitsplats->sprite_ids = sprite_ids;
    hitsplats->durations = durations;
    hitsplats->slot_policies = slot_policies;
    hitsplats->count = count;
    return 1;
}

int
RS_Hitsplats_DurationFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type)
{
    /* The reference's own pre-loop default, not a guess — see the header. */
    if( !hitsplats->durations || hitsplat_type < 0 || hitsplat_type >= hitsplats->count )
        return WORLD_HITMARK_DEFAULT_DURATION;
    return hitsplats->durations[hitsplat_type];
}

int
RS_Hitsplats_SlotPolicyFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type)
{
    if( !hitsplats->slot_policies || hitsplat_type < 0 || hitsplat_type >= hitsplats->count )
        return WORLD_HITMARK_POLICY_DISCARD;
    return hitsplats->slot_policies[hitsplat_type];
}

int
RS_Hitsplats_SpriteFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type)
{
    if( !hitsplats->sprite_ids || hitsplat_type < 0 || hitsplat_type >= hitsplats->count )
        return -1;
    return hitsplats->sprite_ids[hitsplat_type];
}
