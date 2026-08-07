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
 * Only the sprite id is kept. The records carry more (opcodes 8, 9, 13 and an
 * 11-byte composite whose fields are not established — see
 * `3rd/rscache/src/datatypes/dat2_config_hitsplat.h`), none of which this
 * client has a use for yet.
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
    int count;
};

void
RS_Hitsplats_Init(struct RS_Hitsplats* hitsplats);

void
RS_Hitsplats_Free(struct RS_Hitsplats* hitsplats);

/** Takes ownership of `sprite_ids`. */
int
RS_Hitsplats_SetTypes(
    struct RS_Hitsplats* hitsplats,
    int* sprite_ids,
    int count);

/** Sprite id for a hitsplat type, or -1 when the type has none or the table was
 *  never loaded. Callers must treat -1 as "draw the number alone" rather than
 *  as an error: a cache with no hitsplat group is a supported configuration. */
int
RS_Hitsplats_SpriteFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type);

#endif
