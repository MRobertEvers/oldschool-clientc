#include "game/rs_hitsplat.h"
#include <assert.h>

#include "varp/varp_manager.h"
#include "world/entity_pathing.h"

#include <stdlib.h>
#include <string.h>

void
RS_Hitsplats_Init(struct RS_Hitsplats* hitsplats)
{
    memset(hitsplats, 0, sizeof(*hitsplats));
}

/** Release the selector table, whose entries own a list each. */
static void
hitsplats_free_variants(struct RS_Hitsplats* hitsplats)
{
    if( !hitsplats->variants )
        return;
    for( int i = 0; i < hitsplats->count; i++ )
        free(hitsplats->variants[i].ids);
    free(hitsplats->variants);
    hitsplats->variants = NULL;
}

void
RS_Hitsplats_Free(struct RS_Hitsplats* hitsplats)
{
    hitsplats_free_variants(hitsplats);
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
    struct RS_HitsplatVariants* variants,
    int count)
{
    if( count <= 0 )
        return 0;
    assert(sprite_ids);
    /* Before the count is overwritten — `hitsplats_free_variants` walks the OLD
     * table and has to be told its old length. */
    hitsplats_free_variants(hitsplats);
    free(hitsplats->sprite_ids);
    free(hitsplats->durations);
    free(hitsplats->slot_policies);
    hitsplats->sprite_ids = sprite_ids;
    hitsplats->durations = durations;
    hitsplats->slot_policies = slot_policies;
    hitsplats->variants = variants;
    hitsplats->count = count;
    return 1;
}

int
RS_Hitsplats_ResolveType(
    struct RS_Hitsplats const* hitsplats,
    struct VarPManager const* varps,
    int hitsplat_type)
{
    assert(hitsplats);

    if( !hitsplats->variants || hitsplat_type < 0 || hitsplat_type >= hitsplats->count )
        return hitsplat_type;

    struct RS_HitsplatVariants const* sel = &hitsplats->variants[hitsplat_type];
    if( sel->count <= 0 )
        return hitsplat_type;
    assert(sel->ids);

    /*
     * No var state yet: answer the fallback, which is the array's last entry —
     * the same answer the resolver gives for a var that reads out of range. A
     * splat in the default appearance beats no splat while the var table loads.
     */
    if( !varps )
        return sel->ids[sel->count - 1];

    /*
     * The same resolver multiloc and multinpc use, and deliberately not a second
     * copy of it: the reference's `GetMultiHitmark` and `GetMultiLoc` are the
     * same function with the names changed, down to the `v < size - 1` bound
     * that makes the last entry the fallback.
     */
    return VarPManager_ResolveTransform(varps, sel->ids, sel->count, sel->varbit, sel->varp);
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
