#ifndef RS_HITSPLAT_H
#define RS_HITSPLAT_H

/*
 * Hitsplat types: which sprite a damage splat is drawn with.
 *
 * The client used to look for a sprite *archive* called "hitmarks" (or
 * "hitmark") and index it by damage type — which is how the 2004-era caches
 * ship it, and how the dat1 path still works. An OldSchool cache at this
 * revision has no such archive. It has a **hitsplat config group** (group 32)
 * instead: one record per splat type, each naming an ordinary sprite id.
 *
 * The authoritative revision-239 cache carries 83 of them. Its ordinary red
 * damage splat is type 28 / sprite 1359 and its blue block splat is type 26 /
 * sprite 1358. Without this table `UITreeSceneBridge_StaticSpriteSceneId`
 * returns -1, the sprite half of the overlay is skipped, and a hit renders as a
 * bare number floating over the entity — which is exactly what it did.
 *
 * Three fields are kept. The sprite id is what draws the splat; `duration`
 * (opcode 9) is how long it stays up, and `slot_policy` (opcode 12) is what the
 * entity does when all its hitmark slots are already busy. The last two used to
 * be hardcoded in `World_EntityAddHitmark` as 70 and "drop it" — which happen to
 * be the reference's own DEFAULTS, so the hardcoding was right until a record
 * overrode them. See `3rd/rscache/src/datatypes/dat2_config_hitsplat.h`.
 */

enum
{
    RS_HITSPLAT_OSRS239_BLOCK = 26,
    RS_HITSPLAT_OSRS239_DAMAGE = 28
};

struct RS_Hitsplats
{
    /** Indexed by hitsplat type. -1 = no sprite, which is a real state: 25 of
     *  cache.osrs230's records have none. */
    int* sprite_ids;
    /** Opcode 9, in client cycles. Defaults to 70 per record. */
    int* durations;
    /** Opcode 12: -1 discard, 0 evict oldest, 1 evict smallest. Defaults to -1. */
    int* slot_policies;
    int count;
};

void
RS_Hitsplats_Init(struct RS_Hitsplats* hitsplats);

void
RS_Hitsplats_Free(struct RS_Hitsplats* hitsplats);

/** Takes ownership of all three arrays. `durations`/`slot_policies` may be NULL,
 *  in which case every type reports the reference's defaults. */
int
RS_Hitsplats_SetTypes(
    struct RS_Hitsplats* hitsplats,
    int* sprite_ids,
    int* durations,
    int* slot_policies,
    int count);

/** Sprite id for a hitsplat type, or -1 when the type has none or the table was
 *  never loaded. Callers must treat -1 as "draw the number alone" rather than
 *  as an error: a cache with no hitsplat group is a supported configuration. */
int
RS_Hitsplats_SpriteFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type);

/** Splat duration in client cycles. Falls back to the reference's 70. */
int
RS_Hitsplats_DurationFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type);

/** Slot-full policy. Falls back to the reference's -1 (discard the new splat). */
int
RS_Hitsplats_SlotPolicyFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type);

#endif
