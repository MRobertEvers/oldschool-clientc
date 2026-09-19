#ifndef RS_HITSPLAT_H
#define RS_HITSPLAT_H

struct VarPManager;

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
 *
 * ## The fourth field: a record can be a QUESTION rather than an appearance
 *
 * Opcode 17/18 makes a record a **multi-var selector** — a varbit, a varp and a
 * fallback, then a list of ids indexed by that var's value. It is the same
 * mechanism as multiloc and multinpc, and this file resolves it with the same
 * function (`VarPManager_ResolveTransform`).
 *
 * That is the whole of two All Settings rows, and until it existed both were
 * dead:
 *
 *   | setting | varbit | row |
 *   |--------:|-------:|-----|
 *   | 5   | 10236 | Hitsplat tinting |
 *   | 279 | 14196 | Max hit hitsplats |
 *
 * 34 of `cache.osrs239`'s 83 records are selectors, 25 keyed on 10236 and 9 on
 * 14196, and nothing else in the table is keyed on anything. They wrap leaves
 * and they pair: id 16 ("damage you dealt") resolves to leaf 28 whichever way
 * the setting is set, and id 17 ("damage someone else dealt") resolves to the
 * tinted leaf 29 when tinting is on and to 28 when it is off.
 *
 * **So the client IS told whose damage it was and whether it was a maximum** —
 * as which id the server sent. Nothing here has to infer it, and a sender that
 * picks the leaf has silently answered both settings on the player's behalf.
 *
 * Three details of the resolution are the reference's and are not free choices
 * (`HitmarkType::GetMultiHitmark`):
 *
 *   - **One hop.** The result is not re-tested for being a selector itself.
 *   - **A resolved -1 means draw no splat at all**, not "fall back". No record
 *     in this cache exercises it, and it is still implemented, because the
 *     alternative is a splat drawn for a hit the cache said to hide.
 *   - **Resolution belongs at DRAW time, not on receipt.** That is where the
 *     reference does it, and it is why toggling the setting re-skins splats that
 *     are already on screen instead of only the next one. `duration` and
 *     `slot_policy` are the exception: those are read off the type the WIRE
 *     named, at the moment it arrives, exactly as the reference reads `+0xc`
 *     before it swaps.
 */

enum
{
    RS_HITSPLAT_OSRS239_BLOCK = 26,
    RS_HITSPLAT_OSRS239_DAMAGE = 28
};

/**
 * One record's opcode 17/18 selector. `count == 0` for an ordinary appearance.
 *
 * `ids` is the stream's `count + 1` entries **followed by** the opcode-18
 * fallback, which is the reference's `count + 2` array laid out whole — so the
 * bound "a var value indexes it only below the last entry" is the array's own
 * length rather than a rule this file has to remember separately.
 */
struct RS_HitsplatVariants
{
    /** -1 when absent, in which case `varp` is consulted. */
    int varbit;
    /** -1 when absent. Used only when `varbit` is -1. */
    int varp;
    /** Owned. `count` entries, the last of which is the fallback. */
    int* ids;
    int count;
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
    /** Per type; `count == 0` on a type that is a plain appearance. May be NULL
     *  for a cache whose records carry no selector at all. */
    struct RS_HitsplatVariants* variants;
    int count;
};

void
RS_Hitsplats_Init(struct RS_Hitsplats* hitsplats);

void
RS_Hitsplats_Free(struct RS_Hitsplats* hitsplats);

/**
 * Takes ownership of all four arrays, and of every `variants[i].ids`.
 *
 * `durations`/`slot_policies` may be NULL, in which case every type reports the
 * reference's defaults; `variants` may be NULL, in which case no type is a
 * selector and `ResolveType` is the identity.
 */
int
RS_Hitsplats_SetTypes(
    struct RS_Hitsplats* hitsplats,
    int* sprite_ids,
    int* durations,
    int* slot_policies,
    struct RS_HitsplatVariants* variants,
    int count);

/**
 * Resolve a wire hitsplat type to the type that should actually be DRAWN.
 *
 * Returns the same id for an ordinary record, the selected id for a selector,
 * and **-1 when the selection says to draw nothing** — which callers must treat
 * as "skip this splat", not as an error.
 *
 * `varps` may be NULL (a cache loaded with no var state yet), which resolves
 * every selector to its fallback rather than refusing: a splat drawn in the
 * default appearance is better than a hit with no splat while the var table is
 * still loading.
 */
int
RS_Hitsplats_ResolveType(
    struct RS_Hitsplats const* hitsplats,
    struct VarPManager const* varps,
    int hitsplat_type);

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
