#ifndef SRC_ENGINE_ENTITY_SPOTANIM_H
#define SRC_ENGINE_ENTITY_SPOTANIM_H

/*
 * The table of entities wearing attached graphics (the SPOTANIM mask), and
 * for each one which graphics are merged onto its body and at which frames.
 *
 * While a graphic is active the entity's scene element wears the reference
 * getTempModel Model.combine([body, spot...]). An entry holds what that
 * needs: `body`, the owned pristine snapshot put back on detach; one owned
 * spot base model per graphic slot; and `combined`, the identity of the
 * merged model on the element (owned by the element, compared and never
 * dereferenced).
 *
 * An entry is keyed by the body's scene element AND by the entity it was
 * made for. Scene element ids are recycled, so an element id alone outlives
 * its owner and then aliases whoever is handed that id next; detaching such
 * an entry would push the previous owner's body onto the new occupant's
 * element. A lookup that finds the element under a different owner hands
 * that entry back as recycled instead of as a match.
 *
 * A merge is rebuilt only when it is missing, when the set of slots drawn
 * changed, or when a drawn slot's graphic is at a different frame from the
 * one baked in. A slot whose graphic changed drops its model and its frame.
 *
 * The table grows: a crowd wearing graphics is never a graphic that silently
 * does not draw (it was a fixed 64, and the 65th entity's graphic was dropped
 * with nothing to say so). Growing moves the entries, so an entry pointer is
 * not held across a Claim.
 *
 * Nothing here frees a model, reads a scene or a world, or loads anything.
 * Where a model leaves an entry it is handed back to the caller to free. The
 * caller decides which slots are active and resident, what frame each is at,
 * builds and merges the models, swaps them onto the element, and frees or
 * hands off every model before it clears an entry.
 */

#include <stdbool.h>

struct ToriDraw_Model;

/** Entries the table starts with; it doubles when they are all in use. */
#define ENTITY_SPOTANIMS_INITIAL 64

/* One of the entity's graphics, at its position in the entity's own list (the
 * world's, sorted by wire slot: WorldEntityFacet_EntitySpotanims). */
struct EntitySpotanimSlot
{
    int spotanim_id;   /* -1 = slot empty */
    int applied_frame; /* spot seq frame baked into `combined`; -1 none */
    bool merged;       /* drawn in `combined` */
    struct ToriDraw_Model* spot;
};

struct EntitySpotanim
{
    int body_element_id; /* -1 = free entry */
    /*
     * WORLD_ENTITY_ID of the entity this snapshot was taken from.
     *
     * The element id ALONE is not an identity: scene element ids are
     * recycled, so an entry keyed only by `body_element_id` outlives its
     * owner and then aliases whoever is handed that id next. Detaching such
     * an entry pushes `body` -- the previous owner's model -- onto the new
     * occupant's element and hands over ownership, which is how calling a
     * familiar in the QBD arena made the Queen's model follow the player.
     * The liveness sweep cannot see it either: a recycled id IS live.
     */
    int owner_entity_id;
    /* Every slot drawing is merged onto the one body: one per entry of the
     * entity's graphic list, as many as it has (EntitySpotanim_Slots). */
    struct EntitySpotanimSlot* slots;
    int slot_count;
    struct ToriDraw_Model* body;
    struct ToriDraw_Model* combined;
};

struct EntitySpotanims
{
    struct EntitySpotanim* entries;
    int capacity;
};

/**
 * ENTITY_SPOTANIMS_INITIAL free entries. Element id 0 is valid, so a free
 * entry is -1, not 0.
 */
void
EntitySpotanims_Init(struct EntitySpotanims* table);

/** Release the entries. The caller has freed every model they held. */
void
EntitySpotanims_Free(struct EntitySpotanims* table);

/**
 * The first entry whose body element is `body_element_id`, or NULL.
 *
 * `owner_entity_id` 0 matches any owner. Otherwise, when the first entry for
 * a live element (>= 0) belongs to another owner, the element id was
 * recycled: that entry is stored in `*out_recycled`, for the caller to drop
 * without restoring, and NULL is returned. `*out_recycled` is NULL otherwise.
 */
struct EntitySpotanim*
EntitySpotanims_Find(
    struct EntitySpotanims* table,
    int body_element_id,
    int owner_entity_id,
    struct EntitySpotanim** out_recycled);

/**
 * Take the first free entry for (element, owner): every slot empty, nothing
 * merged. With none free the table doubles first; every entry pointer the
 * caller held is stale after this.
 */
struct EntitySpotanim*
EntitySpotanims_Claim(
    struct EntitySpotanims* table,
    int body_element_id,
    int owner_entity_id);

/**
 * The entry is free again, every field cleared and its slots released. The
 * caller has already freed or handed off its body and spot models.
 */
void
EntitySpotanim_Clear(struct EntitySpotanim* entry);

/**
 * The entry's slots, at least `count` of them: grown with empty slots when
 * the entity's graphic list grew. Every slot pointer the caller held is stale
 * after this.
 */
struct EntitySpotanimSlot*
EntitySpotanim_Slots(
    struct EntitySpotanim* entry,
    int count);

/**
 * The slot wants graphic `spotanim_id` (-1: none). When that is a change the
 * slot is emptied of its model and frame and takes the new id; the model it
 * held is returned for the caller to free. Returns NULL when there was no
 * model or nothing changed.
 */
struct ToriDraw_Model*
EntitySpotanim_SlotWant(
    struct EntitySpotanimSlot* slot,
    int spotanim_id);

/** No merge is on the element any more: nothing combined, no frames baked. */
void
EntitySpotanim_ForgetMerge(struct EntitySpotanim* entry);

/**
 * Must the merge be rebuilt to draw the slots with `draw[i]` set at
 * `frames`? Only `frames[i]` for a drawn slot is read, for i < `spot_count`
 * (at most the entry's slot_count); a slot past `spot_count` is not drawn.
 */
int
EntitySpotanim_NeedsMerge(
    struct EntitySpotanim const* entry,
    bool const* draw,
    int const* frames,
    int spot_count);

/** `merged` is on the element, drawing the slots with `draw[i]` set at
 *  `frames`. */
void
EntitySpotanim_NoteMerged(
    struct EntitySpotanim* entry,
    struct ToriDraw_Model* merged,
    bool const* draw,
    int const* frames,
    int spot_count);

#endif
